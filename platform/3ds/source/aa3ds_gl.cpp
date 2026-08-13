#include "aa3ds_gl.h"
#include "aa3ds_gl_extra.h"
#include "aa3ds_runtime.h"
#include "GL/glu.h"

#include <citro3d.h>
#include "aa_gl_vshader_shbin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace
{
constexpr u32 TransferFlags =
    GX_TRANSFER_FLIP_VERT(0) |
    GX_TRANSFER_OUT_TILED(0) |
    GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
    GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

constexpr GLenum GlInvalidEnum = 0x0500;
constexpr GLenum GlInvalidValue = 0x0501;
constexpr GLenum GlInvalidOperation = 0x0502;
constexpr GLenum GlOutOfMemory = 0x0505;

struct Vertex
{
    float color[4];
    float texcoord[4];
    float position[4];
};

struct Texture
{
    C3D_Tex texture{};
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool initialized = false;
    GPU_TEXTURE_FILTER_PARAM magFilter = GPU_LINEAR;
    GPU_TEXTURE_FILTER_PARAM minFilter = GPU_LINEAR;
    bool wantsMipmaps = false;
    bool hasMipmaps = false;
    // Alpha only textures here are glyph atlases. They are drawn at exactly
    // one texel per pixel, and FreeType has already antialiased them, so
    // bilinear filtering only smears them across neighbouring pixels.
    bool isGlyphAtlas = false;
    GPU_TEXTURE_WRAP_PARAM wrapS = GPU_REPEAT;
    GPU_TEXTURE_WRAP_PARAM wrapT = GPU_REPEAT;
};

struct ArrayState
{
    GLint size = 0;
    GLenum type = GL_FLOAT;
    GLsizei stride = 0;
    const std::uint8_t* pointer = nullptr;
    bool enabled = false;
};

struct AttribState
{
    bool blend = false;
    bool depth = false;
    bool cull = false;
    bool alpha = false;
    bool scissor = false;
    bool texture = false;
    GLenum blendSource = GL_ONE;
    GLenum blendDestination = GL_ZERO;
};

bool initialized = false;
bool frameActive = false;
C3D_RenderTarget* topLeft = nullptr;
C3D_RenderTarget* topRight = nullptr;
C3D_RenderTarget* bottom = nullptr;
C3D_RenderTarget* currentTarget = nullptr;
gfxScreen_t currentScreen = GFX_TOP;
gfx3dSide_t currentSide = GFX_LEFT;

DVLB_s* shaderBinary = nullptr;
shaderProgram_s shaderProgram{};
int modelViewUniform = -1;
int projectionUniform = -1;
int textureUniform = -1;

C3D_MtxStack modelViewStack{};
C3D_MtxStack projectionStack{};
C3D_MtxStack textureStack{};
C3D_MtxStack* currentMatrixStack = &modelViewStack;

std::vector<Vertex> vertices;
GLenum primitiveMode = GL_TRIANGLES;
bool primitiveOpen = false;

// Immediate mode pushes twelve GPU command words per vertex, which overflows
// citro3d's command buffer and makes libctru panic in a busy scene. Vertices
// go through a linear-memory buffer instead, so a batch of any size costs one
// draw call worth of commands.
constexpr std::size_t kVertexCapacity = 49152;
Vertex* vertexBuffer = nullptr;
std::size_t vertexBufferUsed = 0;
std::size_t vertexBufferFlushed = 0;
std::vector<Vertex> triangleScratch;
bool commandBudgetReported = false;
// How often a frame had to be submitted early to make room. Nothing is lost
// when this happens, but it costs a GPU round trip, so it is worth knowing.
unsigned commandBudgetSplits = 0;

// Lines are expanded to quads in screen space, so the width has to be applied
// after the modelview and projection transforms rather than in object space.
// The bottom screen map scales the modelview by about 1/200, which turned an
// object space width into something far under a pixel: the arena rim and the
// cycle trails simply disappeared.
bool lineSpaceIsScreen = false;

// Cycle trails are drawn as a wall quad plus a line along its top edge. Seen
// edge on, which is most of the time when driving straight, the quad is
// sub-pixel and that line is the whole trail. One and a half pixels was enough
// to stop it disappearing but too thin to read as a wall on a 400x240 panel,
// so the default is wider and WALL_LINE_WIDTH can tune it on hardware.
float lineHalfWidthPixels = 1.5f;

float currentColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
float currentTexcoord[4] = {0.0f, 0.0f, 0.0f, 1.0f};
float currentNormal[3] = {0.0f, 0.0f, 1.0f};

bool blendEnabled = false;
bool depthEnabled = false;
bool cullEnabled = false;
bool alphaEnabled = false;
bool scissorEnabled = false;
bool textureEnabled = false;
bool lightingEnabled = false;
bool colorMaterialEnabled = false;

GLenum blendSource = GL_ONE;
GLenum blendDestination = GL_ZERO;
GLenum depthFunction = GL_LESS;
GLenum alphaFunction = GL_ALWAYS;
float alphaReference = 0.0f;
GLenum cullMode = GL_BACK;
GLenum frontFace = GL_CCW;
GLenum textureEnvironment = GL_MODULATE;

// Armagetron draws the top edge of every cycle wall as a line and relies on
// polygon offset to hold it in front of the wall quad. Without the offset the
// two sit at identical depth and z-fight, so a trail seen edge on at distance,
// where that top edge is all there is to see, flickers in and out as the
// camera moves. The PICA200 has no per-primitive polygon offset, but its depth
// map takes a constant offset, which is what the fixed offset Armagetron asks
// for amounts to.
bool polygonOffsetEnabled = false;
float polygonOffsetUnits = 0.0f;

bool colorWrite[4] = {true, true, true, true};
bool depthWrite = true;
// Half the eye separation, in world units, signed: negative while the left eye
// is being drawn, positive for the right, zero when the screen is flat. The
// projection picks this up, so every perspective the game builds gets the
// offset without any of the drawing code knowing about it.
float stereoEyeOffset = 0.0f;

// How far away something has to be to sit at the depth of the screen itself.
// Nearer than this it stands out of the panel, further and it sits inside.
float stereoFocalLength = 20.0f;

// What the top screen has been told to do, against what it should be doing.
// Switching costs a mode change on the display, so it is only done when the
// answer actually changes, and between frames.
bool stereoOutputEnabled = false;
bool stereoOutputWanted = false;

float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
double clearDepth = 1.0;

int viewportX = 0;
int viewportY = 0;
int viewportWidth = 240;
int viewportHeight = 400;
int scissorX = 0;
int scissorY = 0;
int scissorWidth = 240;
int scissorHeight = 400;

GLenum glErrorState = GL_NO_ERROR;
GLuint nextTextureId = 1;
GLuint boundTextureId = 0;
std::map<GLuint, Texture> textures;

// Shared staging buffer for texture uploads. A tiled copy is only needed
// between building it and handing it to the GPU, so one buffer serves every
// texture instead of each texture carrying its own permanent copy.
std::vector<std::uint32_t> tiledScratch;

ArrayState vertexArray;
ArrayState colorArray;
ArrayState texcoordArray;
ArrayState normalArray;

int unpackAlignment = 4;
int unpackRowLength = 0;
int proxyTextureWidth = 0;
int proxyTextureHeight = 0;
std::vector<AttribState> attribStack;
std::vector<std::pair<int, int>> clientAttribStack;

void setError(GLenum error)
{
    if (glErrorState == GL_NO_ERROR)
        glErrorState = error;
}

GPU_TESTFUNC convertTest(GLenum function)
{
    switch (function)
    {
    case GL_NEVER: return GPU_NEVER;
    case GL_LESS: return GPU_LESS;
    case GL_EQUAL: return GPU_EQUAL;
    case GL_LEQUAL: return GPU_LEQUAL;
    case GL_GREATER: return GPU_GREATER;
    case GL_NOTEQUAL: return GPU_NOTEQUAL;
    case GL_GEQUAL: return GPU_GEQUAL;
    case GL_ALWAYS: return GPU_ALWAYS;
    default:
        setError(GlInvalidEnum);
        return GPU_ALWAYS;
    }
}

GPU_BLENDFACTOR convertBlend(GLenum factor)
{
    switch (factor)
    {
    case GL_ZERO: return GPU_ZERO;
    case GL_ONE: return GPU_ONE;
    case GL_SRC_COLOR: return GPU_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return GPU_ONE_MINUS_SRC_COLOR;
    case GL_SRC_ALPHA: return GPU_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return GPU_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return GPU_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return GPU_ONE_MINUS_DST_ALPHA;
    case GL_DST_COLOR: return GPU_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return GPU_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA_SATURATE: return GPU_SRC_ALPHA_SATURATE;
    default:
        setError(GlInvalidEnum);
        return GPU_ONE;
    }
}

GPU_TEXTURE_FILTER_PARAM convertFilter(GLenum filter)
{
    switch (filter)
    {
    case GL_NEAREST:
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
        return GPU_NEAREST;
    default:
        return GPU_LINEAR;
    }
}

bool filterWantsMipmaps(GLenum filter)
{
    switch (filter)
    {
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_LINEAR:
        return true;
    default:
        return false;
    }
}

// The PICA200 can only mipmap power of two textures.
bool canMipmap(int width, int height)
{
    return width >= 8 && height >= 8 &&
           (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
}

// Smallest texture the hardware will hold.
constexpr int kMinimumTextureSize = 8;

// Largest texture kept, in texels on a side. The hardware ceiling is 1024, but
// the practical one is lower: nothing on a 400x240 panel resolves more detail
// than 512, and every texel is linear memory this console has little of.
int maxTextureSize = 512;

// Rounds a requested size to something the hardware will accept: a power of
// two, at least eight texels and at most the current limit.
int fitTextureDimension(int size)
{
    int fitted = kMinimumTextureSize;
    while (fitted * 2 <= size && fitted * 2 <= maxTextureSize)
        fitted *= 2;
    return fitted;
}

// Rescales an RGBA image, averaging the source texels under each destination
// texel. Point sampling would sparkle on the reductions a real moviepack
// needs, and averaging costs nothing worth counting once per texture load.
void resampleRgba(
    const std::vector<std::uint8_t>& source, int sourceWidth, int sourceHeight,
    std::vector<std::uint8_t>& destination, int width, int height)
{
    destination.assign(static_cast<std::size_t>(width) * height * 4, 0);
    if (sourceWidth <= 0 || sourceHeight <= 0)
        return;

    for (int y = 0; y < height; ++y)
    {
        const int y0 =
            static_cast<int>(static_cast<long long>(y) * sourceHeight / height);
        int y1 =
            static_cast<int>(static_cast<long long>(y + 1) * sourceHeight / height);
        if (y1 <= y0)
            y1 = y0 + 1;
        if (y1 > sourceHeight)
            y1 = sourceHeight;

        for (int x = 0; x < width; ++x)
        {
            const int x0 =
                static_cast<int>(static_cast<long long>(x) * sourceWidth / width);
            int x1 =
                static_cast<int>(static_cast<long long>(x + 1) * sourceWidth / width);
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > sourceWidth)
                x1 = sourceWidth;

            unsigned sums[4] = {0, 0, 0, 0};
            unsigned taps = 0;
            for (int sourceY = y0; sourceY < y1; ++sourceY)
            {
                const std::uint8_t* row =
                    source.data() +
                    static_cast<std::size_t>(sourceY) * sourceWidth * 4;
                for (int sourceX = x0; sourceX < x1; ++sourceX)
                {
                    const std::uint8_t* texel = row + static_cast<std::size_t>(sourceX) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                        sums[channel] += texel[channel];
                    ++taps;
                }
            }
            if (!taps)
                continue;

            std::uint8_t* out =
                destination.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            for (int channel = 0; channel < 4; ++channel)
                out[channel] =
                    static_cast<std::uint8_t>((sums[channel] + taps / 2) / taps);
        }
    }
}

GPU_TEXTURE_WRAP_PARAM convertWrap(GLenum wrap)
{
    switch (wrap)
    {
    case GL_CLAMP:
    case GL_CLAMP_TO_EDGE:
        return GPU_CLAMP_TO_EDGE;
    default:
        return GPU_REPEAT;
    }
}

GPU_WRITEMASK writeMask()
{
    unsigned mask = 0;
    if (colorWrite[0]) mask |= GPU_WRITE_RED;
    if (colorWrite[1]) mask |= GPU_WRITE_GREEN;
    if (colorWrite[2]) mask |= GPU_WRITE_BLUE;
    if (colorWrite[3]) mask |= GPU_WRITE_ALPHA;
    if (depthWrite) mask |= GPU_WRITE_DEPTH;
    return static_cast<GPU_WRITEMASK>(mask);
}

void applyRenderState()
{
    GPU_CULLMODE gpuCull = GPU_CULL_NONE;
    if (cullEnabled)
    {
        bool clockwise = frontFace == GL_CW;
        if (cullMode == GL_FRONT)
            gpuCull = clockwise ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
        else
            gpuCull = clockwise ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
    }
    C3D_CullFace(gpuCull);

    if (blendEnabled)
    {
        GPU_BLENDFACTOR source = convertBlend(blendSource);
        GPU_BLENDFACTOR destination = convertBlend(blendDestination);
        C3D_AlphaBlend(
            GPU_BLEND_ADD, GPU_BLEND_ADD,
            source, destination, source, destination);
    }
    else
    {
        C3D_AlphaBlend(
            GPU_BLEND_ADD, GPU_BLEND_ADD,
            GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }

    // The magnitude is a compromise: large enough to clear depth buffer
    // quantisation at the distances a wall is still worth seeing, small enough
    // not to pull the line through geometry that is genuinely in front of it.
    const float depthOffset =
        polygonOffsetEnabled && polygonOffsetUnits < 0.0f ? -0.0007f : 0.0f;
    C3D_DepthMap(true, -1.0f, depthOffset);

    C3D_DepthTest(depthEnabled, convertTest(depthFunction), writeMask());
    C3D_AlphaTest(
        alphaEnabled,
        convertTest(alphaFunction),
        static_cast<int>(std::clamp(alphaReference, 0.0f, 1.0f) * 255.0f));
    C3D_SetViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    C3D_SetScissor(
        scissorEnabled ? GPU_SCISSOR_NORMAL : GPU_SCISSOR_DISABLE,
        scissorX, scissorY,
        scissorX + scissorWidth,
        scissorY + scissorHeight);

    C3D_TexEnv* environment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(environment);
    auto boundTexture = textures.find(boundTextureId);
    if (textureEnabled && boundTextureId != 0 &&
        boundTexture != textures.end() && boundTexture->second.initialized)
    {
        C3D_TexBind(0, &boundTexture->second.texture);
        if (textureEnvironment == GL_REPLACE)
        {
            C3D_TexEnvSrc(
                environment, C3D_Both,
                GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
        }
        else
        {
            C3D_TexEnvSrc(
                environment, C3D_Both,
                GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_MODULATE);
        }
    }
    else
    {
        C3D_TexBind(0, nullptr);
        C3D_TexEnvSrc(
            environment, C3D_Both,
            GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
    }
}

void drawTriangle(const Vertex& a, const Vertex& b, const Vertex& c)
{
    triangleScratch.push_back(a);
    triangleScratch.push_back(b);
    triangleScratch.push_back(c);
}

// Transforms an object space vertex into clip space with the matrices
// currently on the stacks. The perspective divide is deliberately left to the
// caller, so a segment crossing the eye plane can be clipped first.
C3D_FVec clipVertex(const Vertex& vertex)
{
    C3D_FVec position;
    position.x = vertex.position[0];
    position.y = vertex.position[1];
    position.z = vertex.position[2];
    position.w = vertex.position[3];

    const C3D_FVec eye = Mtx_MultiplyFVec4(MtxStack_Cur(&modelViewStack), position);
    return Mtx_MultiplyFVec4(MtxStack_Cur(&projectionStack), eye);
}

void drawLine(const Vertex& a, const Vertex& b)
{
    // Expanding lines on the CPU means doing the perspective divide here, and
    // that means handling the eye plane here too. Dropping a segment with an
    // endpoint behind the eye is not good enough: Armagetron emits a whole
    // straight run of cycle wall as one segment, so a trail running back past
    // the camera lost its entire nearest span and looked truncated. Clip the
    // segment to the eye plane instead, the way the GPU would have.
    constexpr float kNearW = 0.0001f;

    C3D_FVec c0 = clipVertex(a);
    C3D_FVec c1 = clipVertex(b);

    if (c0.w < kNearW && c1.w < kNearW)
        return;

    if (c0.w < kNearW || c1.w < kNearW)
    {
        const float span = c1.w - c0.w;
        if (span > -1e-9f && span < 1e-9f)
            return;

        const float t = (kNearW - c0.w) / span;
        C3D_FVec crossing;
        crossing.x = c0.x + (c1.x - c0.x) * t;
        crossing.y = c0.y + (c1.y - c0.y) * t;
        crossing.z = c0.z + (c1.z - c0.z) * t;
        crossing.w = kNearW;

        if (c0.w < kNearW)
            c0 = crossing;
        else
            c1 = crossing;
    }

    const float firstInverseW = 1.0f / c0.w;
    const float secondInverseW = 1.0f / c1.w;
    const float first[3] = {
        c0.x * firstInverseW, c0.y * firstInverseW, c0.z * firstInverseW };
    const float second[3] = {
        c1.x * secondInverseW, c1.y * secondInverseW, c1.z * secondInverseW };

    // Work out the perpendicular in pixels so the line has the same weight
    // wherever it is and whatever the modelview scale happens to be.
    const float halfWidth = std::max(0.5f, lineHalfWidthPixels);
    const float halfViewportWidth = std::max(1, viewportWidth) * 0.5f;
    const float halfViewportHeight = std::max(1, viewportHeight) * 0.5f;

    float dx = (second[0] - first[0]) * halfViewportWidth;
    float dy = (second[1] - first[1]) * halfViewportHeight;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.0001f)
    {
        dx = 1.0f;
        dy = 0.0f;
        length = 1.0f;
    }

    const float offsetX = -dy / length * halfWidth / halfViewportWidth;
    const float offsetY = dx / length * halfWidth / halfViewportHeight;

    auto corner = [](const Vertex& source, const float ndc[3], float dxOffset, float dyOffset)
    {
        Vertex result = source;
        result.position[0] = ndc[0] + dxOffset;
        result.position[1] = ndc[1] + dyOffset;
        result.position[2] = ndc[2];
        result.position[3] = 1.0f;
        return result;
    };

    const Vertex firstLeft = corner(a, first, offsetX, offsetY);
    const Vertex firstRight = corner(a, first, -offsetX, -offsetY);
    const Vertex secondLeft = corner(b, second, offsetX, offsetY);
    const Vertex secondRight = corner(b, second, -offsetX, -offsetY);

    lineSpaceIsScreen = true;
    drawTriangle(firstLeft, firstRight, secondRight);
    drawTriangle(firstLeft, secondRight, secondLeft);
}

// Writes back the vertices recorded so far. GSPGPU_FlushDataCache is a service
// call, so it is done once per submitted command list rather than once per
// batch. The GPU only reads the buffer when the command list runs, so a single
// flush before submission covers every batch in it.
void flushVertexCache()
{
    if (vertexBuffer && vertexBufferFlushed < vertexBufferUsed)
    {
        GSPGPU_FlushDataCache(
            vertexBuffer + vertexBufferFlushed,
            (vertexBufferUsed - vertexBufferFlushed) * sizeof(Vertex));
        vertexBufferFlushed = vertexBufferUsed;
    }
}

// Uploads the staged triangles and issues the draw calls. Returns without
// drawing if the frame produced more geometry than one buffer can hold and the
// GPU could not be caught up.
void flushTriangles()
{
    if (triangleScratch.empty() || !vertexBuffer)
        return;

    // libctru panics outright when the command buffer fills, so something has
    // to give before that happens. Dropping the rest of the frame was the
    // first answer, and it is the wrong one in a busy round: what gets drawn
    // last is other players' walls, and a wall that is not drawn is a wall the
    // player cannot see and will drive into. Submit what is queued and carry
    // on with an empty list instead. The frame costs more; nothing goes
    // missing.
    if (C3D_GetCmdBufUsage() > 0.90f)
    {
        flushVertexCache();
        C3D_FrameSplit(0);
        gspWaitForP3D();
        ++commandBudgetSplits;

        // If a fresh list is still over the mark, the frame is beyond saving
        // and dropping beats a panic.
        if (C3D_GetCmdBufUsage() > 0.90f)
        {
            if (!commandBudgetReported)
            {
                commandBudgetReported = true;
                aa3ds_log("renderer: GPU command budget exhausted, dropping draws");
            }
            return;
        }
    }

    const std::size_t total = triangleScratch.size();
    for (std::size_t first = 0; first < total;)
    {
        std::size_t chunk = std::min(total - first, kVertexCapacity);
        chunk -= chunk % 3;
        if (chunk == 0)
            break;

        if (vertexBufferUsed + chunk > kVertexCapacity)
        {
            // The buffer filled inside a single frame. Submit what has been
            // recorded, wait for the GPU to consume it, and start over.
            flushVertexCache();
            C3D_FrameSplit(0);
            gspWaitForP3D();
            vertexBufferUsed = 0;
            vertexBufferFlushed = 0;
        }

        Vertex* destination = vertexBuffer + vertexBufferUsed;
        const std::size_t bytes = chunk * sizeof(Vertex);
        std::memcpy(destination, triangleScratch.data() + first, bytes);

        C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
        BufInfo_Init(bufferInfo);
        BufInfo_Add(bufferInfo, vertexBuffer, sizeof(Vertex), 3, 0x210);
        C3D_DrawArrays(
            GPU_TRIANGLES,
            static_cast<int>(vertexBufferUsed),
            static_cast<int>(chunk));

        vertexBufferUsed += chunk;
        first += chunk;
    }
}

void submitVertices()
{
    if (vertices.empty())
        return;

    MtxStack_Update(&modelViewStack);
    MtxStack_Update(&projectionStack);
    MtxStack_Update(&textureStack);
    applyRenderState();
    triangleScratch.clear();
    lineSpaceIsScreen = false;

    switch (primitiveMode)
    {
    case GL_TRIANGLES:
        for (std::size_t index = 0; index + 2 < vertices.size(); index += 3)
            drawTriangle(vertices[index], vertices[index + 1], vertices[index + 2]);
        break;
    case GL_TRIANGLE_STRIP:
        for (std::size_t index = 2; index < vertices.size(); ++index)
        {
            if (index & 1)
                drawTriangle(vertices[index - 1], vertices[index - 2], vertices[index]);
            else
                drawTriangle(vertices[index - 2], vertices[index - 1], vertices[index]);
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        for (std::size_t index = 2; index < vertices.size(); ++index)
            drawTriangle(vertices[0], vertices[index - 1], vertices[index]);
        break;
    case GL_QUADS:
        for (std::size_t index = 0; index + 3 < vertices.size(); index += 4)
        {
            drawTriangle(vertices[index], vertices[index + 1], vertices[index + 2]);
            drawTriangle(vertices[index], vertices[index + 2], vertices[index + 3]);
        }
        break;
    case GL_QUAD_STRIP:
        for (std::size_t index = 0; index + 3 < vertices.size(); index += 2)
        {
            drawTriangle(vertices[index], vertices[index + 1], vertices[index + 3]);
            drawTriangle(vertices[index], vertices[index + 3], vertices[index + 2]);
        }
        break;
    case GL_LINES:
        for (std::size_t index = 0; index + 1 < vertices.size(); index += 2)
            drawLine(vertices[index], vertices[index + 1]);
        break;
    case GL_LINE_STRIP:
        for (std::size_t index = 1; index < vertices.size(); ++index)
            drawLine(vertices[index - 1], vertices[index]);
        break;
    case GL_LINE_LOOP:
        for (std::size_t index = 1; index < vertices.size(); ++index)
            drawLine(vertices[index - 1], vertices[index]);
        if (vertices.size() > 1)
            drawLine(vertices.back(), vertices.front());
        break;
    case GL_POINTS:
        for (const Vertex& vertex : vertices)
        {
            Vertex right = vertex;
            Vertex top = vertex;
            right.position[0] += 0.006f;
            top.position[1] += 0.006f;
            drawTriangle(vertex, right, top);
        }
        break;
    default:
        setError(GlInvalidEnum);
        break;
    }

    if (lineSpaceIsScreen)
    {
        // Draw with identity transforms; the geometry is already projected.
        Mtx_Identity(MtxStack_Push(&modelViewStack));
        Mtx_Identity(MtxStack_Push(&projectionStack));
        MtxStack_Update(&modelViewStack);
        MtxStack_Update(&projectionStack);

        flushTriangles();

        MtxStack_Pop(&modelViewStack);
        MtxStack_Pop(&projectionStack);
        MtxStack_Update(&modelViewStack);
        MtxStack_Update(&projectionStack);
        lineSpaceIsScreen = false;
    }
    else
    {
        flushTriangles();
    }
}

Vertex makeVertex(float x, float y, float z, float w)
{
    Vertex vertex{};
    std::copy(currentColor, currentColor + 4, vertex.color);
    std::copy(currentTexcoord, currentTexcoord + 4, vertex.texcoord);
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    vertex.position[3] = w;
    return vertex;
}

Texture* boundTexture()
{
    auto found = textures.find(boundTextureId);
    return found == textures.end() ? nullptr : &found->second;
}

int sourceComponents(GLenum format)
{
    switch (format)
    {
    case GL_ALPHA:
    case GL_LUMINANCE:
        return 1;
    case GL_LUMINANCE_ALPHA:
        return 2;
    case GL_RGB:
    case GL_BGR:
        return 3;
    case GL_RGBA:
    case GL_BGRA:
        return 4;
    default:
        setError(GlInvalidEnum);
        return 0;
    }
}

void convertPixel(const std::uint8_t* source, GLenum format, std::uint8_t* destination)
{
    switch (format)
    {
    case GL_ALPHA:
        destination[0] = 255;
        destination[1] = 255;
        destination[2] = 255;
        destination[3] = source[0];
        break;
    case GL_LUMINANCE:
        destination[0] = source[0];
        destination[1] = source[0];
        destination[2] = source[0];
        destination[3] = 255;
        break;
    case GL_LUMINANCE_ALPHA:
        destination[0] = source[0];
        destination[1] = source[0];
        destination[2] = source[0];
        destination[3] = source[1];
        break;
    case GL_RGB:
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = 255;
        break;
    case GL_BGR:
        destination[0] = source[2];
        destination[1] = source[1];
        destination[2] = source[0];
        destination[3] = 255;
        break;
    case GL_RGBA:
        std::copy(source, source + 4, destination);
        break;
    case GL_BGRA:
        destination[0] = source[2];
        destination[1] = source[1];
        destination[2] = source[0];
        destination[3] = source[3];
        break;
    }
}

std::size_t mortonOffset(int x, int y, int width)
{
    unsigned inTile = 0;
    for (unsigned bit = 0; bit < 3; ++bit)
    {
        inTile |= ((static_cast<unsigned>(x) >> bit) & 1U) << (bit * 2);
        inTile |= ((static_cast<unsigned>(y) >> bit) & 1U) << (bit * 2 + 1);
    }
    return
        (static_cast<std::size_t>(y >> 3) * (width >> 3) + (x >> 3)) * 64 +
        inTile;
}

std::uint32_t packRgba8(const std::uint8_t* rgba)
{
    return
        (static_cast<std::uint32_t>(rgba[0]) << 24) |
        (static_cast<std::uint32_t>(rgba[1]) << 16) |
        (static_cast<std::uint32_t>(rgba[2]) << 8) |
        static_cast<std::uint32_t>(rgba[3]);
}


void applyTextureFilter(Texture& texture)
{
    if (texture.isGlyphAtlas)
        C3D_TexSetFilter(&texture.texture, GPU_NEAREST, GPU_NEAREST);
    else
        C3D_TexSetFilter(&texture.texture, texture.magFilter, texture.minFilter);
    if (texture.hasMipmaps && texture.wantsMipmaps)
        C3D_TexSetFilterMipmap(&texture.texture, GPU_LINEAR);
    C3D_TexSetWrap(&texture.texture, texture.wrapS, texture.wrapT);
}

// Halves an RGBA image with a box filter.
void downsampleRgba(
    const std::vector<std::uint8_t>& source, int width, int height,
    std::vector<std::uint8_t>& destination)
{
    const int halfWidth = std::max(1, width / 2);
    const int halfHeight = std::max(1, height / 2);
    destination.assign(static_cast<std::size_t>(halfWidth) * halfHeight * 4, 0);

    for (int y = 0; y < halfHeight; ++y)
    {
        const int y0 = std::min(y * 2, height - 1);
        const int y1 = std::min(y * 2 + 1, height - 1);
        for (int x = 0; x < halfWidth; ++x)
        {
            const int x0 = std::min(x * 2, width - 1);
            const int x1 = std::min(x * 2 + 1, width - 1);
            const std::size_t taps[4] = {
                (static_cast<std::size_t>(y0) * width + x0) * 4,
                (static_cast<std::size_t>(y0) * width + x1) * 4,
                (static_cast<std::size_t>(y1) * width + x0) * 4,
                (static_cast<std::size_t>(y1) * width + x1) * 4,
            };
            std::uint8_t* out =
                destination.data() + (static_cast<std::size_t>(y) * halfWidth + x) * 4;
            for (int channel = 0; channel < 4; ++channel)
            {
                unsigned sum = 0;
                for (std::size_t tap : taps)
                    sum += source[tap + channel];
                out[channel] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
}

// Tiles one mipmap level out of a plain RGBA image and hands it to the GPU.
void uploadTextureLevel(
    Texture& texture, const std::vector<std::uint8_t>& rgba,
    int width, int height, int level)
{
    tiledScratch.assign(static_cast<std::size_t>(width) * height, 0);
    for (int sourceY = 0; sourceY < height; ++sourceY)
    {
        const int destinationY = height - 1 - sourceY;
        for (int x = 0; x < width; ++x)
        {
            const std::size_t source =
                (static_cast<std::size_t>(sourceY) * width + x) * 4;
            tiledScratch[mortonOffset(x, destinationY, width)] =
                packRgba8(rgba.data() + source);
        }
    }
    C3D_TexLoadImage(
        &texture.texture, tiledScratch.data(), GPU_TEXFACE_2D, level);
}

void refreshTexture(Texture& texture);

void uploadTexture(Texture& texture)
{
    if (texture.initialized)
        C3D_TexDelete(&texture.texture);

    texture.hasMipmaps =
        !texture.isGlyphAtlas && canMipmap(texture.width, texture.height);

    const u16 width = static_cast<u16>(texture.width);
    const u16 height = static_cast<u16>(texture.height);
    texture.initialized =
        texture.hasMipmaps
            ? C3D_TexInitMipmap(&texture.texture, width, height, GPU_RGBA8)
            : C3D_TexInit(&texture.texture, width, height, GPU_RGBA8);

    // Fall back to a flat texture rather than giving up on the image; running
    // out of room for the extra third a mipmap chain costs is recoverable.
    if (!texture.initialized && texture.hasMipmaps)
    {
        texture.hasMipmaps = false;
        texture.initialized = C3D_TexInit(&texture.texture, width, height, GPU_RGBA8);
    }

    if (!texture.initialized)
    {
        setError(GlOutOfMemory);
        return;
    }

    applyTextureFilter(texture);
    refreshTexture(texture);
}

// Re-sends the pixel data of an already created texture.
void refreshTexture(Texture& texture)
{
    if (!texture.initialized)
        return;

    uploadTextureLevel(texture, texture.rgba, texture.width, texture.height, 0);

    if (texture.hasMipmaps)
    {
        std::vector<std::uint8_t> current = texture.rgba;
        std::vector<std::uint8_t> next;
        int width = texture.width;
        int height = texture.height;
        const int levels = C3D_TexCalcMaxLevel(texture.width, texture.height);
        for (int level = 1; level <= levels; ++level)
        {
            downsampleRgba(current, width, height, next);
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            uploadTextureLevel(texture, next, width, height, level);
            current.swap(next);
        }
    }

    // Nothing needs the source data again: the chain is already built and the
    // only thing that sub-images a texture is FreeType filling the glyph
    // atlas. Releasing it moves a quarter megabyte per texture off the
    // application heap, which is the scarce one on this console, at the cost
    // of a third more texture memory in the linear heap, which is not.
    if (!texture.isGlyphAtlas)
    {
        std::vector<std::uint8_t> discard;
        texture.rgba.swap(discard);
    }
}

float arrayComponent(const ArrayState& array, unsigned index, int component, float fallback)
{
    if (!array.pointer || component >= array.size)
        return fallback;
    int componentSize = array.type == GL_UNSIGNED_BYTE ? 1 : 4;
    int stride = array.stride ? array.stride : array.size * componentSize;
    const std::uint8_t* element = array.pointer + index * stride + component * componentSize;
    if (array.type == GL_FLOAT)
        return *reinterpret_cast<const float*>(element);
    if (array.type == GL_UNSIGNED_BYTE)
        return *element / 255.0f;
    setError(GlInvalidEnum);
    return fallback;
}

void emitArrayVertex(unsigned index)
{
    if (colorArray.enabled)
    {
        currentColor[0] = arrayComponent(colorArray, index, 0, 1.0f);
        currentColor[1] = arrayComponent(colorArray, index, 1, 1.0f);
        currentColor[2] = arrayComponent(colorArray, index, 2, 1.0f);
        currentColor[3] = arrayComponent(colorArray, index, 3, 1.0f);
    }
    if (texcoordArray.enabled)
    {
        currentTexcoord[0] = arrayComponent(texcoordArray, index, 0, 0.0f);
        currentTexcoord[1] = arrayComponent(texcoordArray, index, 1, 0.0f);
        currentTexcoord[2] = arrayComponent(texcoordArray, index, 2, 0.0f);
        currentTexcoord[3] = arrayComponent(texcoordArray, index, 3, 1.0f);
    }
    if (normalArray.enabled)
    {
        currentNormal[0] = arrayComponent(normalArray, index, 0, 0.0f);
        currentNormal[1] = arrayComponent(normalArray, index, 1, 0.0f);
        currentNormal[2] = arrayComponent(normalArray, index, 2, 1.0f);
    }
    glVertex4f(
        arrayComponent(vertexArray, index, 0, 0.0f),
        arrayComponent(vertexArray, index, 1, 0.0f),
        arrayComponent(vertexArray, index, 2, 0.0f),
        arrayComponent(vertexArray, index, 3, 1.0f));
}

void resetMatrixStack(C3D_MtxStack* stack)
{
    if (stack == &projectionStack)
    {
        Mtx_OrthoTilt(
            MtxStack_Cur(stack),
            -1.0f, 1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, false);
    }
    else
    {
        Mtx_Identity(MtxStack_Cur(stack));
    }
    stack->isDirty = true;
}

void multiplyCurrent(const C3D_Mtx& right)
{
    C3D_Mtx result;
    Mtx_Multiply(&result, MtxStack_Cur(currentMatrixStack), &right);
    Mtx_Copy(MtxStack_Cur(currentMatrixStack), &result);
    currentMatrixStack->isDirty = true;
}

void convertOpenGlMatrix(C3D_Mtx& output, const GLfloat* input)
{
    float* out = output.m;
    out[0] = input[12]; out[1] = input[8]; out[2] = input[4]; out[3] = input[0];
    out[4] = input[13]; out[5] = input[9]; out[6] = input[5]; out[7] = input[1];
    out[8] = input[14]; out[9] = input[10]; out[10] = input[6]; out[11] = input[2];
    out[12] = input[15]; out[13] = input[11]; out[14] = input[7]; out[15] = input[3];
}
}

extern "C"
{

// SDL's 3DS video driver calls C3D_Init with citro3d's default command buffer,
// which Armagetron overruns: text is drawn a glyph at a time, so a menu like
// the 144 entry server browser emits far more batches per frame than the
// default holds, and libctru panics inside GPUCMD_AddInternal. The link wraps
// C3D_Init so the buffer is sized for this client without SDL knowing.
bool __real_C3D_Init(size_t commandBufferWords);

bool __wrap_C3D_Init(size_t commandBufferWords)
{
    constexpr size_t kMinimumCommandBufferWords = 0xC0000;
    if (commandBufferWords < kMinimumCommandBufferWords)
        commandBufferWords = kMinimumCommandBufferWords;
    return __real_C3D_Init(commandBufferWords);
}

void gl_wrapper_init()
{
    if (initialized)
        return;

    vertexBuffer =
        static_cast<Vertex*>(linearAlloc(kVertexCapacity * sizeof(Vertex)));
    if (!vertexBuffer)
        return;
    vertexBufferUsed = 0;
    triangleScratch.reserve(4096);

    topLeft = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    topRight = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!topLeft || !topRight || !bottom)
    {
        gl_wrapper_cleanup();
        return;
    }

    C3D_RenderTargetSetOutput(topLeft, GFX_TOP, GFX_LEFT, TransferFlags);
    C3D_RenderTargetSetOutput(topRight, GFX_TOP, GFX_RIGHT, TransferFlags);
    C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT, TransferFlags);

    shaderBinary = DVLB_ParseFile(
        const_cast<u32*>(reinterpret_cast<const u32*>(aa_gl_vshader_shbin)),
        aa_gl_vshader_shbin_size);
    if (!shaderBinary)
    {
        gl_wrapper_cleanup();
        return;
    }

    shaderProgramInit(&shaderProgram);
    shaderProgramSetVsh(&shaderProgram, &shaderBinary->DVLE[0]);
    C3D_BindProgram(&shaderProgram);

    C3D_AttrInfo* attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 4);

    modelViewUniform =
        shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "mv_mtx");
    projectionUniform =
        shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "p_mtx");
    textureUniform =
        shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "t_mtx");

    MtxStack_Init(&modelViewStack);
    MtxStack_Init(&projectionStack);
    MtxStack_Init(&textureStack);
    MtxStack_Bind(&modelViewStack, GPU_VERTEX_SHADER, modelViewUniform, 4);
    MtxStack_Bind(&projectionStack, GPU_VERTEX_SHADER, projectionUniform, 4);
    MtxStack_Bind(&textureStack, GPU_VERTEX_SHADER, textureUniform, 4);
    resetMatrixStack(&modelViewStack);
    resetMatrixStack(&projectionStack);
    resetMatrixStack(&textureStack);

    initialized = true;
    frameActive = C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    vertexBufferUsed = 0;
    vertexBufferFlushed = 0;
    gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
}

void gl_wrapper_cleanup()
{
    if (frameActive)
    {
        flushVertexCache();
        C3D_FrameEnd(0);
        frameActive = false;
    }
    for (auto& entry : textures)
        if (entry.second.initialized)
            C3D_TexDelete(&entry.second.texture);
    textures.clear();
    if (shaderBinary)
    {
        shaderProgramFree(&shaderProgram);
        DVLB_Free(shaderBinary);
        shaderBinary = nullptr;
    }
    if (topLeft) C3D_RenderTargetDelete(topLeft);
    if (topRight) C3D_RenderTargetDelete(topRight);
    if (bottom) C3D_RenderTargetDelete(bottom);
    topLeft = nullptr;
    topRight = nullptr;
    bottom = nullptr;
    currentTarget = nullptr;
    if (vertexBuffer)
    {
        linearFree(vertexBuffer);
        vertexBuffer = nullptr;
    }
    vertexBufferUsed = 0;
    initialized = false;
}

void gl_wrapper_shutdown_platform()
{
    // Guarded because tearing the graphics stack down twice faults, and there
    // is more than one path out of the client.
    static bool alreadyShutDown = false;
    if (alreadyShutDown)
        return;
    alreadyShutDown = true;

    gl_wrapper_cleanup();
    C3D_Fini();
    gfxExit();
}

int gl_wrapper_is_initialized()
{
    return initialized ? 1 : 0;
}

void gl_wrapper_perspective(float fieldOfViewY, float aspect, float nearDistance)
{
    C3D_Mtx matrix;
    Mtx_Persp(
        &matrix,
        fieldOfViewY * static_cast<float>(M_PI) / 180.0f,
        aspect, nearDistance, 1000.0f, false);
    multiplyCurrent(matrix);
}

void gl_wrapper_select_screen(gfx3dSide_t side)
{
    gl_wrapper_select_target(GFX_TOP, side);
}

void gl_wrapper_select_target(gfxScreen_t screen, gfx3dSide_t side)
{
    currentScreen = screen;
    currentSide = side;
    if (screen == GFX_BOTTOM)
        currentTarget = bottom;
    else
        currentTarget = side == GFX_RIGHT ? topRight : topLeft;
    if (frameActive && currentTarget)
        C3D_FrameDrawOn(currentTarget);
}

// The software keyboard, and any other system applet, takes over the GPU while
// it runs. Handing it over with a Citro3D frame still open leaves the frame
// unfinished and the graphics state invalid on the way back, so the frame is
// closed first and a fresh one started afterwards.
void gl_wrapper_suspend_for_applet(void)
{
    if (!initialized)
        return;
    if (primitiveOpen)
        glEnd();
    if (frameActive)
    {
        flushVertexCache();
        C3D_FrameEnd(0);
        frameActive = false;
    }
}

void gl_wrapper_resume_after_applet(void)
{
    if (!initialized)
        return;
    frameActive = C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    vertexBufferUsed = 0;
    vertexBufferFlushed = 0;
    gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
}

void gl_wrapper_set_stereo_eye(float eyeOffset, float focalLength)
{
    stereoEyeOffset = eyeOffset;
    if (focalLength > 0.0f)
        stereoFocalLength = focalLength;
}

void gl_wrapper_set_stereo_output(int enabled)
{
    stereoOutputWanted = enabled != 0;
}

int gl_wrapper_stereo_active(void)
{
    return stereoOutputEnabled ? 1 : 0;
}

int gl_wrapper_stereo_available(void)
{
    // A 2DS reports a slider that never leaves zero, so asking the slider is
    // enough to know whether there is any point offering this.
    return topRight != nullptr ? 1 : 0;
}

float gl_wrapper_slider_state(void)
{
    float slider = osGet3DSliderState();
    if (slider < 0.0f)
        slider = 0.0f;
    if (slider > 1.0f)
        slider = 1.0f;
    return slider;
}

void gl_wrapper_begin_right_eye(void)
{
    if (!initialized || !topRight)
        return;

    // The left eye's geometry has to reach its own framebuffer before the
    // target changes underneath it.
    if (primitiveOpen)
        glEnd();
    flushVertexCache();

    gl_wrapper_select_target(GFX_TOP, GFX_RIGHT);

    // This framebuffer still holds the previous frame's right eye.
    C3D_RenderTargetClear(
        topRight, C3D_CLEAR_ALL, 0x000000ff, 0x00ffffff);
}

void gl_wrapper_end_right_eye(void)
{
    if (!initialized)
        return;
    if (primitiveOpen)
        glEnd();
    flushVertexCache();
    gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
}

void gl_wrapper_set_max_texture_size(int size)
{
    if (size < kMinimumTextureSize)
        size = kMinimumTextureSize;
    if (size > 1024)
        size = 1024;
    int rounded = kMinimumTextureSize;
    while (rounded * 2 <= size)
        rounded *= 2;
    maxTextureSize = rounded;
}

void gl_wrapper_set_line_width(float pixels)
{
    if (pixels < 1.0f)
        pixels = 1.0f;
    if (pixels > 6.0f)
        pixels = 6.0f;
    lineHalfWidthPixels = pixels * 0.5f;
}

void gl_wrapper_swap_buffers()
{
    if (!initialized)
        return;

    if (aa3ds_trace_enabled())
    {
        static unsigned frames = 0;
        static u32 previousKeys = 0;
        if ((frames++ % 300) == 0)
        {
            aa3ds_log_memory("frame");
            aa3ds_log(
                "renderer: command buffer %d%% used, %u mid-frame submits",
                (int)(C3D_GetCmdBufUsage() * 100.0f),
                commandBudgetSplits);
            aa3ds_log(
                "hid: held=0x%08lx apt=%d polls=%u",
                (unsigned long)hidKeysHeld(),
                aptIsActive() ? 1 : 0,
                aa3ds_poll_calls);
        }
        u32 keys = hidKeysHeld();
        if (keys != previousKeys)
        {
            previousKeys = keys;
            aa3ds_log("hid: held=0x%08lx", (unsigned long)keys);
        }
    }

    if (primitiveOpen)
        glEnd();
    if (frameActive)
    {
        flushVertexCache();
        C3D_FrameEnd(0);
        frameActive = false;
    }

    // Between frames is the only safe moment to let the system suspend us or
    // send us to the HOME menu, for the same reason the software keyboard is
    // bracketed: handing the GPU over with a Citro3D frame open leaves the
    // graphics state invalid on the way back.
    aa3ds_apt_frame();

    // Same reasoning for the display mode: reconfiguring the top screen is
    // not something to do with a frame open.
    if (stereoOutputWanted != stereoOutputEnabled)
    {
        stereoOutputEnabled = stereoOutputWanted;
        gfxSet3D(stereoOutputEnabled);
    }

    frameActive = C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    vertexBufferUsed = 0;
    vertexBufferFlushed = 0;
    gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
}

void glEnable(GLenum capability)
{
    switch (capability)
    {
    case GL_BLEND: blendEnabled = true; break;
    case GL_DEPTH_TEST: depthEnabled = true; break;
    case GL_CULL_FACE: cullEnabled = true; break;
    case GL_ALPHA_TEST: alphaEnabled = true; break;
    case GL_SCISSOR_TEST: scissorEnabled = true; break;
    case GL_TEXTURE_2D: textureEnabled = true; break;
    case GL_LIGHTING: lightingEnabled = true; break;
    case GL_POLYGON_OFFSET_FILL:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
        polygonOffsetEnabled = true;
        break;
    default: break;
    }
}

void glDisable(GLenum capability)
{
    switch (capability)
    {
    case GL_BLEND: blendEnabled = false; break;
    case GL_DEPTH_TEST: depthEnabled = false; break;
    case GL_CULL_FACE: cullEnabled = false; break;
    case GL_ALPHA_TEST: alphaEnabled = false; break;
    case GL_SCISSOR_TEST: scissorEnabled = false; break;
    case GL_TEXTURE_2D: textureEnabled = false; break;
    case GL_LIGHTING: lightingEnabled = false; break;
    case GL_POLYGON_OFFSET_FILL:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
        polygonOffsetEnabled = false;
        break;
    default: break;
    }
}

void glCullFace(GLenum mode)
{
    cullMode = mode;
}

void glBlendFunc(GLenum source, GLenum destination)
{
    blendSource = source;
    blendDestination = destination;
}

void glDepthFunc(GLenum function)
{
    depthFunction = function;
}

void glDepthMask(GLboolean enabled)
{
    depthWrite = enabled != GL_FALSE;
}

void glAlphaFunc(GLenum function, GLclampf reference)
{
    alphaFunction = function;
    alphaReference = reference;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    clearColor[0] = red;
    clearColor[1] = green;
    clearColor[2] = blue;
    clearColor[3] = alpha;
}

void glClearDepth(GLclampd depth)
{
    clearDepth = depth;
}

void glClear(GLbitfield mask)
{
    if (!currentTarget)
        return;
    C3D_ClearBits bits = static_cast<C3D_ClearBits>(0);
    if (mask & GL_COLOR_BUFFER_BIT)
        bits = static_cast<C3D_ClearBits>(bits | C3D_CLEAR_COLOR);
    if (mask & GL_DEPTH_BUFFER_BIT)
        bits = static_cast<C3D_ClearBits>(bits | C3D_CLEAR_DEPTH);
    u32 red = static_cast<u32>(std::clamp(clearColor[0], 0.0f, 1.0f) * 255.0f);
    u32 green = static_cast<u32>(std::clamp(clearColor[1], 0.0f, 1.0f) * 255.0f);
    u32 blue = static_cast<u32>(std::clamp(clearColor[2], 0.0f, 1.0f) * 255.0f);
    u32 alpha = static_cast<u32>(std::clamp(clearColor[3], 0.0f, 1.0f) * 255.0f);
    u32 color = (red << 24) | (green << 16) | (blue << 8) | alpha;
    u32 depth = static_cast<u32>(std::clamp(clearDepth, 0.0, 1.0) * 0xFFFFFF);
    C3D_RenderTargetClear(currentTarget, bits, color, depth);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    viewportX = y;
    viewportY = x;
    viewportWidth = height;
    viewportHeight = width;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    scissorX = y;
    scissorY = x;
    scissorWidth = height;
    scissorHeight = width;
}

void glBegin(GLenum mode)
{
    if (primitiveOpen)
    {
        setError(GlInvalidOperation);
        return;
    }
    primitiveOpen = true;
    primitiveMode = mode;
    vertices.clear();
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    currentColor[0] = red;
    currentColor[1] = green;
    currentColor[2] = blue;
    currentColor[3] = alpha;
}

void glColor4ubv(const GLubyte* values)
{
    glColor4f(
        values[0] / 255.0f,
        values[1] / 255.0f,
        values[2] / 255.0f,
        values[3] / 255.0f);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    glColor4f(red, green, blue, 1.0f);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    currentTexcoord[0] = s;
    currentTexcoord[1] = t;
    currentTexcoord[2] = 0.0f;
    currentTexcoord[3] = 1.0f;
}

void glTexCoord2fv(const GLfloat* values)
{
    glTexCoord2f(values[0], values[1]);
}

void glVertex2i(GLint x, GLint y)
{
    glVertex4f(static_cast<float>(x), static_cast<float>(y), 0.0f, 1.0f);
}

void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex4f(x, y, 0.0f, 1.0f);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    glVertex4f(x, y, z, 1.0f);
}

