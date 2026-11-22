// The native half of the app shell (contract: host.h). One render thread owns EGL,
// the renderer, the engine's frame loop and the surface / app state machine; it is
// paced by AChoreographer on its own ALooper. Kotlin (app.fractonaut.Host) calls in on
// the main thread; native calls back through the static methods of the same class.
#define __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ 1   // newer NDK calls behind __builtin_available
#include <android/bitmap.h>
#include "yuv.h"
#include "host.h"
#include "renderer.h"
#include "audio_out.h"
#include "core/engine.h"
#include "core/synth.h"
#include "gl_util.h"
#ifdef FX_DEBUG
#include "render/debug.h"
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/performance_hint.h>
#include <android/hardware_buffer.h>
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <jni.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "fx", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "fx", __VA_ARGS__)

namespace {

// ---------------- Java ----------------
// Set once in JNI_OnLoad, read-only afterwards.
JavaVM* gVm = nullptr;
jclass gHost = nullptr;
pthread_key_t gDetachKey;
struct {
  jmethodID pageState, pageEvent, reply, haptic, awake, requestPermission, mediaOpen, mediaClose, share, copy;
} J;

// The calling thread's JNIEnv; threads native started are attached on first use and
// detached when they end.
JNIEnv* env() {
  JNIEnv* e = nullptr;
  if (gVm->GetEnv((void**)&e, JNI_VERSION_1_6) == JNI_OK) return e;
  gVm->AttachCurrentThread(&e, nullptr);
  pthread_setspecific(gDetachKey, e);
  return e;
}

// After every call into Kotlin: a pending exception would abort the next JNI call.
void jcheck(JNIEnv* e) { if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); } }

struct JStr {   // a local Java string for one call
  JNIEnv* e; jstring s;
  JStr(JNIEnv* env, const char* c) : e(env), s(c ? env->NewStringUTF(c) : nullptr) {}
  ~JStr() { if (s) e->DeleteLocalRef(s); }
};

std::string str(JNIEnv* e, jstring s) {
  if (!s) return {};
  const char* c = e->GetStringUTFChars(s, nullptr);
  std::string out = c ? c : "";
  if (c) e->ReleaseStringUTFChars(s, c);
  return out;
}

// ---------------- written once by host_start (main), before the render thread starts ----------------
struct Start {
  Caps caps;                      // the GL half is added on the render thread before capsReady
  std::string filesDir, shaderDir;
  Engine* engine = nullptr;
} S;
std::atomic<bool> gCapsReady{false};
std::atomic<float> gViewAspect{0.47f}, gImageAspect{0.75f};   // view and camera picture, width / height (hands)
std::atomic<float> gThermalHeadroom{NAN};   // main writes, render reads for the stats line

// ---------------- the render thread's queue (any thread posts) ----------------
std::mutex gQm;
std::deque<std::function<void()>> gQueue;
int gWake = -1;   // eventfd the render looper watches; created in host_start

void post(std::function<void()> fn) {
  { std::lock_guard<std::mutex> l(gQm); gQueue.push_back(std::move(fn)); }
  uint64_t one = 1;
  write(gWake, &one, sizeof one);
}

double now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------------- render thread ----------------
// Everything in R belongs to the render thread.
struct Render {
  // EGL
  EGLDisplay dpy = EGL_NO_DISPLAY;
  EGLContext ctx = EGL_NO_CONTEXT;
  EGLConfig config8 = nullptr, config10 = nullptr;
  bool surface10 = false;   // the window surface is 10-bit standard range
  EGLSurface pbuffer = EGL_NO_SURFACE;   // only when surfaceless contexts are missing
  // camera light: Kotlin's SurfaceTexture on our external texture
  ASurfaceTexture* camST = nullptr;
  GLuint camTex = 0;
  float camAspect = 0;
  int64_t camStamp = -1;                 // timestamp of the camera image last handed on
  Renderer* renderer = nullptr;

  // state machine
  ANativeWindow* window = nullptr;       // surface: ready while window && surface
  EGLSurface surface = EGL_NO_SURFACE;
  Output surfaceOut = OUT_SDR;           // what this window surface really encodes
  bool screenDecided = false;            // Caps.screen is decided on the first surface
  bool foreground = false;
  int winW = 0, winH = 0;                // window size, pixels

  // pacing
  AChoreographer* chor = nullptr;
  bool scheduled = false;
  double interval = 1 / 60.0;            // the display's vsync period, from Choreographer
  double fullInterval = 1 / 60.0;        // the same at full rate (not lowered for half rate)
  bool halfSet = false;                  // the window was asked for half the rate
  int64_t lastVsync = 0;
  std::function<bool(const FrameInfo&)> hook;
  EngineStats st{};                      // the engine's state after the last step
  APerformanceHintSession* hint = nullptr;
  int64_t hintTarget = 0;

  // what the engine asked for last
  int rw = 0, rh = 0, taps = -1, steps = -1;
  int capW = 0, capH = 0, capFW = 0, capFH = 0;
  float rfs = 0;
  int bufW = 0, bufH = 0;                // window buffer geometry we set

  // detail ladder (see the frame loop): one-second window and the load reported
  double ladAt = 0, ladGpu = 0, ladLoad = 0.65;
  int ladFrames = 0, ladGpuN = 0;
  bool ladHaveGpu = true;                // GPU times are coming in (see the frame loop)
  double gpuSeenAt = 0;                  // when the last fresh GPU time arrived
  double holdUntil = 1e18;               // no detail changes until the page is ready, plus 8 s
  int32_t format = 0;                    // window buffer format, matching the surface's config
  SynthLevels levels{};
  HandsState grip{};
  double gripAt = 0;
  double lastGpu = 0;

  // interface
  bool ready = false;
  double stateAt = 0;

  // stats (fx.stats) and debug
  bool stats = false;
  double statsAt = 0;
  int frames = 0, dropped = 0, gpuN = 0;
  double gpuSum = 0;
  int strokeI = -1;
  int recordSeconds = 0;
  double recordStopAt = 0;
} R;

void schedule();

const char* outName(Output o) { return o == OUT_HLG ? "hlg" : "sdr"; }
const char* filmName(FilmFormat f) {
  return f == FILM_HEVC10_HLG ? "hevc10_hlg" : f == FILM_HEVC_SDR ? "hevc_sdr" : f == FILM_AVC_SDR ? "avc_sdr" : "none";
}

bool hasExt(const char* list, const char* name) {
  if (!list) return false;
  const size_t n = strlen(name);
  for (const char* p = list; (p = strstr(p, name)); p += n)
    if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return true;
  return false;
}

