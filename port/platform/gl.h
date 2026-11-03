/*
 * gl.h - the OpenGL functions the port draws with (glpresent.c, glscene.c, glview.c), loaded
 * through SDL once a 3.3 core context is current, and its shaders' build. The shaders keep to the
 * subset GLSL 3.30 and GLSL ES 3.00 share, so that only the version line changes for the web.
 */
#ifndef BAK_GL_H
#define BAK_GL_H

#include <stdbool.h>

#include <SDL3/SDL_opengl.h>

/* GL 1.1's functions too: SDL finds them in opengl32. On 32-bit Windows they are __stdcall
 * (GLAPIENTRY). */
#define BAK_GL_FUNCTIONS(X)                                                                        \
    X(void, Clear, (GLbitfield))                                                                   \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                             \
    X(void, Enable, (GLenum))                                                                      \
    X(void, Disable, (GLenum))                                                                     \
    X(void, DepthFunc, (GLenum))                                                                   \
    X(void, DepthMask, (GLboolean))                                                                \
    X(void, PolygonOffset, (GLfloat, GLfloat))                                                     \
    X(void, GetIntegerv, (GLenum, GLint *))                                                        \
    X(void, GetFloatv, (GLenum, GLfloat *))                                                        \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(void, GenTextures, (GLsizei, GLuint *))                                                      \
    X(void, DeleteTextures, (GLsizei, const GLuint *))                                             \
    X(void, BindTexture, (GLenum, GLuint))                                                         \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat))                                              \
    X(void, GenerateMipmap, (GLenum))                                                              \
    X(void, TexImage2D,                                                                            \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))               \
    X(void, TexSubImage2D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))               \
    X(void, PixelStorei, (GLenum, GLint))                                                          \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                  \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                  \
    X(void, ActiveTexture, (GLenum))                                                               \
    X(GLuint, CreateShader, (GLenum))                                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                 \
    X(void, CompileShader, (GLuint))                                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                              \
    X(void, DeleteShader, (GLuint))                                                                \
    X(GLuint, CreateProgram, (void))                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                        \
    X(void, LinkProgram, (GLuint))                                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                             \
    X(void, UseProgram, (GLuint))                                                                  \
    X(void, DeleteProgram, (GLuint))                                                               \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *))                                         \
    X(void, Uniform1i, (GLint, GLint))                                                             \
    X(void, Uniform1f, (GLint, GLfloat))                                                           \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                  \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                \
    X(void, Uniform2i, (GLint, GLint, GLint))                                                      \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                         \
    X(void, Uniform4i, (GLint, GLint, GLint, GLint, GLint))                                        \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *))                        \
    X(void, GenVertexArrays, (GLsizei, GLuint *))                                                  \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint *))                                         \
    X(void, BindVertexArray, (GLuint))                                                             \
    X(void, GenBuffers, (GLsizei, GLuint *))                                                       \
    X(void, DeleteBuffers, (GLsizei, const GLuint *))                                              \
    X(void, BindBuffer, (GLenum, GLuint))                                                          \
    X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))        \
    X(void, EnableVertexAttribArray, (GLuint))                                                     \
    X(void, GenFramebuffers, (GLsizei, GLuint *))                                                  \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                         \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                     \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                         \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                    \
    X(void, GenRenderbuffers, (GLsizei, GLuint *))                                                 \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint *))                                        \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                    \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))           \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                             \
    X(void, BlitFramebuffer,                                                                       \
      (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define BAK_GL_DECLARE(ret, name, args) ret(GLAPIENTRY *name) args;
typedef struct BakGl {
    BAK_GL_FUNCTIONS(BAK_GL_DECLARE)
} BakGl;
#undef BAK_GL_DECLARE

extern BakGl g_gl;

/* Every function of the current context; false, having said which one is missing. */
bool gl_load(void);
/* Forget them (the context is gone). */
void gl_unload(void);
/* A program from a vertex and a fragment shader, each given without its version line; 0,
 * having said why, when it does not build. */
GLuint gl_program(const char *vertex, const char *fragment);

#endif