void glVertex3fv(const GLfloat* values)
{
    glVertex4f(values[0], values[1], values[2], 1.0f);
}

void glEnd()
{
    if (!primitiveOpen)
    {
        setError(GlInvalidOperation);
        return;
    }
    submitVertices();
    vertices.clear();
    primitiveOpen = false;
}

void glGenTextures(GLsizei count, GLuint* output)
{
    for (int index = 0; index < count; ++index)
    {
        GLuint id = nextTextureId++;
        textures.emplace(id, Texture{});
        output[index] = id;
    }
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (target != GL_TEXTURE_2D)
    {
        setError(GlInvalidEnum);
        return;
    }
    if (texture != 0 && !textures.count(texture))
        textures.emplace(texture, Texture{});
    boundTextureId = texture;
}

void glTexParameteri(GLenum target, GLenum parameter, GLint value)
{
    if (target != GL_TEXTURE_2D)
    {
        setError(GlInvalidEnum);
        return;
    }
    Texture* texture = boundTexture();
    if (!texture)
        return;
    switch (parameter)
    {
    case GL_TEXTURE_WRAP_S: texture->wrapS = convertWrap(value); break;
    case GL_TEXTURE_WRAP_T: texture->wrapT = convertWrap(value); break;
    case GL_TEXTURE_MAG_FILTER: texture->magFilter = convertFilter(value); break;
    case GL_TEXTURE_MIN_FILTER:
        texture->minFilter = convertFilter(value);
        texture->wantsMipmaps = filterWantsMipmaps(value);
        break;
    default: return;
    }

    if (texture->initialized)
        applyTextureFilter(*texture);
}