// EGL context and the GL / EGL half of Caps. Once, before anything else on the thread.
bool initGl() {
  Caps& c = S.caps;
  R.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (R.dpy == EGL_NO_DISPLAY || !eglInitialize(R.dpy, nullptr, nullptr)) { LOGW("egl: no display"); return false; }
  const char* ex = eglQueryString(R.dpy, EGL_EXTENSIONS);
  c.eglHlg = hasExt(ex, "EGL_EXT_gl_colorspace_bt2020_hlg");
  c.eglRecordable = hasExt(ex, "EGL_ANDROID_recordable");
  c.eglPresentationTime = hasExt(ex, "EGL_ANDROID_presentation_time");
  const bool noConfig = hasExt(ex, "EGL_KHR_no_config_context");
  const bool surfaceless = hasExt(ex, "EGL_KHR_surfaceless_context");

  R.config8 = egl_pick_config(R.dpy, 8, 8, 8, 8, c.eglRecordable);
  if (!R.config8) R.config8 = egl_pick_config(R.dpy, 8, 8, 8, 8, false);
  R.config10 = egl_pick_config(R.dpy, 10, 10, 10, 2, false);
  c.egl10bit = R.config10 != nullptr && noConfig;   // the context must serve both configs
  if (!R.config8) { LOGW("egl: no RGBA8 config"); return false; }

  const EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  R.ctx = eglCreateContext(R.dpy, noConfig ? EGL_NO_CONFIG_KHR : R.config8, EGL_NO_CONTEXT, ctxAttrs);
  if (R.ctx == EGL_NO_CONTEXT) { LOGW("egl: no ES 3 context (0x%x)", eglGetError()); return false; }
  if (!surfaceless) {
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    R.pbuffer = eglCreatePbufferSurface(R.dpy, R.config8, pb);
  }
  if (!eglMakeCurrent(R.dpy, R.pbuffer, R.pbuffer, R.ctx)) { LOGW("egl: make current failed (0x%x)", eglGetError()); return false; }

  snprintf(c.gpu, sizeof c.gpu, "%s", (const char*)glGetString(GL_RENDERER));
  glGetIntegerv(GL_MAJOR_VERSION, &c.glMajor);
  glGetIntegerv(GL_MINOR_VERSION, &c.glMinor);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &c.maxTexture);
  GLint n = 0;
  bool yuvTarget = false;   // films: the HLG film is written as 10-bit YUV (record/recorder.cpp)
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; i++) {
    const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
    if (!e) continue;
    if (!strcmp(e, "GL_EXT_YUV_target")) yuvTarget = true;
    else if (!strcmp(e, "GL_OES_EGL_image_external_essl3")) c.externalEssl3 = true;
    else if (!strcmp(e, "GL_EXT_color_buffer_half_float")) c.colorHalfFloat = true;
    else if (!strcmp(e, "GL_EXT_color_buffer_float")) c.colorFloat = true;
    else if (!strcmp(e, "GL_EXT_disjoint_timer_query")) c.timerQuery = true;
  }

  // Films: Main10 HLG when the encoder and a YUV target exist (the Qualcomm encoder
  // refuses RGBA 10-10-10-2 surface buffers, so the film pass writes P010), else 8-bit.
  if (c.hevcEncoder[0] && c.hevcMain10 && c.eglRecordable && yuvTarget && hasExt(ex, "EGL_EXT_yuv_surface")) c.film = FILM_HEVC10_HLG;
  else if (c.hevcEncoder[0] && c.eglRecordable) c.film = FILM_HEVC_SDR;
  else if (c.avcEncoder[0] && c.eglRecordable) c.film = FILM_AVC_SDR;
  return true;
}

void logCaps() {
  const Caps& c = S.caps;
  LOG("caps sdk=%d display=%dx%d hz=%.0f density=%.3f displayHlg=%d maxNits=%.0f hevc=%s hevc10=%d avc=%s "
      "gpu=\"%s\" gl=%d.%d maxTex=%d externalEssl3=%d halfFloat=%d float=%d timer=%d eglHlg=%d egl10bit=%d "
      "recordable=%d presentationTime=%d screen=%s headroom=%.2f film=%s hands=%s",
      c.sdk, c.displayW, c.displayH, c.refreshHz, c.density, c.displayHlg, c.displayMaxNits,
      c.hevcEncoder[0] ? c.hevcEncoder : "-", c.hevcMain10, c.avcEncoder[0] ? c.avcEncoder : "-",
      c.gpu, c.glMajor, c.glMinor, c.maxTexture, c.externalEssl3, c.colorHalfFloat, c.colorFloat, c.timerQuery,
      c.eglHlg, c.egl10bit, c.eglRecordable, c.eglPresentationTime, outName(c.screen), c.headroom,
      filmName(c.film), c.handsDelegate ? "gpu" : "cpu");
}

