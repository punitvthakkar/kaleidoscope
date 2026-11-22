// Films (host.h): the final pass drawn a second time into a
// hardware encoder's input surface on a 60 fps grid (HLG films through a 10-bit YUV
// convert pass, see below), AAC from the synth's film tap,
// and AMediaMuxer writing an ordinary MP4. mac/platform/recorder.mm and mac/app/web.mm
// (startFilm / endFilm) are the reference for behaviour.
//
// STATE (F.state, atomic): idle -> starting -> recording -> stopping -> idle.
// THREADS, and what each owns:
//   any        record_start / record_stop / record_active / record_stats (atomics only)
//   fx.film    the writer: output file, both codecs, the input window, the muxer.
//              Sets everything up, then drains and muxes until the film ends.
//   render     the encoder's EGL surface and the 60 fps slot grid (the frame hook).
//   audio      the tap: writes the lock-free audio ring, nothing else.
// BOUNDED: a frame is drawn to the encoder only while fewer than kMaxInFlight drawn
// frames are still waiting for the encoder's output; otherwise it is skipped and
// counted. The render thread never waits on the encoder (swap interval 0, all codec
// calls on the writer). Audio is one fixed ring; held outputs are capped.
//
// A film named "fractonaut-debug-*" (the host's fx.record) is written to
// <filesDir>/films/<name> instead of MediaStore, so it never reaches the gallery.
#include "host.h"
#include "renderer.h"
#include "gl_util.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fx", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "fx", __VA_ARGS__)

namespace {

enum { IDLE, STARTING, RECORDING, STOPPING };
constexpr int kFps = 60;
constexpr int kMaxInFlight = 3;              // drawn frames not yet out of the encoder
constexpr int kRingFrames = 1 << 17;         // audio ring, stereo: about 2.7 s at 48 kHz
constexpr int kHeld = 32;                    // encoded buffers held while the muxer waits for both formats
constexpr int COLOR_FormatSurface = 0x7F000789;
constexpr char kDebugPrefix[] = "fractonaut-debug-";

using PresentFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLnsecsANDROID);

struct Held { ssize_t idx; AMediaCodecBufferInfo info; };

struct Film {
  std::atomic<int> state{IDLE};
  std::atomic<bool> stopAsked{false};

  // Set by record_start before the writer exists; read-only afterwards.
  std::string file;
  bool debug = false;

  // Writer (fx.film); w, h, out, sound are fixed before the render thread sees them.
  std::string path;
  int fd = -1;
  int w = 0, h = 0;
  Output out = OUT_SDR;
  bool sound = false;
  double rate = 48000;
  AMediaCodec* venc = nullptr;
  AMediaCodec* aenc = nullptr;
  ANativeWindow* window = nullptr;
  AMediaMuxer* muxer = nullptr;

  // Render thread.
  EGLSurface surface = EGL_NO_SURFACE;
  GLuint fbo = 0, tex = 0, vao = 0, prog = 0;   // the HLG convert pass
  PresentFn present = nullptr;
  bool feeding = false, intervalSet = false;
  int64_t t0 = -1, lastSlot = -1;
  int64_t inFlight[kMaxInFlight];          // slots drawn, -1 empty

  // Render -> writer.
  std::atomic<int64_t> t0Nanos{-1};        // display time of slot 0, once a frame is in
  std::atomic<bool> inputDone{false};      // no more frames: the EGL surface is gone
  // Writer -> render.
  std::atomic<int64_t> encodedSlot{-1};
  // Stats (any thread reads).
  std::atomic<int> sw{0}, sh{0};
  std::atomic<int64_t> written{0}, dropped{0}, audioDropped{0};
  std::atomic<float> worstSwapMs{0}, worstHookMs{0}, firstSwapMs{0};
  std::atomic<int64_t> slowSwaps{0}, worstSwapAt{0};   // after the first frame: swaps over 4 ms, and the slot of the worst

  // Audio ring: the tap writes, the writer reads.
  float ring[kRingFrames * 2];
  std::atomic<uint64_t> ringW{0}, ringR{0};
  std::atomic<int64_t> audioFirst{0};      // nanos of the ring's first frame
} F;

double nowMs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

void storeMax(std::atomic<float>& a, float v) {
  if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed);
}

std::string jsonString(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if ((unsigned char)c >= 0x20) o += c;
  }
  return o + "\"";
}