void glTexImage2D(
    GLenum target, GLint level, GLint,
    GLsizei width, GLsizei height, GLint,
    GLenum format, GLenum type, const GLvoid* pixels)
{
    if (target == GL_PROXY_TEXTURE_2D)
    {
        // Anything at or below the limit is usable, because glTexImage2D
        // rescales whatever it is handed to a size the hardware takes. Sizes
        // above it are refused so that Armagetron's own halving loop reduces
        // the image before it reaches us, which is cheaper than doing it here.
        const bool supported =
            type == GL_UNSIGNED_BYTE &&
            width >= 1 && height >= 1 &&
            width <= maxTextureSize && height <= maxTextureSize;
        proxyTextureWidth = supported ? width : 0;
        proxyTextureHeight = supported ? height : 0;
        return;
    }
    if (target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE)
    {
        setError(GlInvalidValue);
        return;
    }
    if (level != 0)
        return;
    Texture* texture = boundTexture();
    int components = sourceComponents(format);
    if (!texture || components == 0 || width <= 0 || height <= 0)
        return;

    texture->width = width;
    texture->height = height;
    texture->isGlyphAtlas = format == GL_ALPHA;
    texture->rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    if (pixels)
    {
        const auto* source = static_cast<const std::uint8_t*>(pixels);
        int sourceWidth = unpackRowLength > 0 ? unpackRowLength : width;
        int rowBytes = sourceWidth * components;
        int sourceStride =
            ((rowBytes + unpackAlignment - 1) / unpackAlignment) * unpackAlignment;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                convertPixel(
                    source + y * sourceStride + x * components,
                    format,
                    texture->rgba.data() +
                        (static_cast<std::size_t>(y) * width + x) * 4);
    }

    // Fit the image to something the hardware will hold. Without this a
    // texture the PICA200 cannot take simply failed to upload and drew as
    // untextured white, which is what a real moviepack looks like: the one
    // this was tested against ships 3072x256 rim walls and 1x128 floor
    // strips, and even the stock game has a 48x48 icon.
    //
    // Glyph atlases are left exactly as they are. FreeType writes into them a
    // glyph at a time at coordinates it worked out itself, so their size
    // cannot be changed underneath it.
    if (!texture->isGlyphAtlas)
    {
        const int fittedWidth = fitTextureDimension(width);
        const int fittedHeight = fitTextureDimension(height);
        if (fittedWidth != width || fittedHeight != height)
        {
            std::vector<std::uint8_t> fitted;
            resampleRgba(
                texture->rgba, width, height, fitted, fittedWidth, fittedHeight);
            texture->rgba.swap(fitted);
            texture->width = fittedWidth;
            texture->height = fittedHeight;
        }
    }

    uploadTexture(*texture);
}