// The window surface. The first surface settles Caps.screen and logs the caps line.
void makeSurface() {
  Caps& c = S.caps;
  if (!R.renderer) return;
  // Standard range only: an HLG screen put white at 203 of 1000 nits, a fifth of a phone
  // panel's peak (androidplan.md). Films encode HDR in their own pass.
  R.surface = EGL_NO_SURFACE;
  R.surfaceOut = OUT_SDR;
  // Standard range, but 10 bits a channel where the device can: white as bright as the
  // screen is set, and four times the steps between black and white, so a slow gradient
  // (a nebula's glow, the soft light of a sky) shows no bands. Same 32 bits a pixel as 8-bit.
  if (c.egl10bit && (!R.screenDecided || R.surface10)) {
    R.format = AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;   // a SurfaceView's default is RGB565
    ANativeWindow_setBuffersGeometry(R.window, 0, 0, R.format);
    ANativeWindow_setBuffersDataSpace(R.window, ADATASPACE_SRGB);
    R.surface = eglCreateWindowSurface(R.dpy, R.config10, R.window, nullptr);
    R.surface10 = R.surface != EGL_NO_SURFACE;
    if (!R.surface10) LOGW("surface: 10-bit window failed (0x%x), 8-bit", eglGetError());
  }
  if (R.surface == EGL_NO_SURFACE) {
    R.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    ANativeWindow_setBuffersDataSpace(R.window, ADATASPACE_SRGB);
    ANativeWindow_setBuffersGeometry(R.window, 0, 0, R.format);
    R.surface = eglCreateWindowSurface(R.dpy, R.config8, R.window, nullptr);
  }
  if (R.surface == EGL_NO_SURFACE) { LOGW("surface: window surface failed (0x%x)", eglGetError()); return; }
  if (!R.screenDecided) {
    R.screenDecided = true;
    c.screen = R.surfaceOut;
    c.headroom = c.screen == OUT_HLG ? 4.9f : 1.f;   // HLG peak 1000 nits over 203-nit white
    logCaps();
    gCapsReady.store(true, std::memory_order_release);
  }
  ANativeWindow_setFrameRate(R.window, R.halfSet ? c.refreshHz / 2 : c.refreshHz, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
  eglMakeCurrent(R.dpy, R.surface, R.surface, R.ctx);
  eglSwapInterval(R.dpy, 1);
  R.bufW = R.bufH = 0;
  R.lastVsync = 0;
  LOG("state surface=ready out=%s bits=%d", outName(R.surfaceOut), R.surface10 || R.surfaceOut == OUT_HLG ? 10 : 8);
}

// An export or still in progress with no frames to carry it: one step per turn of the
// queue, so a postWait from the main thread waits at most one step.
void pumpRenderer() {
  if (!R.renderer || !renderer_busy(R.renderer) || (R.foreground && R.surface != EGL_NO_SURFACE)) return;
  renderer_after_swap(R.renderer);
  post(pumpRenderer);
}

void dropSurface() {
  if (R.surface != EGL_NO_SURFACE) {
    eglMakeCurrent(R.dpy, R.pbuffer, R.pbuffer, R.ctx);
    eglDestroySurface(R.dpy, R.surface);
    R.surface = EGL_NO_SURFACE;
  }
  if (R.window) { ANativeWindow_release(R.window); R.window = nullptr; }
  LOG("state surface=none");
  post(pumpRenderer);
}

// A still (RGBA8, bottom-up) encoded as PNG and written to MediaStore, off the render
// thread (encoding takes a while and host_media_open blocks).
void saveImage(std::vector<uint8_t> rgba, int w, int h, std::string file, std::function<void(bool)> done) {
  std::thread([rgba = std::move(rgba), w, h, file = std::move(file), done = std::move(done)] {
    bool ok = false;
    const std::vector<uint8_t> png = renderer_encode_png(rgba.empty() ? nullptr : rgba.data(), w, h);
    if (!png.empty()) {
      const int fd = host_media_open(file.c_str(), "image/png");
      if (fd >= 0) {
        size_t off = 0;
        while (off < png.size()) {
          const ssize_t w = write(fd, png.data() + off, png.size() - off);
          if (w <= 0) break;
          off += (size_t)w;
        }
        ok = off == png.size();
        host_media_close(fd, ok);
      }
    }
    if (!ok) LOGW("could not save %s", file.c_str());
    if (done) done(ok);
  }).detach();
}

void pushState(const EngineStats& st) {
  char b[320];
  snprintf(b, sizeof b,
           "{\"fps\":%.1f,\"quality\":%.3f,\"width\":%d,\"height\":%d,\"refreshHz\":%.1f,\"maxSize\":%d,"
           "\"painted\":%.1f,\"taps\":%d,\"particles\":%d}",
           st.fps, st.quality, st.renderW, st.renderH, st.refreshHz, R.renderer ? renderer_max_size(R.renderer) : 4096,
           st.painted, st.taps, st.particles);
  host_page_state(b);
}

std::atomic<bool> gGripHeld{false};   // a hand holds the glass (Vision's tracking cadence)

// Grip markers: on change, and at most 30 times a second while a grip moves (view.mm).
void gripFrom(const HandsState& h, double t) {
  gGripHeld.store(h.grabMode > 0, std::memory_order_relaxed);
  bool changed = false, moved = false;
  for (int i = 0; i < 2; i++) {
    if (h.grabOn[i] != R.grip.grabOn[i]) changed = true;
    else if (h.grabOn[i] && (std::fabs(h.grabX[i] - R.grip.grabX[i]) > 1e-3f || std::fabs(h.grabY[i] - R.grip.grabY[i]) > 1e-3f)) moved = true;
  }
  if (!changed && !(moved && t - R.gripAt >= 1 / 30.0)) return;
  R.grip = h;
  R.gripAt = t;
  char b[200];
  snprintf(b, sizeof b, "{\"hands\":[{\"on\":%s,\"x\":%.4f,\"y\":%.4f},{\"on\":%s,\"x\":%.4f,\"y\":%.4f}]}",
           h.grabOn[0] ? "true" : "false", h.grabX[0], h.grabY[0], h.grabOn[1] ? "true" : "false", h.grabX[1], h.grabY[1]);
  host_page_event("grip", b);
}

void statsLine(double t, const EngineStats& st) {
  if (t - R.statsAt < 1) return;
  const double span = R.statsAt > 0 ? t - R.statsAt : 1;
  R.statsAt = t;
  long pages = 0, rss = 0;
  if (FILE* f = fopen("/proc/self/statm", "r")) { if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0; fclose(f); }
  const double memMb = rss * (double)sysconf(_SC_PAGESIZE) / (1 << 20);
  const double heapMb = mallinfo().uordblks / double(1 << 20);
  const RecordStats rs = record_stats();
  LOG("stats fps=%.0f gpu_ms=%.2f size=%dx%d level=%d dropped=%d mem_mb=%.0f thermal=%.2f film=%s "
      "draw=%dx%d hz=%.0f out=%s heap_mb=%.1f film_written=%lld film_dropped=%lld xruns=%d audio_buf=%d",
      R.frames / span, R.gpuN ? R.gpuSum / R.gpuN * 1000 : 0, st.renderW, st.renderH, st.level, R.dropped, memMb,
      gThermalHeadroom.load(), record_active() ? "recording" : "idle", st.drawW, st.drawH, 1 / R.interval,
      outName(R.surfaceOut), heapMb, (long long)rs.written, (long long)rs.dropped, audio_xruns(), audio_buffer_frames());
  R.frames = R.dropped = R.gpuN = 0;
  R.gpuSum = 0;
}

std::string stamp() {
  time_t t = time(nullptr);
  tm lt;
  localtime_r(&t, &lt);
  char b[32];
  strftime(b, sizeof b, "%Y%m%d-%H%M%S", &lt);
  return b;
}

// Half rate (EngineStats.halfRate): the window asks the display for half its rate.
void setHalf(bool half) {
  if (half == R.halfSet) return;
  R.halfSet = half;
  if (R.window) ANativeWindow_setFrameRate(R.window, half ? S.caps.refreshHz / 2 : S.caps.refreshHz, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
}

// One frame, mirroring mac/app/view.mm (metalDisplayLink:needsUpdate:).
void frame(int64_t vsyncNanos, int64_t presentNanos) {
  R.scheduled = false;
  if (!R.foreground || R.surface == EGL_NO_SURFACE) return;
  schedule();
  const double workStart = now();
  Engine* e = S.engine;
  setHalf(R.st.halfRate);
  const double full = R.fullInterval;
  const double frameDt = R.halfSet ? 2 * full : full;   // the frame time we aim for
  // The display may keep its full rate (another window asks for it): then vsyncs that
  // come early for half rate are let pass.
  if (R.halfSet && R.lastVsync && (vsyncNanos - R.lastVsync) * 1e-9 < frameDt * 0.75) return;
  const bool resumed = !R.lastVsync;   // the first frame since start or the background
  double dt = R.lastVsync ? (vsyncNanos - R.lastVsync) * 1e-9 : frameDt;
  if (R.lastVsync && dt > frameDt * 1.5) R.dropped++;
  R.lastVsync = vsyncNanos;
  const double t = vsyncNanos * 1e-9;

  EngineEvent ev;
  while (engine_poll_event(e, &ev)) {
    if (ev.type == EV_PLUCK) synth_pluck(ev.x, ev.y, ev.v);
    else if (ev.type == EV_HAPTIC) host_haptic(ev.a);
    else if (ev.type == EV_NEXT_CHAMBER) host_page_event("nextChamber", "{}");
    // EV_BREATH_CUE: the interface keeps its own clock for the words.
  }
  if (R.strokeI >= 0) {   // fx.stroke: a circle painted through the engine, as on the Mac
    const float d = S.caps.density, cx = R.st.drawW / d / 2, cy = R.st.drawH / d / 2, rad = std::min(cx, cy) * 0.5f;
    const double a = R.strokeI * 0.05;
    engine_pointer(e, 7, R.strokeI == 0 ? PTR_DOWN : R.strokeI == 180 ? PTR_UP : PTR_MOVE,
                   cx + rad * (float)cos(a), cy + rad * (float)sin(a), now(), false);
    if (R.strokeI++ == 180) { R.strokeI = -1; LOG("stroke done"); }
  }

  if (R.camST && ASurfaceTexture_updateTexImage(R.camST) == 0) {
    const int64_t stamp = ASurfaceTexture_getTimestamp(R.camST);
    if (stamp != R.camStamp) {   // a new camera image: the renderer copies only these
      R.camStamp = stamp;
      float m[16];
      ASurfaceTexture_getTransformMatrix(R.camST, m);
      renderer_set_camera(R.renderer, R.camTex, m, R.camAspect);
    }
  }
  renderer_fade_veil(R.renderer, (float)std::min(dt, 0.05));
  const Frame& f = engine_step(e, dt, R.levels.bass, R.levels.mid, R.levels.high);
  const EngineStats st = engine_stats(e);   // the one snapshot this frame
  R.st = st;
  synth_update((float)std::min(dt, 0.05), st.energy, st.lung);
  R.levels = synth_levels();
  if (st.drawW <= 0) return;

  renderer_reserve(R.renderer, st.capW, st.capH, st.capEchoW, st.capEchoH);
  if (st.renderW != R.rw || st.renderH != R.rh || st.echoScale != R.rfs || st.capW != R.capW || st.capH != R.capH ||
      st.capEchoW != R.capFW || st.capEchoH != R.capFH) {
    if (renderer_resize(R.renderer, st.renderW, st.renderH, st.echoScale)) {
      R.rw = st.renderW; R.rh = st.renderH; R.rfs = st.echoScale;
      R.capW = st.capW; R.capH = st.capH; R.capFW = st.capEchoW; R.capFH = st.capEchoH;
    }
  }
  if (st.ssTaps != R.taps || st.rmSteps != R.steps) { renderer_set_detail(R.renderer, st.ssTaps, st.rmSteps); R.taps = st.ssTaps; R.steps = st.rmSteps; }
  if (st.drawW != R.bufW || st.drawH != R.bufH) {   // the compositor scales the buffer to the view
    ANativeWindow_setBuffersGeometry(R.window, st.drawW == R.winW && st.drawH == R.winH ? 0 : st.drawW,
                                     st.drawW == R.winW && st.drawH == R.winH ? 0 : st.drawH, R.format);
    R.bufW = st.drawW; R.bufH = st.drawH;
  }
  const HandsState hs = engine_hands_state(e);
  gripFrom(hs, t);

  const float headroom = R.surfaceOut == OUT_HLG ? S.caps.headroom : 1.f;
  renderer_render(R.renderer, f, st.drawW, st.drawH, R.surfaceOut, headroom);
  if (R.hook && R.hook(FrameInfo{presentNanos, st.drawW, st.drawH, headroom}))
    eglMakeCurrent(R.dpy, R.surface, R.surface, R.ctx);   // in case the hook forgot
  renderer_frame_end(R.renderer);   // the GPU timer covers the film's pass too
  if (!eglSwapBuffers(R.dpy, R.surface)) {
    const EGLint err = eglGetError();
    LOGW("swap failed (0x%x)", err);
    if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) { eglMakeCurrent(R.dpy, R.pbuffer, R.pbuffer, R.ctx); eglDestroySurface(R.dpy, R.surface); R.surface = EGL_NO_SURFACE; }
    return;
  }
  renderer_after_swap(R.renderer);
  R.frames++;
  const double gpu = renderer_gpu_seconds(R.renderer);
  const bool fresh = gpu > 0 && gpu != R.lastGpu;
  if (fresh) { R.lastGpu = gpu; R.gpuSum += gpu; R.gpuN++; }
  // Detail ladder: this GPU slows its clock under light work, so its time always reads
  // as 70-80% load; once a second the host reports its own verdict as the load instead.
  const double rate = 1 / frameDt;   // frames per second we aim for
  // A window starts with the first frame, not with the time spent away.
  if (resumed) { R.ladAt = t; R.ladFrames = 0; R.ladGpu = 0; R.ladGpuN = 0; R.gpuSeenAt = t; }
  R.ladFrames++;
  if (fresh) { R.ladGpu += gpu; R.ladGpuN++; R.gpuSeenAt = t; }
  if (t - R.ladAt >= 1) {
    const double fps = R.ladFrames / (t - R.ladAt);
    // A second can pass without a GPU time (results still in flight at startup, or the
    // timer reset by a clock change, GL_GPU_DISJOINT): that alone must not hand the
    // ladder to the frame rate, which drops detail for every hitch the GPU did not
    // cause. Only a device with no timer, or one silent for 3 s, is judged that way.
    R.ladHaveGpu = S.caps.timerQuery && t - R.gpuSeenAt < 3;
    const double real = R.ladGpuN ? R.ladGpu / R.ladGpuN * rate : 0;
    const bool missed = fps < rate * 0.955, smooth = fps > rate * 0.97;   // 115 of 120 is already visible judder
    R.ladLoad = t < R.holdUntil ? (smooth ? 0.65 : 0.3) : (missed && real > 0.85 ? 0.9 : 0.3);
    R.ladAt = t; R.ladFrames = 0; R.ladGpu = 0; R.ladGpuN = 0;
  }
  // Without GPU times the engine judges by frame rate alone (a negative time only
  // passes the interval on).
  engine_report_gpu(e, R.ladHaveGpu ? R.ladLoad / rate : -1, full);

  if (R.ready && t - R.stateAt >= 0.2) { R.stateAt = t; pushState(st); }
  if (R.recordSeconds > 0 && R.ready && R.frames > 0) {   // fx.record
    const std::string file = "fractonaut-debug-" + stamp() + ".mp4";
    LOG("film starting %s for %d s", file.c_str(), R.recordSeconds);
    record_start(file.c_str());
    R.recordStopAt = t + R.recordSeconds;
    R.recordSeconds = 0;
  }
  if (R.recordStopAt > 0 && t >= R.recordStopAt) { R.recordStopAt = 0; record_stop(); }
  if (R.stats) statsLine(t, st);
  if (__builtin_available(android 33, *)) {
    if (R.hint) {
      const int64_t target = (int64_t)(frameDt * 1e9);
      if (target != R.hintTarget) { R.hintTarget = target; APerformanceHint_updateTargetWorkDuration(R.hint, target); }
      APerformanceHint_reportActualWorkDuration(R.hint, std::max<int64_t>(1, (int64_t)((now() - workStart) * 1e9)));
    }
  }
}

void onVsync(const AChoreographerFrameCallbackData* d, void*) {
  if (__builtin_available(android 33, *)) {
    const size_t i = AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(d);
    frame(AChoreographerFrameCallbackData_getFrameTimeNanos(d),
          AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(d, i));
  }
}
void onFrame64(int64_t nanos, void*) { frame(nanos, nanos + (int64_t)(2 * R.interval * 1e9)); }
void onRefreshRate(int64_t periodNanos, void*) {
  if (periodNanos > 0) R.interval = periodNanos * 1e-9;
  if (periodNanos > 0 && !R.halfSet) R.fullInterval = R.interval;
  LOG("display vsync %.1f Hz", 1e9 / (double)periodNanos);
}

void schedule() {
  if (R.scheduled || !R.foreground || R.surface == EGL_NO_SURFACE) return;
  R.scheduled = true;
  if (__builtin_available(android 33, *)) AChoreographer_postVsyncCallback(R.chor, onVsync, nullptr);
  else AChoreographer_postFrameCallback64(R.chor, onFrame64, nullptr);
}

int drain(int fd, int, void*) {
  uint64_t n;
  read(fd, &n, sizeof n);
  std::deque<std::function<void()>> q;
  { std::lock_guard<std::mutex> l(gQm); q.swap(gQueue); }
  for (auto& fn : q) fn();
  return 1;
}

void renderMain() {
  pthread_setname_np(pthread_self(), "fx.render");
  setpriority(PRIO_PROCESS, gettid(), -10);
  if (__builtin_available(android 33, *)) {   // lets the system clock the CPU for the frame's deadline
    if (APerformanceHintManager* m = APerformanceHint_getManager()) {
      int32_t tid = gettid();
      R.hintTarget = (int64_t)(R.fullInterval * 1e9);
      R.hint = APerformanceHint_createSession(m, &tid, 1, R.hintTarget);
    }
  }
  ALooper* looper = ALooper_prepare(0);
  // Without GL or a renderer the thread still serves its queue (surface calls wait on
  // it), but no surface is made and nothing draws.
  if (initGl()) {
    char err[4096] = "";
    R.renderer = renderer_create(S.caps, S.shaderDir.c_str(), err, sizeof err);
    if (!R.renderer) LOGW("renderer_create failed: %s", err);
  } else {
    LOGW("render thread: no GL, nothing will draw");
  }
  R.chor = AChoreographer_getInstance();
  AChoreographer_registerRefreshRateCallback(R.chor, onRefreshRate, nullptr);
  ALooper_addFd(looper, gWake, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, drain, nullptr);
  drain(gWake, 0, nullptr);   // anything posted before the looper existed
  for (;;) ALooper_pollOnce(-1, nullptr, nullptr, nullptr);
}

// Camera light off (render thread).
void stopCamera() {
  if (!R.camST) return;
  if (R.renderer) renderer_set_camera(R.renderer, 0, nullptr, 0);
  engine_set_cam_on(S.engine, false);
  ASurfaceTexture_detachFromGLContext(R.camST);
  glDeleteTextures(1, &R.camTex);
  ASurfaceTexture_release(R.camST);
  R.camST = nullptr;
  R.camTex = 0;
  R.camStamp = -1;
}

// Runs fn on the render thread and returns once it has run.
void postWait(std::function<void()> fn) {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  post([&] {
    fn();
    std::lock_guard<std::mutex> l(m);
    done = true;
    cv.notify_one();
  });
  std::unique_lock<std::mutex> l(m);
  cv.wait(l, [&] { return done; });
}

// ---------------- main thread ----------------
// Pending permission requests (main thread only): id -> what.
std::unordered_map<int, std::string> gAsked;

// PowerManager thermal status to the engine's four states (the Mac's nominal / fair /
// serious / critical). Android's LIGHT and MODERATE are "not impacted" and "not largely
// impacted": none and light are nominal, moderate is fair, severe is serious, and
// critical or worse is critical. Measured GPU time still steps detail down by itself.
int thermalState(int status) { return status <= 1 ? 0 : status == 2 ? 1 : status == 3 ? 2 : 3; }

void reply(int id, bool ok, const char* error) { if (id) host_reply(id, ok, error); }

}  // namespace