// Bits follow pixels as on the Mac: 0.085 bits per pixel for H.264, HEVC about 0.6x,
// 10-bit HDR about 1.25x back (recorder.mm, bitrateFor).
int bitrateFor(FilmFormat f, int w, int h) {
  const double bpp = f == FILM_AVC_SDR ? 0.085 : f == FILM_HEVC_SDR ? 0.051 : 0.064;
  return (int)std::clamp(w * (double)h * kFps * bpp, 6e6, 50e6);
}

// ---------------- audio thread ----------------
void filmTap(const float* l, const float* r, int frames, int64_t nanos) {
  const uint64_t w = F.ringW.load(std::memory_order_relaxed), rd = F.ringR.load(std::memory_order_acquire);
  if (w == 0 && F.audioFirst.load(std::memory_order_relaxed) == 0) F.audioFirst.store(nanos, std::memory_order_relaxed);
  if (w + frames - rd > (uint64_t)kRingFrames) { F.audioDropped.fetch_add(frames, std::memory_order_relaxed); return; }
  for (int i = 0; i < frames; i++) {
    const uint64_t j = (w + i) & (kRingFrames - 1);
    F.ring[2 * j] = l[i];
    F.ring[2 * j + 1] = r[i];
  }
  F.ringW.store(w + frames, std::memory_order_release);
}

// ---------------- render thread ----------------
// HLG films: this encoder (Qualcomm's) refuses RGBA 10-10-10-2 surface buffers, so the
// final pass is drawn into a 10-bit texture and converted here to 10-bit 4:2:0 YUV,
// written straight into the encoder's buffers through a YUV EGL surface
// (EGL_EXT_yuv_surface, GL_EXT_YUV_target). BT.2020 non-constant luminance, limited range.
const char* kVert = R"(#version 300 es
out vec2 vUv;
void main() {
  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
  vUv = p * 0.5 + 0.5;
  gl_Position = vec4(p, 0.0, 1.0);
})";
const char* kFrag = R"(#version 300 es
#extension GL_EXT_YUV_target : require
precision highp float;
uniform sampler2D uSrc;
in vec2 vUv;
layout(yuv) out vec4 oYuv;
void main() {
  vec3 e = clamp(texture(uSrc, vUv).rgb, 0.0, 1.0);   // HLG signal, BT.2020 primaries
  float y = dot(e, vec3(0.2627, 0.6780, 0.0593));
  float cb = (e.b - y) / 1.8814, cr = (e.r - y) / 1.4746;
  oYuv = vec4((64.0 + 876.0 * y) / 1023.0, (512.0 + 896.0 * cb) / 1023.0, (512.0 + 896.0 * cr) / 1023.0, 1.0);
})";

GLuint compile(GLenum type, const char* src) {
  std::string log;
  const GLuint sh = gl_compile(type, src, &log);
  if (!sh) LOGW("film: shader: %s", log.c_str());
  return sh;
}

// The convert pass's GL objects, made with the context current (render thread).
bool makeYuvPass() {
  const GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
  if (vs && fs) {
    F.prog = glCreateProgram();
    glAttachShader(F.prog, vs);
    glAttachShader(F.prog, fs);
    glLinkProgram(F.prog);
    GLint ok = 0;
    glGetProgramiv(F.prog, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(F.prog); F.prog = 0; }
  }
  if (vs) glDeleteShader(vs);
  if (fs) glDeleteShader(fs);
  if (!F.prog) return false;
  glGenVertexArrays(1, &F.vao);
  glGenTextures(1, &F.tex);
  glBindTexture(GL_TEXTURE_2D, F.tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, F.w, F.h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &F.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, F.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, F.tex, 0);
  const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return complete && glGetError() == GL_NO_ERROR;
}

void freeYuvPass() {
  if (F.fbo) glDeleteFramebuffers(1, &F.fbo);
  if (F.tex) glDeleteTextures(1, &F.tex);
  if (F.vao) glDeleteVertexArrays(1, &F.vao);
  if (F.prog) glDeleteProgram(F.prog);
  F.fbo = F.tex = F.vao = F.prog = 0;
}

