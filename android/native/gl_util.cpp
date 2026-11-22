#include "gl_util.h"
#include <EGL/eglext.h>
#include <algorithm>

EGLConfig egl_pick_config(EGLDisplay dpy, int r, int g, int b, int a, bool recordable) {
  const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                          EGL_RED_SIZE, r, EGL_GREEN_SIZE, g, EGL_BLUE_SIZE, b, EGL_ALPHA_SIZE, a,
                          EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
                          recordable ? EGL_RECORDABLE_ANDROID : EGL_NONE, EGL_TRUE, EGL_NONE};
  EGLConfig configs[64];
  EGLint n = 0;
  if (!eglChooseConfig(dpy, attrs, configs, 64, &n)) return nullptr;
  for (int i = 0; i < n; i++) {   // eglChooseConfig sorts deeper colour first: take the exact match
    EGLint v[4];
    eglGetConfigAttrib(dpy, configs[i], EGL_RED_SIZE, &v[0]);
    eglGetConfigAttrib(dpy, configs[i], EGL_GREEN_SIZE, &v[1]);
    eglGetConfigAttrib(dpy, configs[i], EGL_BLUE_SIZE, &v[2]);
    eglGetConfigAttrib(dpy, configs[i], EGL_ALPHA_SIZE, &v[3]);
    if (v[0] == r && v[1] == g && v[2] == b && v[3] == a) return configs[i];
  }
  return nullptr;
}

GLuint gl_compile(GLenum type, const char* src, std::string* log) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (ok) return s;
  GLint n = 0;
  glGetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
  std::string l(std::max(1, n), '\0');
  glGetShaderInfoLog(s, n, nullptr, l.data());
  if (log) *log = l.c_str();
  glDeleteShader(s);
  return 0;
}