// ================= host.h =================
double host_now() { return now(); }
const Caps& host_caps() { gCapsReady.load(std::memory_order_acquire); return S.caps; }
const char* host_files_dir() { return S.filesDir.c_str(); }
Engine* host_engine() { return S.engine; }
void host_post_render(std::function<void()> fn) { post(std::move(fn)); }
HostGl host_gl() { return HostGl{R.dpy, R.ctx, R.surface, R.renderer}; }
void host_set_frame_hook(std::function<bool(const FrameInfo&)> hook) {
  post([hook = std::move(hook)]() mutable { R.hook = std::move(hook); });
}

void host_start(const DeviceFacts& d) {
  Caps& c = S.caps;
  c.sdk = d.sdk; c.displayW = d.displayW; c.displayH = d.displayH;
  c.refreshHz = d.refreshHz; c.density = d.density > 0 ? d.density : 1;
  c.displayHlg = d.displayHlg; c.displayMaxNits = d.displayMaxNits;
  snprintf(c.hevcEncoder, sizeof c.hevcEncoder, "%s", d.hevcEncoder ? d.hevcEncoder : "");
  c.hevcMain10 = d.hevcMain10;
  c.handsDelegate = d.handsDelegate;
  snprintf(c.avcEncoder, sizeof c.avcEncoder, "%s", d.avcEncoder ? d.avcEncoder : "");
  S.filesDir = d.filesDir ? d.filesDir : "";
  S.shaderDir = S.filesDir + "/shaders";
  S.engine = engine_create();
  gWake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  std::thread(renderMain).detach();
}