// 10-bit 4:2:0 two-plane YUV window config (P010), or null.
EGLConfig yuvConfig(EGLDisplay dpy) {
  const EGLint attrs[] = {EGL_COLOR_BUFFER_TYPE, EGL_YUV_BUFFER_EXT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                          EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_YUV_ORDER_EXT, EGL_YUV_ORDER_YUV_EXT,
                          EGL_YUV_NUMBER_OF_PLANES_EXT, 2, EGL_YUV_SUBSAMPLE_EXT, EGL_YUV_SUBSAMPLE_4_2_0_EXT,
                          EGL_YUV_PLANE_BPP_EXT, EGL_YUV_PLANE_BPP_10_EXT, EGL_NONE};
  EGLConfig cs[16];
  EGLint n = 0;
  if (!eglChooseConfig(dpy, attrs, cs, 16, &n) || n < 1) return nullptr;
  EGLint vis = 0, range = 0, csc = 0, rec = 0;
  eglGetConfigAttrib(dpy, cs[0], EGL_NATIVE_VISUAL_ID, &vis);
  eglGetConfigAttrib(dpy, cs[0], EGL_YUV_DEPTH_RANGE_EXT, &range);
  eglGetConfigAttrib(dpy, cs[0], EGL_YUV_CSC_STANDARD_EXT, &csc);
  eglGetConfigAttrib(dpy, cs[0], EGL_RECORDABLE_ANDROID, &rec);
  LOG("film: yuv config 1 of %d visual=0x%x range=0x%x csc=0x%x recordable=%d", n, vis, range, csc, rec);
  return cs[0];
}

// RGBA8 config, recordable if one is.
EGLConfig rgbaConfig(EGLDisplay dpy) {
  EGLConfig c = egl_pick_config(dpy, 8, 8, 8, 8, true);
  return c ? c : egl_pick_config(dpy, 8, 8, 8, 8, false);
}

void stopOnRender(bool fade);

void finishOnRender() {
  F.feeding = false;
  host_set_frame_hook(nullptr);
  if (F.surface != EGL_NO_SURFACE) {   // never current here: the hook restores the screen
    eglDestroySurface(host_gl().display, F.surface);
    F.surface = EGL_NO_SURFACE;
  }
  freeYuvPass();
  F.inputDone.store(true, std::memory_order_release);
}

// Draws only frames on the film's 60 fps grid; true when it switched surfaces.
bool hook(const FrameInfo& fi) {
  if (!F.feeding || F.surface == EGL_NO_SURFACE) return false;
  const double a = nowMs();
  if (F.t0 < 0) F.t0 = fi.displayNanos;
  const int64_t slot = (int64_t)std::floor((fi.displayNanos - F.t0) * (kFps * 1e-9) + 0.25);
  if (slot <= F.lastSlot) return false;
  if (F.lastSlot >= 0 && slot > F.lastSlot + 1) F.dropped.fetch_add(slot - F.lastSlot - 1);   // missed slots
  F.lastSlot = slot;

  const int64_t enc = F.encodedSlot.load(std::memory_order_acquire);
  int busy = 0, freeI = -1;
  for (int i = 0; i < kMaxInFlight; i++) {
    if (F.inFlight[i] > enc) busy++;
    else freeI = i;
  }
  if (busy >= kMaxInFlight) { F.dropped.fetch_add(1); return false; }   // the encoder has not taken the last ones

  const HostGl g = host_gl();
  const EGLDisplay dpy = g.display;
  if (!eglMakeCurrent(dpy, F.surface, F.surface, (EGLContext)g.context)) {
    LOGW("film: make current failed (0x%x)", eglGetError());
    eglMakeCurrent(dpy, g.screenSurface, g.screenSurface, (EGLContext)g.context);
    stopOnRender(false);
    return true;
  }
  if (!F.intervalSet) { eglSwapInterval(dpy, 0); F.intervalSet = true; }
  if (F.fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, F.fbo);
    // A film's highlights are its own: an HLG film keeps everything above white (HLG
    // peak 1000 nits over 203-nit white) whether or not the screen is showing HDR.
    renderer_draw_final(g.renderer, F.w, F.h, F.out, F.out == OUT_HLG ? 4.9f : 1.f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, F.w, F.h);
    glUseProgram(F.prog);
    glBindVertexArray(F.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, F.tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  } else {
    renderer_draw_final(g.renderer, F.w, F.h, F.out, 1.f);
  }
  F.present(dpy, F.surface, slot * 1000000000LL / kFps);
  const double b = nowMs();
  const bool ok = eglSwapBuffers(dpy, F.surface);
  const double c = nowMs();
  const EGLint err = ok ? EGL_SUCCESS : eglGetError();
  eglMakeCurrent(dpy, g.screenSurface, g.screenSurface, (EGLContext)g.context);
  if (F.t0Nanos.load(std::memory_order_relaxed) < 0) F.firstSwapMs.store((float)(c - b));   // allocates the buffers
  else {
    if (c - b > F.worstSwapMs.load(std::memory_order_relaxed)) F.worstSwapAt = slot;
    storeMax(F.worstSwapMs, (float)(c - b));
    storeMax(F.worstHookMs, (float)(nowMs() - a));
    if (c - b > 4) F.slowSwaps.fetch_add(1);
  }
  if (!ok) {
    LOGW("film: swap failed (0x%x)", err);
    F.dropped.fetch_add(1);
    if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) stopOnRender(false);
    return true;
  }
  F.inFlight[freeI] = slot;
  if (F.t0Nanos.load(std::memory_order_relaxed) < 0) {
    F.t0Nanos.store(F.t0, std::memory_order_release);
    host_page_event("recordStarted", F.sound ? "{\"sound\":true}" : "{\"sound\":false}");
  }
  return true;
}