void glCopyTexSubImage2D(
    GLenum, GLint, GLint, GLint,
    GLint, GLint, GLsizei, GLsizei)
{
    setError(GlInvalidOperation);
}

void glDeleteTextures(GLsizei count, const GLuint* ids)
{
    for (int index = 0; index < count; ++index)
    {
        auto found = textures.find(ids[index]);
        if (found == textures.end())
            continue;
        if (found->second.initialized)
            C3D_TexDelete(&found->second.texture);
        textures.erase(found);
        if (boundTextureId == ids[index])
            boundTextureId = 0;
    }
}

void glFogi(GLenum, GLint)
{
}

void glFogf(GLenum, GLfloat)
{
}

void glFogfv(GLenum, const GLfloat*)
{
}

void glTexEnvi(GLenum target, GLenum parameter, GLint value)
{
    if (target == GL_TEXTURE_ENV && parameter == GL_TEXTURE_ENV_MODE)
        textureEnvironment = value;
}

void glGetIntegerv(GLenum parameter, GLint* values)
{
    switch (parameter)
    {
    case GL_BLEND_SRC: values[0] = blendSource; break;
    case GL_BLEND_DST: values[0] = blendDestination; break;
    case GL_MAX_TEXTURE_SIZE: values[0] = maxTextureSize; break;
    case GL_MAX_CLIP_PLANES: values[0] = 0; break;
    default: values[0] = 0; break;
    }
}