void host_surface_created(void* w) {
  ANativeWindow* win = (ANativeWindow*)w;
  ANativeWindow_acquire(win);
  post([win] {
    if (R.window) dropSurface();
    R.window = win;
    R.winW = ANativeWindow_getWidth(win);
    R.winH = ANativeWindow_getHeight(win);
    makeSurface();
    schedule();
  });
}

void host_surface_changed(int w, int h) {
  const float d = S.caps.density;
  engine_resize(S.engine, w / d, h / d, d);
  if (w > 0 && h > 0) { gViewAspect.store((float)w / h); engine_set_camera_aspect(S.engine, (float)w / h); }
  post([w, h] { R.winW = w; R.winH = h; R.bufW = R.bufH = 0; LOG("state surface size=%dx%d", w, h); });
}

void host_surface_destroyed() { postWait(dropSurface); }

void host_foreground(bool on) {
  audio_set_foreground(on);
  if (!on) {
    if (record_active()) record_stop();   // the film is saved
    audio_in_stop();
    audio_set_listening(false);
  }
  post([on] {
    R.foreground = on;
    R.lastVsync = 0;
    LOG("state app=%s", on ? "foreground" : "background");
    schedule();
    pumpRenderer();
  });
}

void host_touch(int id, int kind, float x, float y, double t) {
  const float d = S.caps.density;
  engine_pointer(S.engine, id, kind, x / d, y / d, t, false);
}

