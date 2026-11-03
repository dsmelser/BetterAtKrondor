/* The OpenGL functions the port draws with, and its shaders' build (gl.h). */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gl.h"

BakGl g_gl;

/* The version line goes first; the rest is the subset GLSL 3.30 and GLSL ES 3.00 share. */
static const char s_version[] = "#version 330 core\n";

bool gl_load(void) {
#define BAK_GL_LOAD(ret, name, args)                                                               \
    {                                                                                              \
        SDL_FunctionPointer f = SDL_GL_GetProcAddress("gl" #name);                                 \
        if (f == NULL) {                                                                           \
            fprintf(stderr, "OpenGL: no gl" #name "\n");                                           \
            return false;                                                                          \
        }                                                                                          \
        memcpy(&g_gl.name, &f, sizeof f);                                                          \
    }
    BAK_GL_FUNCTIONS(BAK_GL_LOAD)
#undef BAK_GL_LOAD
    return true;
}

void gl_unload(void) {
    memset(&g_gl, 0, sizeof g_gl);
}

static GLuint compile(GLenum kind, const char *source) {
    GLuint shader = g_gl.CreateShader(kind);
    const GLchar *parts[] = {s_version, source};
    g_gl.ShaderSource(shader, 2, parts, NULL);
    g_gl.CompileShader(shader);
    GLint ok = 0;
    g_gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        g_gl.GetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "OpenGL: a %s shader does not compile: %s\n",
                kind == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        g_gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint gl_program(const char *vertex, const char *fragment) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0 || fs == 0) {
        g_gl.DeleteShader(vs);
        g_gl.DeleteShader(fs);
        return 0;
    }
    GLuint program = g_gl.CreateProgram();
    g_gl.AttachShader(program, vs);
    g_gl.AttachShader(program, fs);
    g_gl.LinkProgram(program);
    g_gl.DeleteShader(vs);
    g_gl.DeleteShader(fs);
    GLint ok = 0;
    g_gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        g_gl.GetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "OpenGL: the shaders do not link: %s\n", log);
        g_gl.DeleteProgram(program);
        return 0;
    }
    return program;
}