// The writer has the encoder running: make its surface and start feeding frames.
void beginOnRender() {
  F.state.store(RECORDING);
  if (F.stopAsked.load()) { F.state.store(STOPPING); F.inputDone.store(true); return; }
  const HostGl g = host_gl();
  const EGLDisplay dpy = g.display;
  F.present = (PresentFn)eglGetProcAddress("eglPresentationTimeANDROID");
  const bool hlg = F.out == OUT_HLG;
  const EGLConfig config = hlg ? yuvConfig(dpy) : rgbaConfig(dpy);
  if (config && F.present && g.renderer && (!hlg || makeYuvPass())) {
    F.surface = eglCreateWindowSurface(dpy, config, F.window, nullptr);
    if (F.surface != EGL_NO_SURFACE && hlg) ANativeWindow_setBuffersDataSpace(F.window, ADATASPACE_BT2020_ITU_HLG);
  }
  if (F.surface == EGL_NO_SURFACE) {
    LOGW("film: no encoder surface (config=%d, 0x%x)", config != nullptr, eglGetError());
    freeYuvPass();
    F.state.store(STOPPING);
    F.inputDone.store(true);
    return;
  }
  F.feeding = true;
  F.intervalSet = false;
  F.t0 = F.lastSlot = -1;
  std::fill(F.inFlight, F.inFlight + kMaxInFlight, -1);
  host_set_frame_hook(hook);
}

// web.mm endFilm: fade the film's sound out, then stop feeding frames.
void stopOnRender(bool fade) {
  if (F.state.load() != RECORDING) return;   // starting: beginOnRender sees stopAsked
  F.state.store(STOPPING);
  if (F.sound && fade && F.feeding) audio_film_fade(finishOnRender);
  else finishOnRender();
}

// ---------------- writer thread ----------------
AMediaFormat* videoFormat(const Caps& c) {
  AMediaFormat* f = AMediaFormat_new();
  const bool hevc = c.film != FILM_AVC_SDR;
  AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, hevc ? "video/hevc" : "video/avc");
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, F.w);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, F.h);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatSurface);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, bitrateFor(c.film, F.w, F.h));
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BITRATE_MODE, 1);   // VBR
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, kFps);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_PRIORITY, 0);       // real time
  AMediaFormat_setInt32(f, "max-bframes", 0);
  // MediaCodecInfo.CodecProfileLevel: HEVCProfileMain10 2, HEVCProfileMain 1, AVCProfileHigh 8.
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_PROFILE, c.film == FILM_HEVC10_HLG ? 2 : hevc ? 1 : 8);
  // MediaFormat COLOR_*: standard BT2020 6 / BT709 1, transfer HLG 7 / SDR 3, range limited 2.
  const bool hlg = c.film == FILM_HEVC10_HLG;
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_STANDARD, hlg ? 6 : 1);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_TRANSFER, hlg ? 7 : 3);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_RANGE, 2);
  return f;
}

AMediaFormat* audioFormat() {
  AMediaFormat* f = AMediaFormat_new();
  AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, (int)F.rate);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, 160000);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);   // LC
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
  return f;
}