void host_thermal(float headroom, int status) {
  if (!std::isnan(headroom)) gThermalHeadroom.store(headroom);
  engine_set_thermal(S.engine, thermalState(status));
}

void host_set_param(const char* key, double num, const char* s) { engine_set_param(S.engine, key, num, s && *s ? s : nullptr); }
// The interface caps the picture at 2 pixels per point, which suits a laptop. A phone
// panel has 3 or more, so that cap drew the picture at just over half the screen's
// resolution and stretched it. Here the screen's own pixels are the target, and the
// detail ladder (which already holds the frame rate) decides how much of it the GPU keeps.
void host_set_system(const char* key, double num) {
  if (!strcmp(key, "maxDpr")) num = std::max(num, (double)S.caps.density);
  engine_set_system(S.engine, key, num);
}

void host_message(const PageMessage& m) {
  const std::string type = m.type ? m.type : "";
  Engine* e = S.engine;
  if (type == "ready") {
    post([] { R.ready = true; R.holdUntil = host_now() + 8; pushState(R.st); });
  } else if (type == "veil") {
    const float s = (float)m.num;
    post([s] { if (R.renderer) renderer_start_veil(R.renderer, s); });
  } else if (type == "clear") {
    post([e] { if (R.renderer) renderer_clear(R.renderer); engine_clear_pens(e); });
  } else if (type == "restore") {
    post([] { if (R.renderer) renderer_restore(R.renderer); });
  } else if (type == "breathe") {
    post([e] { engine_reset_breathing(e); });
  } else if (type == "sound") {
    audio_set_sound(m.on);
    if (m.on) host_page_event("soundStarted", "{}");   // audible at once: no gesture needed
  } else if (type == "root") {
    synth_set_root((int)m.num);
  } else if (type == "mic") {
    if (!m.on) { audio_in_stop(); audio_set_listening(false); return; }
    gAsked[m.id] = "mic";
    host_request_permission("mic", m.id);
  } else if (type == "handsOptions") {
    engine_set_two_hands(e, m.on);
  } else if (type == "snapshot") {
    std::string file = m.str ? m.str : "";
    post([file] {
      if (R.renderer) renderer_snapshot(R.renderer, [file](std::vector<uint8_t> px, int w, int h) { saveImage(std::move(px), w, h, file, nullptr); });
      pumpRenderer();
    });
  } else if (type == "export") {
    const int w = m.w, h = m.h, id = m.id;
    std::string file = m.str ? m.str : "";
    post([e, w, h, id, file] {
      if (!R.renderer) { reply(id, false, "failed"); return; }
      // The export steps a copy of the engine, so the live picture runs on undisturbed,
      // and spreads its frames over the next ones; the ladder holds still meanwhile.
      Engine* copy = engine_clone(e);
      R.holdUntil = std::max(R.holdUntil, host_now() + 3);
      renderer_export_hires(R.renderer, w, h, 60, [copy](float dt) -> const Frame& { return engine_step(copy, dt, 0, 0, 0); },
                            [id, file, copy](std::vector<uint8_t> px, int pw, int ph) {
                              engine_destroy(copy);
                              saveImage(std::move(px), pw, ph, file, [id](bool ok) { reply(id, ok, ok ? nullptr : "failed"); });
                            });
      pumpRenderer();
    });
  } else if (type == "record") {
    if (m.on) record_start(m.str ? m.str : "");
    else record_stop();
  } else if (type == "share") {
    host_share(m.str ? m.str : "");
  } else if (type == "copy") {
    host_copy(m.str ? m.str : "");
  } else if (type == "awake") {
    host_awake(m.on);
  }
  // overUI: Kotlin routes each touch by the page's verdict for that touch.
  // fullscreen: Kotlin's (system bars).
}