void glGetFloatv(GLenum parameter, GLfloat* values)
{
    switch (parameter)
    {
    case GL_CURRENT_COLOR:
        std::copy(currentColor, currentColor + 4, values);
        break;
    case GL_MODELVIEW_MATRIX:
        std::copy(
            MtxStack_Cur(&modelViewStack)->m,
            MtxStack_Cur(&modelViewStack)->m + 16,
            values);
        break;
    case GL_PROJECTION_MATRIX:
        std::copy(
            MtxStack_Cur(&projectionStack)->m,
            MtxStack_Cur(&projectionStack)->m + 16,
            values);
        break;
    default:
        values[0] = 0.0f;
        break;
    }
}

void glGetTexLevelParameteriv(
    GLenum target, GLint level, GLenum parameter, GLint* values)
{
    if (target == GL_PROXY_TEXTURE_2D)
    {
        if (level != 0)
            values[0] = 0;
        else if (parameter == GL_TEXTURE_WIDTH)
            values[0] = proxyTextureWidth;
        else if (parameter == GL_TEXTURE_HEIGHT)
            values[0] = proxyTextureHeight;
        else
            values[0] = 0;
        return;
    }
    Texture* texture = boundTexture();
    if (!texture || level != 0)
    {
        values[0] = 0;
        return;
    }
    if (parameter == GL_TEXTURE_WIDTH)
        values[0] = texture->width;
    else if (parameter == GL_TEXTURE_HEIGHT)
        values[0] = texture->height;
    else
        values[0] = 0;
}