bool setup(const Caps& c, bool* audioBegun) {
  // The display's shape, at most 1080p (the short side 1080): renderer_draw_final
  // scales the frame's final pass to it, whatever the detail ladder draws at.
  const double k = std::min(1.0, 1080.0 / std::max(1, std::min(c.displayW, c.displayH)));
  F.w = (int)std::lround(c.displayW * k) & ~1;
  F.h = (int)std::lround(c.displayH * k) & ~1;
  F.out = c.film == FILM_HEVC10_HLG ? OUT_HLG : OUT_SDR;
  if (F.w <= 0 || F.h <= 0) { LOGW("film: no display size"); return false; }
  F.sw.store(F.w); F.sh.store(F.h);

  if (F.debug) {
    const std::string dir = std::string(host_files_dir()) + "/films";
    mkdir(dir.c_str(), 0700);
    F.path = dir + "/" + F.file;
    F.fd = open(F.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
  } else {
    F.path = "Movies/fractonaut/" + F.file;
    F.fd = host_media_open(F.file.c_str(), "video/mp4");
  }
  if (F.fd < 0) { LOGW("film: cannot open %s", F.path.c_str()); return false; }

  const char* name = c.film == FILM_AVC_SDR ? c.avcEncoder : c.hevcEncoder;
  F.venc = AMediaCodec_createCodecByName(name);
  if (!F.venc) { LOGW("film: no codec %s", name); return false; }
  AMediaFormat* vf = videoFormat(c);
  media_status_t s = AMediaCodec_configure(F.venc, vf, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  AMediaFormat_delete(vf);
  if (s != AMEDIA_OK) { LOGW("film: %s configure failed (%d) at %dx%d", name, s, F.w, F.h); return false; }
  if (AMediaCodec_createInputSurface(F.venc, &F.window) != AMEDIA_OK || !F.window) { LOGW("film: no input surface"); return false; }
  if (AMediaCodec_start(F.venc) != AMEDIA_OK) { LOGW("film: video encoder did not start"); return false; }

  F.sound = audio_film_has_sound();
  if (F.sound) {
    F.rate = audio_sample_rate();
    F.aenc = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    AMediaFormat* af = audioFormat();
    s = F.aenc ? AMediaCodec_configure(F.aenc, af, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) : AMEDIA_ERROR_UNKNOWN;
    AMediaFormat_delete(af);
    if (s != AMEDIA_OK || AMediaCodec_start(F.aenc) != AMEDIA_OK) { LOGW("film: AAC encoder failed"); return false; }
    audio_film_begin(filmTap);
    *audioBegun = true;
  }
  F.muxer = AMediaMuxer_new(F.fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
  if (!F.muxer) { LOGW("film: no muxer"); return false; }
  return true;
}

struct Track {
  AMediaCodec* codec = nullptr;
  ssize_t index = -1;            // muxer track, once its format is known
  bool eos = false;
  Held held[kHeld];
  int nHeld = 0;
  int64_t lastUs = -1;
};

struct Writer {
  Track v, a;
  bool muxing = false, broken = false;
  bool wantAudio = false;       // F.sound until the audio track is given up
  double videoFormatAt = 0;

  void write(Track& t, ssize_t idx, const AMediaCodecBufferInfo& info) {
    size_t cap = 0;
    uint8_t* buf = AMediaCodec_getOutputBuffer(t.codec, idx, &cap);
    if (buf && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) && info.presentationTimeUs > t.lastUs) {
      if (AMediaMuxer_writeSampleData(F.muxer, t.index, buf, &info) != AMEDIA_OK) {
        LOGW("film: muxer write failed");
        broken = true;
      } else {
        t.lastUs = info.presentationTimeUs;
        if (&t == &v) F.written.fetch_add(1);
      }
    }
    AMediaCodec_releaseOutputBuffer(t.codec, idx, false);
  }

  void maybeStartMuxer() {
    if (muxing || v.index < 0 || (wantAudio && a.index < 0)) return;
    if (AMediaMuxer_start(F.muxer) != AMEDIA_OK) { LOGW("film: muxer did not start"); broken = true; return; }
    muxing = true;
    for (Track* t : {&v, &a}) {
      for (int i = 0; i < t->nHeld; i++) write(*t, t->held[i].idx, t->held[i].info);
      t->nHeld = 0;
    }
  }

  // Drain one codec's output; timeoutUs > 0 only for the video encoder (the loop's pace).
  void drain(Track& t, int64_t timeoutUs) {
    while (!t.eos && !broken && t.nHeld < kHeld) {
      AMediaCodecBufferInfo info;
      const ssize_t idx = AMediaCodec_dequeueOutputBuffer(t.codec, &info, timeoutUs);
      timeoutUs = 0;
      if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return;
      if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
      if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* f = AMediaCodec_getOutputFormat(t.codec);
        if (t.index < 0 && !muxing) t.index = AMediaMuxer_addTrack(F.muxer, f);
        if (&t == &v) videoFormatAt = nowMs();
        AMediaFormat_delete(f);
        if (t.index < 0) { LOGW("film: muxer refused a track"); broken = true; return; }
        maybeStartMuxer();
        continue;
      }
      if (idx < 0) { LOGW("film: encoder error %zd", idx); broken = true; return; }
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) t.eos = true;
      if (&t == &v && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG))
        F.encodedSlot.store((info.presentationTimeUs * kFps + 500000) / 1000000, std::memory_order_release);
      if (muxing) write(t, idx, info);
      else t.held[t.nHeld++] = {idx, info};
    }
  }

  // Ring -> AAC input, from the first frame's display time on.
  bool audioInputDone = false;
  void feedAudio(bool ended) {
    if (audioInputDone || broken) return;
    const int64_t t0 = F.t0Nanos.load(std::memory_order_acquire);
    for (;;) {
      const uint64_t w = F.ringW.load(std::memory_order_acquire);
      uint64_t r = F.ringR.load(std::memory_order_relaxed);
      const int64_t first = F.audioFirst.load(std::memory_order_relaxed);
      if (t0 >= 0 && w > r) {   // skip what was heard before the first frame
        const double skipTo = std::ceil((t0 - first) * 1e-9 * F.rate);
        if (skipTo > (double)r) { r = std::min<uint64_t>(w, (uint64_t)skipTo); F.ringR.store(r, std::memory_order_release); }
      }
      const bool have = t0 >= 0 && w > r;
      if (!have && !ended) return;
      const ssize_t in = AMediaCodec_dequeueInputBuffer(a.codec, 0);
      if (in < 0) return;
      size_t cap = 0;
      uint8_t* buf = AMediaCodec_getInputBuffer(a.codec, in, &cap);
      if (!have) {   // ended and empty
        AMediaCodec_queueInputBuffer(a.codec, in, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        audioInputDone = true;
        return;
      }
      const int n = (int)std::min<uint64_t>({w - r, cap / 4, 1024});
      int16_t* pcm = (int16_t*)buf;
      for (int i = 0; i < n; i++) {
        const uint64_t j = (r + i) & (kRingFrames - 1);
        pcm[2 * i] = (int16_t)std::lrint(std::clamp(F.ring[2 * j], -1.f, 1.f) * 32767);
        pcm[2 * i + 1] = (int16_t)std::lrint(std::clamp(F.ring[2 * j + 1], -1.f, 1.f) * 32767);
      }
      const int64_t nanos = first + (int64_t)(r * 1e9 / F.rate);
      AMediaCodec_queueInputBuffer(a.codec, in, 0, n * 4, (uint64_t)((nanos - t0) / 1000), 0);
      F.ringR.store(r + n, std::memory_order_release);
    }
  }

  void run() {
    v.codec = F.venc;
    a.codec = F.aenc;
    a.eos = !F.sound;
    audioInputDone = !F.sound;
    wantAudio = F.sound;
    bool endSent = false, stopPosted = false;
    double endAt = 0;
    for (;;) {
      const bool done = F.inputDone.load(std::memory_order_acquire);
      if (done && !endSent) {
        endSent = true;
        endAt = nowMs();
        if (F.sound) audio_film_end();   // no tap calls after this: the ring is final
        AMediaCodec_signalEndOfInputStream(F.venc);
      }
      if (F.sound) { feedAudio(endSent); if (wantAudio) drain(a, 0); else a.eos = true; }
      drain(v, 5000);
      if (wantAudio && !muxing && a.index < 0 && v.index >= 0 && nowMs() - videoFormatAt > 1500) {
        LOGW("film: no sound arrived, the film goes on without it");   // the muxer cannot add it later
        wantAudio = false;
        maybeStartMuxer();
      }
      if (broken && !done && !stopPosted) { stopPosted = true; host_post_render([] { stopOnRender(false); }); }   // ends on its own
      if (broken && done) break;
      if (v.eos && a.eos) break;
      if (endSent && nowMs() - endAt > 3000) { LOGW("film: encoder did not finish"); break; }
      if (broken || v.nHeld >= kHeld) usleep(5000);
    }
  }
};

