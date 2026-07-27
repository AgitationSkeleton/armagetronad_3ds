#ifndef ARMAGETRON_3DS_GL_EXTRA_H
#define ARMAGETRON_3DS_GL_EXTRA_H

#include "aa3ds_gl.h"

#define GL_POINTS 0x0000
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009

#define GL_NO_ERROR 0
#define GL_CLIENT_PIXEL_STORE_BIT 0x00000001
#define GL_ENABLE_BIT 0x00002000

#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_LINE_SMOOTH 0x0B20
#define GL_DITHER 0x0BD0
#define GL_LIGHTING 0x0B50
#define GL_LIGHT0 0x4000
#define GL_LIGHT1 0x4001
#define GL_POLYGON_OFFSET_POINT 0x2A01
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_FILL 0x8037

#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078

#define GL_FLOAT 0x1406
#define GL_UNSIGNED_INT 0x1405
#define GL_UNPACK_LSB_FIRST 0x0CF1
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1

#define GL_RGB5 0x8050
#define GL_RGB8 0x8051
#define GL_LUMINANCE8_ALPHA8 0x8045
#define GL_PROXY_TEXTURE_2D 0x8064
#define GL_CLAMP_TO_EDGE 0x812F

#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_CLIP_PLANES 0x0D32
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03

#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01
#define GL_FASTEST 0x1101
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_LINE_SMOOTH_HINT 0x0C52

#define GL_POSITION 0x1203
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202

#define GL_S 0x2000
#define GL_T 0x2001
#define GL_R 0x2002
#define GL_Q 0x2003
#define GL_OBJECT_LINEAR 0x2401
#define GL_OBJECT_PLANE 0x2501
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_R 0x0C62
#define GL_TEXTURE_GEN_Q 0x0C63
#define GL_TEXTURE_GEN_MODE 0x2500

#define GL_CLIP_PLANE0 0x3000
#define GL_CLIP_PLANE1 0x3001
#define GL_CLIP_PLANE2 0x3002
#define GL_CLIP_PLANE3 0x3003

#define GL_T2F_V3F 0x2A27
#define GL_COMPILE 0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301

#ifdef __cplusplus
extern "C" {
#endif

void glClipPlane(GLenum plane, const GLdouble* equation);
void glColor3fv(const GLfloat* values);
void glColor4fv(const GLfloat* values);
void glColorMask(
    GLboolean red, GLboolean green,
    GLboolean blue, GLboolean alpha);
void glColorPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer);
void glDisableClientState(GLenum array);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawBuffer(GLenum mode);
void glDrawElements(
    GLenum mode, GLsizei count,
    GLenum type, const GLvoid* indices);
void glEnableClientState(GLenum array);
void glFrontFace(GLenum mode);
void glFrustum(
    GLdouble left, GLdouble right,
    GLdouble bottom, GLdouble top,
    GLdouble nearDistance, GLdouble farDistance);
GLenum glGetError(void);
const GLubyte* glGetString(GLenum name);
void glHint(GLenum target, GLenum mode);
void glInterleavedArrays(
    GLenum format, GLsizei stride,
    const GLvoid* pointer);
GLboolean glIsEnabled(GLenum capability);
void glLightfv(GLenum light, GLenum parameter, const GLfloat* values);
void glMaterialfv(GLenum face, GLenum parameter, const GLfloat* values);
void glMultMatrixf(const GLfloat* matrix);
void glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void glNormal3fv(const GLfloat* values);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid* pointer);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glPopAttrib(void);
void glPopClientAttrib(void);
void glPushAttrib(GLbitfield mask);
void glPushClientAttrib(GLbitfield mask);
void glPixelStorei(GLenum parameter, GLint value);
void glRasterPos2f(GLfloat x, GLfloat y);
void glReadPixels(
    GLint x, GLint y,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type,
    GLvoid* pixels);
void glRectf(GLfloat left, GLfloat bottom, GLfloat right, GLfloat top);
void glShadeModel(GLenum mode);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord3fv(const GLfloat* values);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glTexCoordPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer);
void glTexGenfv(GLenum coordinate, GLenum parameter, const GLfloat* values);
void glTexGeni(GLenum coordinate, GLenum parameter, GLint value);
void glTexSubImage2D(
    GLenum target, GLint level,
    GLint xOffset, GLint yOffset,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type,
    const GLvoid* pixels);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glVertexPointer(
    GLint size, GLenum type,
    GLsizei stride, const GLvoid* pointer);

GLuint glGenLists(GLsizei range);
void glNewList(GLuint list, GLenum mode);
void glEndList(void);
void glCallList(GLuint list);
void glDeleteLists(GLuint list, GLsizei range);

#ifdef __cplusplus
}
#endif

#endif