void glGetTexImage(GLenum, GLint level, GLenum format, GLenum type, GLvoid* pixels)
{
    Texture* texture = boundTexture();
    if (!texture || level != 0 || format != GL_RGBA || type != GL_UNSIGNED_BYTE)
    {
        setError(GlInvalidOperation);
        return;
    }
    std::memcpy(pixels, texture->rgba.data(), texture->rgba.size());
}

void glFlush()
{
}

void glFinish()
{
    C3D_FrameSync();
}

void glMatrixMode(GLenum mode)
{
    switch (mode)
    {
    case GL_MODELVIEW: currentMatrixStack = &modelViewStack; break;
    case GL_PROJECTION: currentMatrixStack = &projectionStack; break;
    case GL_TEXTURE: currentMatrixStack = &textureStack; break;
    default: setError(GlInvalidEnum); break;
    }
}

void glLoadIdentity()
{
    resetMatrixStack(currentMatrixStack);
}

void glLoadMatrixf(const GLfloat* matrix)
{
    C3D_Mtx converted;
    convertOpenGlMatrix(converted, matrix);
    if (currentMatrixStack == &projectionStack)
    {
        resetMatrixStack(currentMatrixStack);
        multiplyCurrent(converted);
    }
    else
    {
        Mtx_Copy(MtxStack_Cur(currentMatrixStack), &converted);
        currentMatrixStack->isDirty = true;
    }
}

void glPushMatrix()
{
    MtxStack_Push(currentMatrixStack);
}

void glPopMatrix()
{
    MtxStack_Pop(currentMatrixStack);
}

void glOrtho(
    GLdouble left, GLdouble right,
    GLdouble bottomValue, GLdouble topValue,
    GLdouble nearDistance, GLdouble farDistance)
{
    C3D_Mtx matrix;
    Mtx_Ortho(
        &matrix,
        static_cast<float>(left), static_cast<float>(right),
        static_cast<float>(bottomValue), static_cast<float>(topValue),
        static_cast<float>(farDistance), static_cast<float>(nearDistance),
        false);
    multiplyCurrent(matrix);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    Mtx_Translate(MtxStack_Cur(currentMatrixStack), x, y, z, true);
    currentMatrixStack->isDirty = true;
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float radians = angle * static_cast<float>(M_PI) / 180.0f;
    if (x != 0.0f && y == 0.0f && z == 0.0f)
        Mtx_RotateX(MtxStack_Cur(currentMatrixStack), radians, true);
    else if (y != 0.0f && x == 0.0f && z == 0.0f)
        Mtx_RotateY(MtxStack_Cur(currentMatrixStack), radians, true);
    else if (z != 0.0f && x == 0.0f && y == 0.0f)
        Mtx_RotateZ(MtxStack_Cur(currentMatrixStack), radians, true);
    currentMatrixStack->isDirty = true;
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    Mtx_Scale(MtxStack_Cur(currentMatrixStack), x, y, z);
    currentMatrixStack->isDirty = true;
}

