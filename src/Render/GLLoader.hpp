#pragma once
// ---------------------------------------------------------------------------
// The handful of OpenGL 3.3 entry points ConUI actually uses, loaded by hand
// through glfwGetProcAddress. This replaces GLEW: the renderer issues exactly
// one draw call per frame from one shader, so pulling in a whole loader
// library for it was never justified (docs/DESIGN_SYSTEM.md §12).
//
// GL 1.1 functions (glClear, glViewport, glDrawArrays, glTexImage2D, ...) are
// exported by the system GL library everywhere (opengl32 on Windows, the
// OpenGL framework on macOS, libGL on Linux), so they come from the system
// header; only the 1.2+ entry points need loading. The loaded pointers live
// in namespace gl, so they never collide with prototypes a header declares.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION   // OpenGL is deprecated on macOS, still present
#endif
#include <OpenGL/gl.h>           // the header GLFW itself includes on macOS
#else
#include <GL/gl.h>
#endif
#include <cstddef>
#include <cstdint>

// Tokens beyond GL 1.1. Which of them a system header already defines differs
// by platform and header version (Windows ships a GL 1.1 header, macOS a 2.1
// one), so each is guarded on its own.
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                   0x8892
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW                    0x88E0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                  0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                    0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH                0x8B84
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                       0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1                       0x84C1
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                          0x8058
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE                  0x812F
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                    0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0              0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#endif
#ifndef GL_R8
#define GL_R8                             0x8229
#endif
#ifndef GL_RED
#define GL_RED                            0x1903
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD                       0x8006
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#endif

#if defined(_WIN32)
#define CONUI_GLAPI __stdcall
#else
#define CONUI_GLAPI
#endif

typedef ptrdiff_t GLsizeiptrCONUI;
typedef char GLcharCONUI;

namespace gl {

// Buffers / vertex arrays
extern void (CONUI_GLAPI* GenBuffers)(GLsizei, GLuint*);
extern void (CONUI_GLAPI* DeleteBuffers)(GLsizei, const GLuint*);
extern void (CONUI_GLAPI* BindBuffer)(GLenum, GLuint);
extern void (CONUI_GLAPI* BufferData)(GLenum, GLsizeiptrCONUI, const void*, GLenum);
extern void (CONUI_GLAPI* GenVertexArrays)(GLsizei, GLuint*);
extern void (CONUI_GLAPI* DeleteVertexArrays)(GLsizei, const GLuint*);
extern void (CONUI_GLAPI* BindVertexArray)(GLuint);
extern void (CONUI_GLAPI* EnableVertexAttribArray)(GLuint);
extern void (CONUI_GLAPI* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);

// Shaders
extern GLuint (CONUI_GLAPI* CreateShader)(GLenum);
extern void   (CONUI_GLAPI* ShaderSource)(GLuint, GLsizei, const GLcharCONUI* const*, const GLint*);
extern void   (CONUI_GLAPI* CompileShader)(GLuint);
extern void   (CONUI_GLAPI* GetShaderiv)(GLuint, GLenum, GLint*);
extern void   (CONUI_GLAPI* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLcharCONUI*);
extern void   (CONUI_GLAPI* DeleteShader)(GLuint);
extern GLuint (CONUI_GLAPI* CreateProgram)(void);
extern void   (CONUI_GLAPI* AttachShader)(GLuint, GLuint);
extern void   (CONUI_GLAPI* LinkProgram)(GLuint);
extern void   (CONUI_GLAPI* GetProgramiv)(GLuint, GLenum, GLint*);
extern void   (CONUI_GLAPI* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLcharCONUI*);
extern void   (CONUI_GLAPI* DeleteProgram)(GLuint);
extern void   (CONUI_GLAPI* UseProgram)(GLuint);
extern GLint  (CONUI_GLAPI* GetUniformLocation)(GLuint, const GLcharCONUI*);
extern void   (CONUI_GLAPI* Uniform1i)(GLint, GLint);
extern void   (CONUI_GLAPI* Uniform2f)(GLint, GLfloat, GLfloat);

// Blending
extern void (CONUI_GLAPI* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);

// Textures / framebuffers
extern void (CONUI_GLAPI* ActiveTexture)(GLenum);
extern void (CONUI_GLAPI* GenFramebuffers)(GLsizei, GLuint*);
extern void (CONUI_GLAPI* DeleteFramebuffers)(GLsizei, const GLuint*);
extern void (CONUI_GLAPI* BindFramebuffer)(GLenum, GLuint);
extern void (CONUI_GLAPI* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
extern GLenum (CONUI_GLAPI* CheckFramebufferStatus)(GLenum);

/** Resolves every pointer above. Returns false if any one is missing. */
bool Load();

} // namespace gl