void host_camera_texture(void* ast, float aspect) {
  ASurfaceTexture* st = (ASurfaceTexture*)ast;
  postWait([st, aspect] {
    stopCamera();
    if (!st) return;
    if (!R.renderer) { ASurfaceTexture_release(st); return; }
    glGenTextures(1, &R.camTex);
    if (ASurfaceTexture_attachToGLContext(st, R.camTex) != 0) {
      LOGW("camera: cannot attach the picture");
      glDeleteTextures(1, &R.camTex);
      R.camTex = 0;
      ASurfaceTexture_release(st);
      return;
    }
    R.camST = st;
    R.camAspect = aspect;
    engine_set_cam_on(S.engine, true);
  });
}
void host_hands_enabled(bool on) { engine_set_hands_enabled(S.engine, on, now()); }
// The part of the camera picture that reaches the whole screen: the view's own shape
// (equal finger travel both ways), high and small, so the palm stays in sight at the edges.
struct HandWindow { float cx, cy, fx, fy; };
HandWindow handWindow() {
  const float view = gViewAspect.load(), image = gImageAspect.load();
  float fy = 0.62f, fx = fy * view / image;
  if (fx > 0.8f) { fx = 0.8f; fy = fx * image / view; }
  return {0.5f, 0.11f + fy / 2, fx, fy};
}
// Landmarks are rescaled onto that window (host_hands), which turns the picture's aspect
// into the view's: the engine is told the view's aspect.
void host_camera_aspect(float a) { if (a > 0) gImageAspect.store(a); engine_set_camera_aspect(S.engine, gViewAspect.load()); }
bool host_grip_held() { return gGripHeld.load(std::memory_order_relaxed); }
void host_hands(const HandObs* hands, int n, double t) {
  const HandWindow w = handWindow();
  HandObs out[2];
  n = std::max(0, std::min(n, 2));
  for (int i = 0; i < n; i++) {
    out[i] = hands[i];
    for (int k = 0; k < 21; k++) {
      out[i].x[k] = 0.5f + (hands[i].x[k] - w.cx) * (0.8f / w.fx);
      out[i].y[k] = 0.5f + (hands[i].y[k] - w.cy) * (0.8f / w.fy);
    }
  }
  engine_hands(S.engine, out, n, t);
}

void host_permission(int id, bool granted) {
  auto it = gAsked.find(id);
  if (it == gAsked.end()) return;
  const std::string what = it->second;
  gAsked.erase(it);
  if (what == "mic") {
    if (!granted) { reply(id, false, "denied"); return; }
    const bool ok = audio_in_start();
    audio_set_listening(ok);
    reply(id, ok, ok ? nullptr : "unavailable");
  }
}

void host_debug(const char* key, const char* value) {
  const std::string k = key ? key : "", v = value ? value : "";
  LOG("debug %s=%s", k.c_str(), v.c_str());
  if (k == "fx.stats") post([on = v == "true"] { R.stats = on; });
  else if (k == "fx.stroke") post([] { R.strokeI = 0; });
  else if (k == "fx.record") post([s = atoi(v.c_str())] { R.recordSeconds = s; });
#ifdef FX_DEBUG
  else if (k == "fx.skydbg") post([m = atoi(v.c_str())] { renderer_sky_debug(m); });
  else if (k == "fx.chambers") post([v] { render_debug_chambers(v.c_str()); });
  else if (k == "fx.looks") post([v] { render_debug_looks(v.c_str()); });
#endif
  // fx.script and fx.hands run in the page; Kotlin evaluates them after `ready`.
}

// ---------------- native -> Kotlin ----------------
void host_page_state(const char* json) {
  JNIEnv* e = env();
  JStr j(e, json);
  e->CallStaticVoidMethod(gHost, J.pageState, j.s);
  jcheck(e);
}
void host_page_event(const char* name, const char* json) {
  if (name && (!strcmp(name, "recordSaved") || !strcmp(name, "recordFailed"))) LOG("film %s %s", name, json ? json : "{}");
  JNIEnv* e = env();
  JStr n(e, name), j(e, json && *json ? json : "{}");
  e->CallStaticVoidMethod(gHost, J.pageEvent, n.s, j.s);
  jcheck(e);
}
void host_reply(int id, bool ok, const char* error) {
  JNIEnv* e = env();
  JStr er(e, error);
  e->CallStaticVoidMethod(gHost, J.reply, id, (jboolean)ok, er.s);
  jcheck(e);
}
void host_haptic(int kind) { JNIEnv* e = env(); e->CallStaticVoidMethod(gHost, J.haptic, kind); jcheck(e); }
void host_awake(bool on) { JNIEnv* e = env(); e->CallStaticVoidMethod(gHost, J.awake, (jboolean)on); jcheck(e); }
void host_request_permission(const char* what, int id) {
  JNIEnv* e = env();
  JStr w(e, what);
  e->CallStaticVoidMethod(gHost, J.requestPermission, w.s, id);
  jcheck(e);
}
int host_media_open(const char* file, const char* mime) {
  JNIEnv* e = env();
  JStr f(e, file), m(e, mime);
  const int fd = e->CallStaticIntMethod(gHost, J.mediaOpen, f.s, m.s);
  if (e->ExceptionCheck()) { jcheck(e); return -1; }
  return fd;
}
void host_media_close(int fd, bool keep) {
  JNIEnv* e = env();
  e->CallStaticVoidMethod(gHost, J.mediaClose, fd, (jboolean)keep);
  jcheck(e);
}
void host_share(const char* file) {
  JNIEnv* e = env();
  JStr f(e, file);
  e->CallStaticVoidMethod(gHost, J.share, f.s);
  jcheck(e);
}
void host_copy(const char* text) {
  JNIEnv* e = env();
  JStr t(e, text);
  e->CallStaticVoidMethod(gHost, J.copy, t.s);
  jcheck(e);
}