void glClipPlane(GLenum, const GLdouble*)
{
}

void glColor3fv(const GLfloat* values)
{
    glColor3f(values[0], values[1], values[2]);
}

void glColor4fv(const GLfloat* values)
{
    glColor4f(values[0], values[1], values[2], values[3]);
}

void glColorMask(
    GLboolean red, GLboolean green,
    GLboolean blue, GLboolean alpha)
{
    colorWrite[0] = red != GL_FALSE;
    colorWrite[1] = green != GL_FALSE;
    colorWrite[2] = blue != GL_FALSE;
    colorWrite[3] = alpha != GL_FALSE;
}

void glColorPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer)
{
    colorArray = {size, type, stride, static_cast<const std::uint8_t*>(pointer), colorArray.enabled};
}

void glDisableClientState(GLenum array)
{
    switch (array)
    {
    case GL_VERTEX_ARRAY: vertexArray.enabled = false; break;
    case GL_COLOR_ARRAY: colorArray.enabled = false; break;
    case GL_TEXTURE_COORD_ARRAY: texcoordArray.enabled = false; break;
    case GL_NORMAL_ARRAY: normalArray.enabled = false; break;
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (!vertexArray.enabled)
    {
        setError(GlInvalidOperation);
        return;
    }
    glBegin(mode);
    for (int index = 0; index < count; ++index)
        emitArrayVertex(static_cast<unsigned>(first + index));
    glEnd();
}

void glDrawBuffer(GLenum)
{
}

void glDrawElements(
    GLenum mode, GLsizei count,
    GLenum type, const GLvoid* indices)
{
    if (!vertexArray.enabled || type != GL_UNSIGNED_INT)
    {
        setError(GlInvalidOperation);
        return;
    }
    const auto* indexData = static_cast<const GLuint*>(indices);
    glBegin(mode);
    for (int index = 0; index < count; ++index)
        emitArrayVertex(indexData[index]);
    glEnd();
}

void glEnableClientState(GLenum array)
{
    switch (array)
    {
    case GL_VERTEX_ARRAY: vertexArray.enabled = true; break;
    case GL_COLOR_ARRAY: colorArray.enabled = true; break;
    case GL_TEXTURE_COORD_ARRAY: texcoordArray.enabled = true; break;
    case GL_NORMAL_ARRAY: normalArray.enabled = true; break;
    }
}

void glFrontFace(GLenum mode)
{
    frontFace = mode;
}

void glFrustum(
    GLdouble left, GLdouble right,
    GLdouble bottomValue, GLdouble topValue,
    GLdouble nearDistance, GLdouble farDistance)
{
    float height = static_cast<float>(topValue - bottomValue);
    float width = static_cast<float>(right - left);
    if (nearDistance <= 0.0 || farDistance <= nearDistance || height == 0.0f)
    {
        setError(GlInvalidValue);
        return;
    }
    float fieldOfView =
        2.0f * std::atan(height / (2.0f * static_cast<float>(nearDistance)));
    C3D_Mtx matrix;
    if (stereoEyeOffset != 0.0f)
    {
        // The off axis construction: the frustum slides sideways and the eye
        // follows it, which keeps both eyes looking straight ahead. Toeing
        // them in instead would converge the images but leave a vertical
        // disparity towards the edges that is unpleasant to look at.
        //
        // The projection stack is based on a tilt matrix for the rotated
        // panel, and tilt composed with this is what Mtx_PerspStereoTilt
        // builds in one step, so the parallax comes out along the screen's
        // physical horizontal.
        Mtx_PerspStereo(
            &matrix, fieldOfView, width / height,
            static_cast<float>(nearDistance),
            static_cast<float>(farDistance),
            stereoEyeOffset, stereoFocalLength, false);
    }
    else
    {
        Mtx_Persp(
            &matrix, fieldOfView, width / height,
            static_cast<float>(nearDistance),
            static_cast<float>(farDistance), false);
    }
    multiplyCurrent(matrix);
}

GLenum glGetError()
{
    GLenum result = glErrorState;
    glErrorState = GL_NO_ERROR;
    return result;
}

const GLubyte* glGetString(GLenum name)
{
    static const GLubyte vendor[] = "devkitPro/Citro3D";
    static const GLubyte renderer[] = "Nintendo 3DS PICA200";
    static const GLubyte version[] = "Armagetron GL 1.x compatibility";
    static const GLubyte extensions[] = "";
    switch (name)
    {
    case GL_VENDOR: return vendor;
    case GL_RENDERER: return renderer;
    case GL_VERSION: return version;
    case GL_EXTENSIONS: return extensions;
    default: return nullptr;
    }
}

void glHint(GLenum, GLenum)
{
}

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid* pointer)
{
    if (format != GL_T2F_V3F)
    {
        setError(GlInvalidEnum);
        return;
    }
    int actualStride = stride ? stride : 5 * static_cast<int>(sizeof(float));
    auto* bytes = static_cast<const std::uint8_t*>(pointer);
    texcoordArray = {2, GL_FLOAT, actualStride, bytes, true};
    vertexArray = {
        3, GL_FLOAT, actualStride,
        bytes + 2 * sizeof(float), true};
}

GLboolean glIsEnabled(GLenum capability)
{
    switch (capability)
    {
    case GL_BLEND: return blendEnabled;
    case GL_DEPTH_TEST: return depthEnabled;
    case GL_CULL_FACE: return cullEnabled;
    case GL_ALPHA_TEST: return alphaEnabled;
    case GL_SCISSOR_TEST: return scissorEnabled;
    case GL_TEXTURE_2D: return textureEnabled;
    case GL_LIGHTING: return lightingEnabled;
    default: return GL_FALSE;
    }
}

void glLightfv(GLenum, GLenum, const GLfloat*)
{
}

void glMaterialfv(GLenum, GLenum parameter, const GLfloat* values)
{
    if (parameter == GL_DIFFUSE)
        glColor4fv(values);
}

void glMultMatrixf(const GLfloat* matrix)
{
    C3D_Mtx converted;
    convertOpenGlMatrix(converted, matrix);
    multiplyCurrent(converted);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    currentNormal[0] = x;
    currentNormal[1] = y;
    currentNormal[2] = z;
}

void glNormal3fv(const GLfloat* values)
{
    glNormal3f(values[0], values[1], values[2]);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* pointer)
{
    normalArray = {3, type, stride, static_cast<const std::uint8_t*>(pointer), normalArray.enabled};
}

void glPolygonOffset(GLfloat, GLfloat units)
{
    polygonOffsetUnits = units;
}

void glPopAttrib()
{
    if (attribStack.empty())
        return;
    const AttribState state = attribStack.back();
    attribStack.pop_back();
    blendEnabled = state.blend;
    depthEnabled = state.depth;
    cullEnabled = state.cull;
    alphaEnabled = state.alpha;
    scissorEnabled = state.scissor;
    textureEnabled = state.texture;
    blendSource = state.blendSource;
    blendDestination = state.blendDestination;
}

void glPopClientAttrib()
{
    if (clientAttribStack.empty())
        return;
    unpackAlignment = clientAttribStack.back().first;
    unpackRowLength = clientAttribStack.back().second;
    clientAttribStack.pop_back();
}

void glPushAttrib(GLbitfield)
{
    attribStack.push_back({
        blendEnabled, depthEnabled, cullEnabled,
        alphaEnabled, scissorEnabled, textureEnabled,
        blendSource, blendDestination});
}

void glPushClientAttrib(GLbitfield)
{
    clientAttribStack.emplace_back(unpackAlignment, unpackRowLength);
}

void glPixelStorei(GLenum parameter, GLint value)
{
    if (parameter == GL_UNPACK_ALIGNMENT)
        unpackAlignment = std::max(1, value);
    else if (parameter == GL_UNPACK_ROW_LENGTH)
        unpackRowLength = std::max(0, value);
}

void glRasterPos2f(GLfloat, GLfloat)
{
}

