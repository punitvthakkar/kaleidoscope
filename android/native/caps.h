// Everything the app knows about the device, probed once at start and passed down.
// No code asks the system again later or assumes a capability that is not here.
//
// Filled in two steps, both before the first frame:
//   1. Kotlin (onCreate) -> host_start: sdk, display and encoder facts.
//   2. Render thread, when the EGL context first exists: GL and EGL facts, then the
//      decisions (screen, headroom, film). Logged once as a single line, tag "fx":
//      "caps ..." with every field. After that the struct never changes, except
//      handsDelegate, which is set once after measuring the tracker.
#pragma once

enum Output {            // how the final pass encodes its picture
  OUT_SDR = 0,           // 8-bit sRGB, the web's picture exactly
  OUT_HLG = 1,           // BT.2020 HLG, 10-bit (screen: EGL_GL_COLORSPACE_BT2020_HLG_EXT; film: Main10)
};
enum FilmFormat { FILM_NONE = 0, FILM_HEVC10_HLG, FILM_HEVC_SDR, FILM_AVC_SDR };

struct Caps {
  // ---- device (Kotlin) ----
  int sdk = 0;
  int displayW = 0, displayH = 0;        // current mode, pixels, portrait
  float refreshHz = 60;                  // highest rate of the current resolution
  float density = 1;                     // pixels per dp
  bool displayHlg = false;               // Display.getHdrCapabilities lists HLG
  float displayMaxNits = 0;
  char hevcEncoder[64] = "";             // hardware HEVC encoder name, "" if none
  bool hevcMain10 = false;               // that encoder lists HEVCProfileMain10
  char avcEncoder[64] = "";              // hardware H.264 encoder name, "" if none

  // ---- GL and EGL (render thread) ----
  char gpu[64] = "";
  int glMajor = 0, glMinor = 0, maxTexture = 0;
  bool externalEssl3 = false;            // GL_OES_EGL_image_external_essl3
  bool colorHalfFloat = false;           // GL_EXT_color_buffer_half_float
  bool colorFloat = false;               // GL_EXT_color_buffer_float
  bool timerQuery = false;               // GL_EXT_disjoint_timer_query
  bool eglHlg = false;                   // EGL_EXT_gl_colorspace_bt2020_hlg
  bool egl10bit = false;                 // an RGBA 10-10-10-2 window config exists
  bool eglRecordable = false;            // EGL_ANDROID_recordable
  bool eglPresentationTime = false;      // EGL_ANDROID_presentation_time

  // ---- decisions ----
  Output screen = OUT_SDR;               // OUT_HLG only if displayHlg && eglHlg && egl10bit and the surface was created
  float headroom = 1;                    // 1 for OUT_SDR; for OUT_HLG the fixed HLG headroom (see androidplan.md)
  FilmFormat film = FILM_NONE;
  int handsDelegate = 0;                 // 0 CPU, 1 GPU
};
