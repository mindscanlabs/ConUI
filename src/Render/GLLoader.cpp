#include "GLLoader.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>

namespace gl {

void (CONUI_GLAPI* GenBuffers)(GLsizei, GLuint*) = nullptr;
void (CONUI_GLAPI* DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
void (CONUI_GLAPI* BindBuffer)(GLenum, GLuint) = nullptr;
void (CONUI_GLAPI* BufferData)(GLenum, GLsizeiptrCONUI, const void*, GLenum) = nullptr;
void (CONUI_GLAPI* GenVertexArrays)(GLsizei, GLuint*) = nullptr;
void (CONUI_GLAPI* DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
void (CONUI_GLAPI* BindVertexArray)(GLuint) = nullptr;
void (CONUI_GLAPI* EnableVertexAttribArray)(GLuint) = nullptr;
void (CONUI_GLAPI* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

GLuint (CONUI_GLAPI* CreateShader)(GLenum) = nullptr;
void   (CONUI_GLAPI* ShaderSource)(GLuint, GLsizei, const GLcharCONUI* const*, const GLint*) = nullptr;
void   (CONUI_GLAPI* CompileShader)(GLuint) = nullptr;
void   (CONUI_GLAPI* GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
void   (CONUI_GLAPI* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLcharCONUI*) = nullptr;
void   (CONUI_GLAPI* DeleteShader)(GLuint) = nullptr;
GLuint (CONUI_GLAPI* CreateProgram)(void) = nullptr;
void   (CONUI_GLAPI* AttachShader)(GLuint, GLuint) = nullptr;
void   (CONUI_GLAPI* LinkProgram)(GLuint) = nullptr;
void   (CONUI_GLAPI* GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
void   (CONUI_GLAPI* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLcharCONUI*) = nullptr;
void   (CONUI_GLAPI* DeleteProgram)(GLuint) = nullptr;
void   (CONUI_GLAPI* UseProgram)(GLuint) = nullptr;
GLint  (CONUI_GLAPI* GetUniformLocation)(GLuint, const GLcharCONUI*) = nullptr;
void   (CONUI_GLAPI* Uniform1i)(GLint, GLint) = nullptr;
void   (CONUI_GLAPI* Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;

void (CONUI_GLAPI* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;

void (CONUI_GLAPI* ActiveTexture)(GLenum) = nullptr;
void (CONUI_GLAPI* GenFramebuffers)(GLsizei, GLuint*) = nullptr;
void (CONUI_GLAPI* DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
void (CONUI_GLAPI* BindFramebuffer)(GLenum, GLuint) = nullptr;
void (CONUI_GLAPI* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
GLenum (CONUI_GLAPI* CheckFramebufferStatus)(GLenum) = nullptr;

namespace {
    bool g_failed = false;

    template <typename T>
    void grab(T& fn, const char* name) {
        fn = reinterpret_cast<T>(glfwGetProcAddress(name));
        if (!fn) {
            std::fprintf(stderr, "GLLoader: missing entry point %s\n", name);
            g_failed = true;
        }
    }
}

bool Load() {
    g_failed = false;
    grab(GenBuffers,              "glGenBuffers");
    grab(DeleteBuffers,           "glDeleteBuffers");
    grab(BindBuffer,              "glBindBuffer");
    grab(BufferData,              "glBufferData");
    grab(GenVertexArrays,         "glGenVertexArrays");
    grab(DeleteVertexArrays,      "glDeleteVertexArrays");
    grab(BindVertexArray,         "glBindVertexArray");
    grab(EnableVertexAttribArray, "glEnableVertexAttribArray");
    grab(VertexAttribPointer,     "glVertexAttribPointer");

    grab(CreateShader,       "glCreateShader");
    grab(ShaderSource,       "glShaderSource");
    grab(CompileShader,      "glCompileShader");
    grab(GetShaderiv,        "glGetShaderiv");
    grab(GetShaderInfoLog,   "glGetShaderInfoLog");
    grab(DeleteShader,       "glDeleteShader");
    grab(CreateProgram,      "glCreateProgram");
    grab(AttachShader,       "glAttachShader");
    grab(LinkProgram,        "glLinkProgram");
    grab(GetProgramiv,       "glGetProgramiv");
    grab(GetProgramInfoLog,  "glGetProgramInfoLog");
    grab(DeleteProgram,      "glDeleteProgram");
    grab(UseProgram,         "glUseProgram");
    grab(GetUniformLocation, "glGetUniformLocation");
    grab(Uniform1i,          "glUniform1i");
    grab(Uniform2f,          "glUniform2f");

    grab(BlendFuncSeparate,      "glBlendFuncSeparate");

    grab(ActiveTexture,          "glActiveTexture");
    grab(GenFramebuffers,        "glGenFramebuffers");
    grab(DeleteFramebuffers,     "glDeleteFramebuffers");
    grab(BindFramebuffer,        "glBindFramebuffer");
    grab(FramebufferTexture2D,   "glFramebufferTexture2D");
    grab(CheckFramebufferStatus, "glCheckFramebufferStatus");
    return !g_failed;
}

} // namespace gl