void glReadPixels(
    GLint x, GLint y, GLsizei width, GLsizei height,
    GLenum format, GLenum type, GLvoid* pixels)
{
    if (!pixels || width <= 0 || height <= 0)
        return;

    const int components = format == GL_RGBA ? 4 : 3;
    std::uint8_t* out = static_cast<std::uint8_t*>(pixels);
    std::memset(out, 0, static_cast<std::size_t>(width) * height * components);

    if (!initialized || !topLeft || type != GL_UNSIGNED_BYTE ||
        (format != GL_RGB && format != GL_RGBA))
    {
        setError(GlInvalidOperation);
        return;
    }

    if (primitiveOpen)
        glEnd();

    // The render target lives in VRAM in the PICA's tiled layout. Rather than
    // untile it by hand, end the frame so the GPU is done with it and then run
    // the same display transfer the screen uses, into a linear scratch buffer.
    if (frameActive)
    {
        flushVertexCache();
        C3D_FrameEnd(0);
        frameActive = false;
    }

    // Whichever eye is selected, so a stereo pair can be read back one side at
    // a time rather than the left one twice.
    const gfx3dSide_t readSide =
        (currentScreen == GFX_TOP && currentSide == GFX_RIGHT && topRight)
            ? GFX_RIGHT
            : GFX_LEFT;
    C3D_RenderTarget* const readTarget =
        readSide == GFX_RIGHT ? topRight : topLeft;

    const int targetWidth = 240;
    const int targetHeight = 400;
    const std::size_t scratchSize =
        static_cast<std::size_t>(targetWidth) * targetHeight * 3;
    u8* scratch = static_cast<u8*>(linearAlloc(scratchSize));
    if (!scratch)
    {
        frameActive = C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    vertexBufferUsed = 0;
    vertexBufferFlushed = 0;
        gl_wrapper_select_target(GFX_TOP, GFX_LEFT);
        setError(GlOutOfMemory);
        return;
    }

    std::memset(scratch, 0, scratchSize);
    GSPGPU_FlushDataCache(scratch, scratchSize);
    C3D_SyncDisplayTransfer(
        static_cast<u32*>(readTarget->frameBuf.colorBuf),
        GX_BUFFER_DIM(targetWidth, targetHeight),
        reinterpret_cast<u32*>(scratch),
        GX_BUFFER_DIM(targetWidth, targetHeight),
        TransferFlags);
    GSPGPU_InvalidateDataCache(scratch, scratchSize);

    auto countNonZero = [](const u8* data, std::size_t size)
    {
        std::size_t nonZero = 0;
        for (std::size_t index = 0; index < size; ++index)
            if (data[index])
                ++nonZero;
        return nonZero;
    };

    // Citra does not always write an accelerated render target back to guest
    // VRAM, so the direct transfer can come back empty under emulation even
    // though it is correct on hardware. Repeating the frame fills the second
    // display buffer with the same image, and that buffer always reads back.
    const u8* source = scratch;
    std::size_t transferred = countNonZero(scratch, scratchSize);
    if (transferred == 0)
    {
        if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
        {
            C3D_FrameDrawOn(readTarget);
            flushVertexCache();
            C3D_FrameEnd(0);
        }
        const u8* displayed = gfxGetFramebuffer(GFX_TOP, readSide, nullptr, nullptr);
        if (displayed)
            source = displayed;
    }

    if (aa3ds_trace_enabled())
        aa3ds_log(
            "readback: %s color=%p %dx%d transfer=%u display=%u",
            readSide == GFX_RIGHT ? "right" : "left",
            readTarget->frameBuf.colorBuf,
            static_cast<int>(readTarget->frameBuf.width),
            static_cast<int>(readTarget->frameBuf.height),
            static_cast<unsigned>(transferred),
            static_cast<unsigned>(
                source == scratch ? 0u : countNonZero(source, scratchSize)));

    frameActive = C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    vertexBufferUsed = 0;
    vertexBufferFlushed = 0;
    gl_wrapper_select_target(GFX_TOP, readSide);

    // The transferred image keeps the screen's rotated layout: 400 columns of
    // 240 pixels, three bytes per pixel in BGR order, with column 0 on the left
    // and index 0 of each column at the bottom of the screen. That bottom-up
    // ordering is what OpenGL asks for, so rows map across directly.
    const int screenWidth = 400;
    const int screenHeight = 240;
    for (int row = 0; row < height; ++row)
    {
        const int screenRow = y + row;
        if (screenRow < 0 || screenRow >= screenHeight)
            continue;

        std::uint8_t* target =
            out + static_cast<std::size_t>(row) * width * components;
        for (int column = 0; column < width; ++column)
        {
            const int screenColumn = x + column;
            if (screenColumn < 0 || screenColumn >= screenWidth)
                continue;

            const u8* pixel =
                source + 3 * (screenRow + screenColumn * screenHeight);
            target[column * components + 0] = pixel[2];
            target[column * components + 1] = pixel[1];
            target[column * components + 2] = pixel[0];
            if (components == 4)
                target[column * components + 3] = 255;
        }
    }

    linearFree(scratch);
}

void glRectf(GLfloat left, GLfloat bottomValue, GLfloat right, GLfloat topValue)
{
    glBegin(GL_QUADS);
    glVertex2f(left, bottomValue);
    glVertex2f(right, bottomValue);
    glVertex2f(right, topValue);
    glVertex2f(left, topValue);
    glEnd();
}

void glShadeModel(GLenum)
{
}

void glTexCoord2d(GLdouble s, GLdouble t)
{
    glTexCoord2f(static_cast<float>(s), static_cast<float>(t));
}

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    currentTexcoord[0] = s;
    currentTexcoord[1] = t;
    currentTexcoord[2] = r;
    currentTexcoord[3] = 1.0f;
}

void glTexCoord3fv(const GLfloat* values)
{
    glTexCoord3f(values[0], values[1], values[2]);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    currentTexcoord[0] = s;
    currentTexcoord[1] = t;
    currentTexcoord[2] = r;
    currentTexcoord[3] = q;
}

void glTexCoordPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer)
{
    texcoordArray = {size, type, stride, static_cast<const std::uint8_t*>(pointer), texcoordArray.enabled};
}

void glTexGenfv(GLenum, GLenum, const GLfloat*)
{
}

void glTexGeni(GLenum, GLenum, GLint)
{
}

void glTexSubImage2D(
    GLenum target, GLint level,
    GLint xOffset, GLint yOffset,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type,
    const GLvoid* pixels)
{
    Texture* texture = boundTexture();
    int components = sourceComponents(format);
    if (!texture || !pixels || target != GL_TEXTURE_2D ||
        level != 0 || type != GL_UNSIGNED_BYTE || components == 0)
        return;
    const auto* source = static_cast<const std::uint8_t*>(pixels);
    int sourceWidth = unpackRowLength > 0 ? unpackRowLength : width;
    int rowBytes = sourceWidth * components;
    int sourceStride =
        ((rowBytes + unpackAlignment - 1) / unpackAlignment) * unpackAlignment;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            int destinationX = xOffset + x;
            int destinationY = yOffset + y;
            if (destinationX < 0 || destinationY < 0 ||
                destinationX >= texture->width ||
                destinationY >= texture->height)
                continue;
            convertPixel(
                source + y * sourceStride + x * components,
                format,
                texture->rgba.data() +
                    (static_cast<std::size_t>(destinationY) * texture->width +
                     destinationX) * 4);
        }
    refreshTexture(*texture);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (!primitiveOpen)
    {
        setError(GlInvalidOperation);
        return;
    }
    vertices.push_back(makeVertex(x, y, z, w));
}

void glVertexPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer)
{
    vertexArray = {size, type, stride, static_cast<const std::uint8_t*>(pointer), vertexArray.enabled};
}

GLuint glGenLists(GLsizei)
{
    static GLuint nextList = 1;
    return nextList++;
}

void glNewList(GLuint, GLenum)
{
}

void glEndList()
{
}

void glCallList(GLuint)
{
}

void glDeleteLists(GLuint, GLsizei)
{
}

void gluLookAt(
    GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
    GLdouble centerX, GLdouble centerY, GLdouble centerZ,
    GLdouble upX, GLdouble upY, GLdouble upZ)
{
    double forwardX = centerX - eyeX;
    double forwardY = centerY - eyeY;
    double forwardZ = centerZ - eyeZ;
    double forwardLength =
        std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
    if (forwardLength == 0.0)
        return;
    forwardX /= forwardLength;
    forwardY /= forwardLength;
    forwardZ /= forwardLength;

    double sideX = forwardY * upZ - forwardZ * upY;
    double sideY = forwardZ * upX - forwardX * upZ;
    double sideZ = forwardX * upY - forwardY * upX;
    double sideLength = std::sqrt(sideX * sideX + sideY * sideY + sideZ * sideZ);
    if (sideLength == 0.0)
        return;
    sideX /= sideLength;
    sideY /= sideLength;
    sideZ /= sideLength;

    double actualUpX = sideY * forwardZ - sideZ * forwardY;
    double actualUpY = sideZ * forwardX - sideX * forwardZ;
    double actualUpZ = sideX * forwardY - sideY * forwardX;

    GLfloat matrix[16] = {
        static_cast<float>(sideX), static_cast<float>(actualUpX), static_cast<float>(-forwardX), 0.0f,
        static_cast<float>(sideY), static_cast<float>(actualUpY), static_cast<float>(-forwardY), 0.0f,
        static_cast<float>(sideZ), static_cast<float>(actualUpZ), static_cast<float>(-forwardZ), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    glMultMatrixf(matrix);
    glTranslatef(
        static_cast<float>(-eyeX),
        static_cast<float>(-eyeY),
        static_cast<float>(-eyeZ));
}

void gluPerspective(
    GLdouble fieldOfViewY, GLdouble aspect,
    GLdouble nearDistance, GLdouble farDistance)
{
    double topValue =
        nearDistance * std::tan(fieldOfViewY * M_PI / 360.0);
    double right = topValue * aspect;
    glFrustum(
        -right, right, -topValue, topValue,
        nearDistance, farDistance);
}

GLint gluBuild2DMipmaps(
    GLenum target, GLint internalFormat,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type, const void* data)
{
    glTexImage2D(
        target, 0, internalFormat,
        width, height, 0, format, type, data);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

const GLubyte* gluErrorString(GLenum error)
{
    static const GLubyte noError[] = "no error";
    static const GLubyte invalidEnum[] = "invalid enum";
    static const GLubyte invalidValue[] = "invalid value";
    static const GLubyte invalidOperation[] = "invalid operation";
    static const GLubyte outOfMemory[] = "out of memory";
    switch (error)
    {
    case GL_NO_ERROR: return noError;
    case GlInvalidEnum: return invalidEnum;
    case GlInvalidValue: return invalidValue;
    case GlInvalidOperation: return invalidOperation;
    case GlOutOfMemory: return outOfMemory;
    default: return invalidOperation;
    }
}

struct GLUquadric
{
};

GLUquadric* gluNewQuadric()
{
    return new GLUquadric;
}

void gluDeleteQuadric(GLUquadric* quadric)
{
    delete quadric;
}

void gluSphere(GLUquadric*, GLdouble radius, GLint slices, GLint stacks)
{
    for (int stack = 0; stack < stacks; ++stack)
    {
        double latitude0 = M_PI * (-0.5 + static_cast<double>(stack) / stacks);
        double latitude1 = M_PI * (-0.5 + static_cast<double>(stack + 1) / stacks);
        glBegin(GL_QUAD_STRIP);
        for (int slice = 0; slice <= slices; ++slice)
        {
            double longitude = 2.0 * M_PI * static_cast<double>(slice) / slices;
            double cosine = std::cos(longitude);
            double sine = std::sin(longitude);
            glVertex3f(
                static_cast<float>(radius * cosine * std::cos(latitude0)),
                static_cast<float>(radius * sine * std::cos(latitude0)),
                static_cast<float>(radius * std::sin(latitude0)));
            glVertex3f(
                static_cast<float>(radius * cosine * std::cos(latitude1)),
                static_cast<float>(radius * sine * std::cos(latitude1)),
                static_cast<float>(radius * std::sin(latitude1)));
        }
        glEnd();
    }
}
}
