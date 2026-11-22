// GL and EGL helpers shared by the host, the renderer and films.
#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <string>

// An RGBA window-and-pbuffer config with exactly these channel sizes, recordable when
// asked (EGL_ANDROID_recordable). Null if none.
EGLConfig egl_pick_config(EGLDisplay dpy, int r, int g, int b, int a, bool recordable);

// Compiles one shader. 0 on failure, with the compiler's log in *log.
GLuint gl_compile(GLenum type, const char* src, std::string* log);