// ================= JNI (app.fractonaut.Host) =================
namespace {

void jStart(JNIEnv* e, jclass, jint sdk, jint w, jint h, jfloat hz, jfloat density, jboolean hlg, jfloat nits,
            jstring hevc, jboolean hevc10, jstring avc, jstring files, jint handsDelegate) {
  const std::string hv = str(e, hevc), av = str(e, avc), fd = str(e, files);
  DeviceFacts d{sdk, w, h, hz, density, (bool)hlg, nits, hv.c_str(), (bool)hevc10, av.c_str(), fd.c_str(), handsDelegate};
  host_start(d);
}
void jSurfaceCreated(JNIEnv* e, jclass, jobject surface) {
  ANativeWindow* w = ANativeWindow_fromSurface(e, surface);
  if (!w) return;
  host_surface_created(w);
  ANativeWindow_release(w);   // the host holds its own reference
}
void jCameraTexture(JNIEnv* e, jclass, jobject st, jfloat aspect) {
  host_camera_texture(st ? ASurfaceTexture_fromSurfaceTexture(e, st) : nullptr, aspect);
}
void jHandsEnabled(JNIEnv*, jclass, jboolean on) { host_hands_enabled(on); }
void jCameraAspect(JNIEnv*, jclass, jfloat a) { host_camera_aspect(a); }
// The tracker's camera image, converted into `bitmap` (RGBA 8888, already the upright size).
jboolean jYuvToBitmap(JNIEnv* e, jclass, jobject y, jobject u, jobject v, jint yRow, jint yPix, jint cRow, jint cPix,
                      jint w, jint h, jint rot, jobject bitmap) {
  YuvImage s{(const uint8_t*)e->GetDirectBufferAddress(y), (const uint8_t*)e->GetDirectBufferAddress(u),
             (const uint8_t*)e->GetDirectBufferAddress(v), e->GetDirectBufferCapacity(y), e->GetDirectBufferCapacity(u),
             e->GetDirectBufferCapacity(v), yRow, yPix, cRow, cPix, w, h};
  AndroidBitmapInfo info;
  void* px = nullptr;
  if (!s.y || !s.u || !s.v || AndroidBitmap_getInfo(e, bitmap, &info) < 0 || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return false;
  if (AndroidBitmap_lockPixels(e, bitmap, &px) < 0) return false;
  const bool ok = yuv_to_rgba(s, rot, (uint8_t*)px, (int)info.stride, (int)info.width, (int)info.height);
  AndroidBitmap_unlockPixels(e, bitmap);
  return ok;
}
jboolean jGripHeld(JNIEnv*, jclass) { return host_grip_held(); }
// Per hand 44 floats: x[21], y[21], right (0 / 1), score.
void jHands(JNIEnv* e, jclass, jfloatArray data, jint n, jdouble t) {
  HandObs h[2];
  n = std::max(0, std::min<int>(n, 2));
  float v[88] = {};
  if (n) e->GetFloatArrayRegion(data, 0, 44 * n, v);
  for (int i = 0; i < n; i++) {
    const float* p = v + 44 * i;
    std::copy(p, p + 21, h[i].x);
    std::copy(p + 21, p + 42, h[i].y);
    h[i].right = p[42] > 0.5f;
    h[i].score = p[43];
  }
  host_hands(h, n, t);
}
void jSurfaceChanged(JNIEnv*, jclass, jint w, jint h) { host_surface_changed(w, h); }
void jSurfaceDestroyed(JNIEnv*, jclass) { host_surface_destroyed(); }
void jForeground(JNIEnv*, jclass, jboolean on) { host_foreground(on); }
void jTouch(JNIEnv*, jclass, jint id, jint kind, jfloat x, jfloat y, jdouble t) { host_touch(id, kind, x, y, t); }
void jThermal(JNIEnv*, jclass, jfloat h, jint status) { host_thermal(h, status); }
void jSetParam(JNIEnv* e, jclass, jstring key, jdouble num, jstring s) {
  const std::string k = str(e, key), v = str(e, s);
  host_set_param(k.c_str(), num, s ? v.c_str() : nullptr);
}
void jSetSystem(JNIEnv* e, jclass, jstring key, jdouble num) { host_set_system(str(e, key).c_str(), num); }
void jMessage(JNIEnv* e, jclass, jstring type, jboolean on, jint id, jdouble num, jstring s, jint w, jint h) {
  const std::string t = str(e, type), v = str(e, s);
  host_message(PageMessage{t.c_str(), (bool)on, id, num, v.c_str(), w, h});
}
void jPermission(JNIEnv*, jclass, jint id, jboolean granted) { host_permission(id, granted); }
void jDebug(JNIEnv* e, jclass, jstring k, jstring v) { host_debug(str(e, k).c_str(), str(e, v).c_str()); }

}  // namespace

extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM* vm, void*) {
  gVm = vm;
  JNIEnv* e = nullptr;
  if (vm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  pthread_key_create(&gDetachKey, [](void*) { gVm->DetachCurrentThread(); });
  jclass c = e->FindClass("app/fractonaut/Host");
  if (!c) return JNI_ERR;
  gHost = (jclass)e->NewGlobalRef(c);
  const JNINativeMethod natives[] = {
      {"start", "(IIIFFZFLjava/lang/String;ZLjava/lang/String;Ljava/lang/String;I)V", (void*)jStart},
      {"cameraTexture", "(Landroid/graphics/SurfaceTexture;F)V", (void*)jCameraTexture},
      {"handsEnabled", "(Z)V", (void*)jHandsEnabled},
      {"cameraAspect", "(F)V", (void*)jCameraAspect},
      {"yuvToBitmap", "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;IIIIIIILandroid/graphics/Bitmap;)Z", (void*)jYuvToBitmap},
      {"gripHeld", "()Z", (void*)jGripHeld},
      {"hands", "([FID)V", (void*)jHands},
      {"surfaceCreated", "(Landroid/view/Surface;)V", (void*)jSurfaceCreated},
      {"surfaceChanged", "(II)V", (void*)jSurfaceChanged},
      {"surfaceDestroyed", "()V", (void*)jSurfaceDestroyed},
      {"foreground", "(Z)V", (void*)jForeground},
      {"touch", "(IIFFD)V", (void*)jTouch},
      {"thermal", "(FI)V", (void*)jThermal},
      {"setParam", "(Ljava/lang/String;DLjava/lang/String;)V", (void*)jSetParam},
      {"setSystem", "(Ljava/lang/String;D)V", (void*)jSetSystem},
      {"message", "(Ljava/lang/String;ZIDLjava/lang/String;II)V", (void*)jMessage},
      {"permission", "(IZ)V", (void*)jPermission},
      {"debug", "(Ljava/lang/String;Ljava/lang/String;)V", (void*)jDebug},
  };
  if (e->RegisterNatives(gHost, natives, sizeof natives / sizeof natives[0]) != JNI_OK) return JNI_ERR;
  J.pageState = e->GetStaticMethodID(gHost, "pageState", "(Ljava/lang/String;)V");
  J.pageEvent = e->GetStaticMethodID(gHost, "pageEvent", "(Ljava/lang/String;Ljava/lang/String;)V");
  J.reply = e->GetStaticMethodID(gHost, "reply", "(IZLjava/lang/String;)V");
  J.haptic = e->GetStaticMethodID(gHost, "haptic", "(I)V");
  J.awake = e->GetStaticMethodID(gHost, "awake", "(Z)V");
  J.requestPermission = e->GetStaticMethodID(gHost, "requestPermission", "(Ljava/lang/String;I)V");
  J.mediaOpen = e->GetStaticMethodID(gHost, "mediaOpen", "(Ljava/lang/String;Ljava/lang/String;)I");
  J.mediaClose = e->GetStaticMethodID(gHost, "mediaClose", "(IZ)V");
  J.share = e->GetStaticMethodID(gHost, "share", "(Ljava/lang/String;)V");
  J.copy = e->GetStaticMethodID(gHost, "copy", "(Ljava/lang/String;)V");
  if (!J.pageState || !J.pageEvent || !J.reply || !J.haptic || !J.awake || !J.requestPermission || !J.mediaOpen ||
      !J.mediaClose || !J.share || !J.copy) return JNI_ERR;
  return JNI_VERSION_1_6;
}
