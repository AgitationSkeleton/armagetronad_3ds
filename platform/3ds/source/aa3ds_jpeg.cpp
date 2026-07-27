#include <SDL.h>
#include <jpeglib.h>

#include <csetjmp>
#include <cstdint>
#include <vector>

namespace
{
struct JpegError
{
    jpeg_error_mgr base;
    std::jmp_buf jump;
};

void JpegErrorExit(j_common_ptr common)
{
    JpegError *error = reinterpret_cast<JpegError *>(common->err);
    std::longjmp(error->jump, 1);
}

bool ReadSource(SDL_RWops *source, std::vector<unsigned char> &data)
{
    const Sint64 start = SDL_RWtell(source);
    if (start < 0)
        return false;

    const Sint64 end = SDL_RWseek(source, 0, RW_SEEK_END);
    if (end < start || SDL_RWseek(source, start, RW_SEEK_SET) < 0)
        return false;

    const Uint64 length = static_cast<Uint64>(end - start);
    if (length == 0 || length > 64U * 1024U * 1024U)
        return false;

    data.resize(static_cast<std::size_t>(length));
    return SDL_RWread(source, data.data(), 1, data.size()) ==
           static_cast<int>(data.size());
}
}

extern "C" int IMG_InitJPG()
{
    return 0;
}

extern "C" void IMG_QuitJPG()
{
}

extern "C" int IMG_isJPG(SDL_RWops *source)
{
    if (!source)
        return 0;

    const Sint64 start = SDL_RWtell(source);
    unsigned char magic[3] = {};
    const bool read = SDL_RWread(source, magic, 1, sizeof(magic)) == sizeof(magic);
    if (start >= 0)
        SDL_RWseek(source, start, RW_SEEK_SET);

    return read && magic[0] == 0xff && magic[1] == 0xd8 && magic[2] == 0xff;
}

extern "C" SDL_Surface *IMG_LoadJPG_RW(SDL_RWops *source)
{
    if (!source)
    {
        SDL_SetError("JPEG source is null");
        return nullptr;
    }

    std::vector<unsigned char> data;
    if (!ReadSource(source, data))
    {
        SDL_SetError("Could not read JPEG source");
        return nullptr;
    }

    jpeg_decompress_struct decoder = {};
    JpegError error = {};
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = JpegErrorExit;

    if (setjmp(error.jump))
    {
        jpeg_destroy_decompress(&decoder);
        SDL_SetError("Could not decode JPEG image");
        return nullptr;
    }

    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, data.data(), static_cast<unsigned long>(data.size()));
    jpeg_read_header(&decoder, TRUE);
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);

    if (decoder.output_width == 0 || decoder.output_height == 0 ||
        decoder.output_width > 4096 || decoder.output_height > 4096 ||
        decoder.output_components != 3)
    {
        jpeg_destroy_decompress(&decoder);
        SDL_SetError("Unsupported JPEG image dimensions");
        return nullptr;
    }

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    const Uint32 redMask = 0xff0000;
    const Uint32 greenMask = 0x00ff00;
    const Uint32 blueMask = 0x0000ff;
#else
    const Uint32 redMask = 0x0000ff;
    const Uint32 greenMask = 0x00ff00;
    const Uint32 blueMask = 0xff0000;
#endif

    SDL_Surface *surface = SDL_CreateRGBSurface(
        SDL_SWSURFACE,
        static_cast<int>(decoder.output_width),
        static_cast<int>(decoder.output_height),
        24,
        redMask,
        greenMask,
        blueMask,
        0);
    if (!surface)
    {
        jpeg_destroy_decompress(&decoder);
        return nullptr;
    }

    while (decoder.output_scanline < decoder.output_height)
    {
        JSAMPROW row = static_cast<JSAMPROW>(surface->pixels) +
                       decoder.output_scanline * surface->pitch;
        if (jpeg_read_scanlines(&decoder, &row, 1) != 1)
        {
            SDL_FreeSurface(surface);
            jpeg_destroy_decompress(&decoder);
            SDL_SetError("Could not read JPEG scanline");
            return nullptr;
        }
    }

    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return surface;
}
