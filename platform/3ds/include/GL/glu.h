#ifndef ARMAGETRON_3DS_GLU_H
#define ARMAGETRON_3DS_GLU_H

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GLUquadric GLUquadric;

void gluLookAt(
    GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
    GLdouble centerX, GLdouble centerY, GLdouble centerZ,
    GLdouble upX, GLdouble upY, GLdouble upZ);
void gluPerspective(
    GLdouble fieldOfViewY, GLdouble aspect,
    GLdouble nearDistance, GLdouble farDistance);
GLint gluBuild2DMipmaps(
    GLenum target, GLint internalFormat,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type, const void* data);
const GLubyte* gluErrorString(GLenum error);
GLUquadric* gluNewQuadric(void);
void gluDeleteQuadric(GLUquadric* quadric);
void gluSphere(
    GLUquadric* quadric, GLdouble radius,
    GLint slices, GLint stacks);

#ifdef __cplusplus
}
#endif

#endif