void cleanup(bool audioBegun) {
  if (audioBegun) audio_film_end();
  if (F.muxer) { AMediaMuxer_delete(F.muxer); F.muxer = nullptr; }
  for (AMediaCodec** c : {&F.venc, &F.aenc})
    if (*c) { AMediaCodec_stop(*c); AMediaCodec_delete(*c); *c = nullptr; }
  if (F.window) { ANativeWindow_release(F.window); F.window = nullptr; }
}

void finishFile(bool ok) {
  if (F.fd >= 0) {
    if (F.debug) { close(F.fd); if (!ok) unlink(F.path.c_str()); }
    else host_media_close(F.fd, ok);
    F.fd = -1;
  }
}

void writerMain() {
  pthread_setname_np(pthread_self(), "fx.film");
  const Caps& c = host_caps();
  bool audioBegun = false;
  if (!setup(c, &audioBegun)) {
    cleanup(audioBegun);
    finishFile(false);
    F.state.store(IDLE);
    host_page_event("recordFailed", "{}");
    return;
  }
  host_post_render(beginOnRender);

  Writer wr;
  wr.run();
  bool ok = wr.muxing && F.written.load() > 0;
  if (wr.muxing && AMediaMuxer_stop(F.muxer) != AMEDIA_OK) ok = false;
  const double seconds = wr.v.lastUs >= 0 ? wr.v.lastUs * 1e-6 + 1.0 / kFps : 0;
  cleanup(false);   // audio_film_end already ran in run()
  finishFile(ok);
  LOG("film %s frames=%lld dropped=%lld seconds=%.2f size=%dx%d format=%s sound=%d audio_dropped=%lld "
      "first_swap_ms=%.2f worst_swap_ms=%.2f (at %.2f s) swaps_over_4ms=%lld worst_hook_ms=%.2f",
      F.path.c_str(), (long long)F.written.load(), (long long)F.dropped.load(), seconds, F.w, F.h,
      c.film == FILM_HEVC10_HLG ? "hevc10_hlg" : c.film == FILM_HEVC_SDR ? "hevc_sdr" : "avc_sdr", F.sound,
      (long long)F.audioDropped.load(), F.firstSwapMs.load(), F.worstSwapMs.load(), F.worstSwapAt.load() / (double)kFps, (long long)F.slowSwaps.load(), F.worstHookMs.load());
  F.state.store(IDLE);
  if (ok) host_page_event("recordSaved", ("{\"file\":" + jsonString(F.file) + "}").c_str());
  else host_page_event("recordFailed", "{}");
}

}  // namespace

void record_start(const char* file) {
  int idle = IDLE;
  if (host_caps().film == FILM_NONE || !file || !*file || !F.state.compare_exchange_strong(idle, STARTING)) {
    host_page_event("recordFailed", "{}");
    return;
  }
  F.file = file;
  F.debug = !strncmp(file, kDebugPrefix, sizeof kDebugPrefix - 1);
  F.stopAsked.store(false);
  F.inputDone.store(false);
  F.t0Nanos.store(-1);
  F.encodedSlot.store(-1);
  F.written.store(0); F.dropped.store(0); F.audioDropped.store(0);
  F.worstSwapMs.store(0); F.worstHookMs.store(0); F.firstSwapMs.store(0);
  F.slowSwaps.store(0); F.worstSwapAt.store(0);
  F.ringW.store(0); F.ringR.store(0); F.audioFirst.store(0);
  F.sound = false;
  std::thread(writerMain).detach();
}

void record_stop() {
  if (F.state.load() == IDLE) return;
  F.stopAsked.store(true);
  host_post_render([] { stopOnRender(true); });
}

bool record_active() { return F.state.load() != IDLE; }

RecordStats record_stats() { return {F.sw.load(), F.sh.load(), F.written.load(), F.dropped.load()}; }
