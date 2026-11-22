// Headless engine test. Build and run:
//   clang++ -std=c++20 -O2 -g -fsanitize=address,undefined mac/core/*.cpp -o mac/build/test_engine && mac/build/test_engine
#include "engine.h"
#include <atomic>
#include <cmath>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

// Count heap allocations so the steady state can be checked for none.
static std::atomic<long> gAllocs{0};
void* operator new(size_t n) { gAllocs++; if (void* p = malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { gAllocs++; if (void* p = malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const double DT = 1.0 / 120;
static double now = 100;                 // host clock, seconds

static bool finite(float v) { return std::isfinite(v); }

static void checkFrame(const Frame& f, const char* where) {
  const float* s = &f.n;
  for (const float* p = s; p <= &f.sheenT; p++) CHECK(finite(*p), "%s: frame scalar %d", where, (int)(p - s));
  const float* fo = &f.fold.spin;
  for (int i = 0; i < 20; i++) CHECK(finite(fo[i]), "%s: fold field %d", where, i);
  CHECK(f.inkCount >= 0 && f.inkCount <= 16384, "%s: inkCount %d", where, f.inkCount);
  CHECK(f.inkCopies >= 1 && f.inkCopies <= 48, "%s: inkCopies %d", where, f.inkCopies);
  for (int i = 0; i < f.inkCount * 8; i++) if (!finite(f.ink[i])) { CHECK(false, "%s: ink[%d] = %f", where, i, f.ink[i]); break; }
  CHECK(f.ripN >= 0 && f.ripN <= 4, "%s: ripN %d", where, f.ripN);
  for (int i = 0; i < f.ripN * 4; i++) CHECK(finite(f.rip[i]), "%s: rip", where);
  for (int i = 0; i < 3; i++) CHECK(finite(f.pal.a[i]) && finite(f.pal.b[i]) && finite(f.pal.c[i]) && finite(f.pal.d[i]), "%s: palette", where);
}

static const Frame* stepN(Engine* e, int n, const char* where, float bass = 0, float mid = 0, float high = 0) {
  const Frame* f = nullptr;
  for (int i = 0; i < n; i++) {
    now += DT;
    f = &engine_step(e, DT, bass, mid, high);
    engine_report_gpu(e, 0.002, DT);
    checkFrame(*f, where);
  }
  return f;
}

// A stroke of `n` samples per frame across the view, in points.
static void strokeFrames(Engine* e, int id, bool mouse, int frames, float w, float h, const char* where) {
  engine_pointer(e, id, PTR_DOWN, w * 0.3f, h * 0.5f, now, mouse);
  for (int i = 0; i < frames; i++) {
    for (int k = 0; k < 4; k++) {
      const double t = (i * 4 + k) / (frames * 4.0);
      engine_pointer(e, id, PTR_MOVE, w * (0.3f + 0.4f * (float)t), h * (0.5f + 0.25f * (float)std::sin(t * 12)), now + k * DT / 4, mouse);
    }
    stepN(e, 1, where);
  }
  engine_pointer(e, id, PTR_UP, w * 0.7f, h * 0.5f, now, mouse);
}

// MediaPipe-style landmarks: wrist at (cx, 0.8), fingers up. open: all five out;
// point: index only; fist: none; pinch: thumb tip on index tip, others out.
enum { OPEN, POINT, FIST, PINCH };
static HandObs hand(float cx, int pose, bool right, float size = 1) {
  HandObs h{}; h.right = right; h.score = 0.9f;
  auto set = [&](int i, float dx, float dy) { h.x[i] = cx + dx * size; h.y[i] = 0.8f + dy * size; };
  set(0, 0, 0);
  set(1, -0.10f, -0.08f); set(2, -0.17f, -0.14f); set(3, -0.23f, -0.18f); set(4, -0.28f, -0.22f);
  const float bx[4] = {-0.06f, 0, 0.06f, 0.12f};
  for (int f = 0; f < 4; f++) {
    const int b = 5 + f * 4;
    const bool up = pose == OPEN || pose == PINCH || (pose == POINT && f == 0);
    set(b, bx[f], -0.2f);
    if (up) { set(b + 1, bx[f], -0.3f); set(b + 2, bx[f], -0.38f); set(b + 3, bx[f], -0.45f); }
    else { set(b + 1, bx[f], -0.27f); set(b + 2, bx[f], -0.22f); set(b + 3, bx[f], -0.17f); }
  }
  if (pose == PINCH) { h.x[4] = h.x[8] + 0.01f * size; h.y[4] = h.y[8] + 0.01f * size; set(3, -0.08f, -0.35f); }
  return h;
}

int main() {
  Engine* e = engine_create();
  const float W = 1440, H = 900;
  engine_resize(e, W, H, 2);
  const Frame* f = stepN(e, 2, "start");
  EngineStats s = engine_stats(e);
  CHECK(s.drawW == 2880 && s.drawH == 1800, "draw %dx%d", s.drawW, s.drawH);
  // 1440x900 at dpr 2 is 5.2 Mpx, just under the desktop budget: dense screens start at level 1.
  CHECK(s.level == 1 && s.ssTaps == 2, "first level %d taps %d", s.level, s.ssTaps);
  CHECK(s.echoScale < 1 && s.echoScale >= 0.6f, "echo scale %f", s.echoScale);
  printf("start: draw %dx%d render %dx%d echo %dx%d (%.3f) level %d taps %d steps %d\n", s.drawW, s.drawH, s.renderW, s.renderH, s.echoW, s.echoH, s.echoScale, s.level, s.ssTaps, s.rmSteps);

  // ---- easing: linear, reaches the target 1/60 s after it arrives ----
  {
    const float from = f->P.fbZoom;
    engine_set_param(e, "fbZoom", 1.02, nullptr);
    f = stepN(e, 1, "ease");
    const float mid = from + (1.02f - from) * 0.5f;
    CHECK(std::fabs(f->P.fbZoom - mid) < 1e-5f, "half way %f vs %f", f->P.fbZoom, mid);
    f = stepN(e, 1, "ease");
    CHECK(f->P.fbZoom == 1.02f, "reached %f", f->P.fbZoom);
    engine_set_param(e, "symmetry", 9, nullptr);
    engine_set_param(e, "palette", 0, "ice");
    engine_set_param(e, "mirror", 0, nullptr);
    f = stepN(e, 1, "snap");
    CHECK(f->P.symmetry == 9 && f->P.palette == 8 && !f->P.mirror, "stepped params snap");
    CHECK(f->fold.sym == 9 && f->inkCopies == 9, "fold sym %f copies %d", f->fold.sym, f->inkCopies);
    engine_set_param(e, "mirror", 1, nullptr);
    engine_set_param(e, "nonsense", 1, nullptr);
    engine_set_param(e, "mode", 0, "bogus");
  }

  // ---- every mode, with strokes from mouse and touch, autopilot pens and brushes ----
  const char* modes[] = {"none", "kaleido", "kifs", "hyperbolic", "droste", "tiles"};
  const char* brushes[] = {"neon", "dots", "sparks"};
  const char* paths[] = {"lissajous", "rose", "noise", "spiral"};
  const char* colors[] = {"cycle", "speed", "angle", "solid"};
  int maxInk = 0;
  for (int m = 0; m < 6; m++) {
    engine_set_param(e, "mode", 0, modes[m]);
    engine_set_param(e, "brush", 0, brushes[m % 3]);
    engine_set_param(e, "autoPens", 4 + m % 5, nullptr);
    engine_set_param(e, "autoPath", 0, paths[m % 4]);
    engine_set_param(e, "inkColor", 0, colors[m % 4]);
    engine_set_param(e, "warpAmount", m == 1 ? 0.4 : 0, nullptr);
    engine_set_param(e, "fisheye", m % 2 ? 0.5 : -0.3, nullptr);
    f = stepN(e, 2, modes[m]);
    CHECK(f->fold.mode == m, "mode %d", f->fold.mode);
    strokeFrames(e, 1, true, 60, W, H, modes[m]);
    if (f->inkCount > maxInk) maxInk = f->inkCount;
    strokeFrames(e, 7, false, 60, W, H, modes[m]);
    for (int i = 0; i < 60; i++) { f = stepN(e, 1, modes[m]); if (f->inkCount > maxInk) maxInk = f->inkCount; }
    // mouse hovering (inkHover) outside a stroke
    for (int i = 0; i < 30; i++) { engine_pointer(e, 1, PTR_MOVE, W * 0.5f + i * 5, H * 0.4f, now, true); stepN(e, 1, modes[m]); }
    engine_pointer(e, 1, PTR_LEAVE, 0, 0, now, true);
  }
  s = engine_stats(e);
  printf("modes: max ink %d, particles %d, painted %.2f, energy %.2f\n", maxInk, s.particles, s.painted, s.energy);
  CHECK(maxInk > 0, "something was drawn");
  CHECK(s.painted > 1, "painted %f", s.painted);
  CHECK(s.particles > 0 && s.particles <= 4096, "particles %d", s.particles);

  // ---- tap: ripple, sparks, pluck and haptic events ----
  {
    engine_set_param(e, "mode", 0, "kifs");
    EngineEvent ev; while (engine_poll_event(e, &ev)) {}
    for (int i = 0; i < 6; i++) {
      engine_pointer(e, 3, PTR_DOWN, 300 + i * 100, 400, now, false);
      stepN(e, 2, "tap");
      engine_pointer(e, 3, PTR_UP, 302 + i * 100, 401, now, false);
      stepN(e, 2, "tap");
    }
    f = stepN(e, 1, "tap");
    s = engine_stats(e);
    CHECK(s.taps == 6, "taps %d", s.taps);
    CHECK(f->ripN == 4, "ripples capped at 4: %d", f->ripN);
    int plucks = 0, haptics = 0;
    while (engine_poll_event(e, &ev)) { plucks += ev.type == EV_PLUCK; haptics += ev.type == EV_HAPTIC; }
    CHECK(plucks >= 6 && haptics == 6, "plucks %d haptics %d", plucks, haptics);
    stepN(e, 400, "ripples fade");
    CHECK(engine_stats(e).ripples == 0, "ripples fade after 3 s");
  }

  // ---- two touches, trackpad twist and pinch, scroll ----
  {
    const Frame* f0 = stepN(e, 1, "twist");
    const float spin0 = f0->fold.spin;
    engine_pointer(e, 10, PTR_DOWN, 600, 450, now, false);
    engine_pointer(e, 11, PTR_DOWN, 840, 450, now, false);
    for (int i = 1; i <= 30; i++) {
      const double a = i * 0.03, r = 120 + i * 3;
      engine_pointer(e, 11, PTR_MOVE, 720 + (float)(r * std::cos(a)), 450 + (float)(r * std::sin(a)), now, false);
      engine_pointer(e, 10, PTR_MOVE, 720 - (float)(r * std::cos(a)), 450 - (float)(r * std::sin(a)), now, false);
      stepN(e, 1, "twist");
    }
    engine_pointer(e, 10, PTR_UP, 0, 0, now, false);
    engine_pointer(e, 11, PTR_UP, 0, 0, now, false);
    f = stepN(e, 30, "twist");
    CHECK(std::fabs(f->fold.spin - spin0) > 0.3f, "two-finger twist turned the glass: %f", f->fold.spin - spin0);
    CHECK(engine_stats(e).taps == 6, "a twist is not a tap");

    const float spin1 = f->fold.spin;
    engine_gesture_begin(e, now);
    for (int i = 0; i < 30; i++) { engine_rotate(e, 0.03f, now); engine_magnify(e, 0.02f, now); stepN(e, 1, "gesture"); }
    f = stepN(e, 1, "gesture");
    CHECK(f->fbZoom > 1.0f, "pinch out dives in: fbZoom %f", f->fbZoom);
    engine_gesture_end(e);
    f = stepN(e, 30, "gesture");
    // Counterclockwise on the trackpad turns the glass counterclockwise on screen, which is
    // a negative angle in the web's y-down maths and a positive one in p-space (y up).
    CHECK(f->fold.spin - spin1 > 0.3f, "trackpad twist turned the glass: %f", f->fold.spin - spin1);

    for (int i = 0; i < 20; i++) { engine_scroll(e, 0, -40, false); engine_scroll(e, 30, 0, true); stepN(e, 1, "scroll"); }
    stepN(e, 240, "coast");
  }

  // ---- hands ----
  {
    engine_set_hands_enabled(e, true, now);
    engine_set_param(e, "mode", 0, "kaleido");
    const float p0 = engine_stats(e).painted;
    // One open hand sweeping: five painting fingertips.
    for (int i = 0; i < 120; i++) {
      HandObs h = hand(0.35f + 0.3f * std::sin(i * 0.05f), OPEN, true);
      if (i % 4 == 0) engine_hands(e, &h, 1, now);
      stepN(e, 1, "open hand");
    }
    CHECK(engine_stats(e).painted > p0 + 0.5f, "open hand painted %f", engine_stats(e).painted - p0);

    // Pinch and turn: one hand holds the glass like a dial.
    const float spin0 = stepN(e, 1, "pinch")->fold.spin;
    for (int i = 0; i < 90; i++) {
      HandObs h = hand(0.5f, PINCH, false);
      const float a = i * 0.012f;   // rotate the landmarks about the wrist
      for (int k = 1; k < 21; k++) {
        const float dx = h.x[k] - h.x[0], dy = h.y[k] - h.y[0];
        h.x[k] = h.x[0] + dx * std::cos(a) - dy * std::sin(a);
        h.y[k] = h.y[0] + dx * std::sin(a) + dy * std::cos(a);
      }
      if (i % 4 == 0) engine_hands(e, &h, 1, now);
      stepN(e, 1, "pinch");
      if (i == 40) { HandsState hs = engine_hands_state(e); CHECK(hs.grabMode == 1 && hs.grabOn[0] && !hs.grabOn[1], "one hand grips: mode %d", hs.grabMode); }
    }
    f = stepN(e, 1, "pinch");
    CHECK(std::fabs(f->fold.spin - spin0) > 0.1f, "pinched hand turned the glass: %f", f->fold.spin - spin0);

    // Pinch and grow (moving toward the camera): dives.
    for (int i = 0; i < 60; i++) {
      HandObs h = hand(0.5f, PINCH, false, 1 + i * 0.01f);
      if (i % 4 == 0) engine_hands(e, &h, 1, now);
      stepN(e, 1, "pinch dive");
    }
    // Two pinching hands, then two still open palms: next chamber.
    for (int i = 0; i < 40; i++) {
      HandObs hh[2] = {hand(0.3f, PINCH, false), hand(0.7f + i * 0.002f, PINCH, true)};
      if (i % 4 == 0) engine_hands(e, hh, 2, now);
      stepN(e, 1, "two grips");
    }
    CHECK(engine_hands_state(e).grabMode == 2, "both hands grip");
    EngineEvent ev; while (engine_poll_event(e, &ev)) {}
    int chambers = 0;
    for (int i = 0; i < 180; i++) {
      HandObs hh[2] = {hand(0.3f, OPEN, false), hand(0.7f, OPEN, true)};
      if (i % 4 == 0) engine_hands(e, hh, 2, now);
      stepN(e, 1, "palms");
      while (engine_poll_event(e, &ev)) chambers += ev.type == EV_NEXT_CHAMBER;
    }
    CHECK(chambers == 1, "two still palms step to the next chamber once: %d", chambers);
    // Fist and pointing.
    for (int i = 0; i < 60; i++) {
      HandObs hh[2] = {hand(0.3f, FIST, false), hand(0.6f + i * 0.003f, POINT, true)};
      if (i % 4 == 0) engine_hands(e, hh, 2, now);
      stepN(e, 1, "fist");
    }
    // Hands leave, then tracking turns off.
    engine_hands(e, nullptr, 0, now);
    stepN(e, 30, "gone");
    CHECK(engine_hands_state(e).grabMode == 0, "grip released");
    engine_set_hands_enabled(e, false, now);
    stepN(e, 2, "off");
    CHECK(!engine_hands_state(e).hands, "hands off");
  }

  // ---- breathing guide, mic levels, camera flag ----
  {
    engine_set_system(e, "breathGuide", 1);
    engine_set_cam_on(e, true);
    EngineEvent ev; while (engine_poll_event(e, &ev)) {}
    stepN(e, 120 * 21, "breath", 0.6f, 0.4f, 0.8f);
    int cues = 0;
    while (engine_poll_event(e, &ev)) cues += ev.type == EV_BREATH_CUE;
    CHECK(cues >= 4 && cues <= 5, "breath cues over 21 s: %d", cues);
    s = engine_stats(e);
    CHECK(s.lung >= 0 && s.lung <= 1, "lung %f", s.lung);
    CHECK(f->camOn, "cam on");
    engine_set_system(e, "breathGuide", 0);
    engine_reset_breathing(e);
    stepN(e, 2, "breath off");
    CHECK(engine_stats(e).lung == -1, "lung off");
  }

  // ---- detail ladder: GPU heavy steps down, light climbs back, thermal caps ----
  {
    // A GPU whose time follows the detail: pixels rendered and supersampling taps.
    auto run = [&](int frames, double base) {
      for (int i = 0; i < frames; i++) {
        now += DT; engine_step(e, DT, 0, 0, 0);
        const EngineStats st = engine_stats(e);
        const double px = (double)st.renderW * st.renderH / ((double)st.drawW * st.drawH);
        engine_report_gpu(e, base * DT * px * (0.6 + 0.1 * st.ssTaps), DT);
      }
      return engine_stats(e);
    };
    const int l0 = engine_stats(e).level;
    s = run(120 * 12, 1.3);
    CHECK(s.level > l0 && s.gpuLoad <= 0.8, "heavy GPU steps detail down: %d -> %d (load %.2f)", l0, s.level, s.gpuLoad);
    CHECK(s.renderW < s.drawW || s.ssTaps < 4, "less detail");
    const int heavy = s.level;
    s = run(120 * 12, 1.3);
    CHECK(s.level == heavy, "settled, no oscillation: %d -> %d", heavy, s.level);
    // CPU-bound: missed frames with an idle GPU leave detail alone.
    // (The first second mixes both phases, so it is let through before measuring.)
    auto cpuBound = [&](int frames) { for (int i = 0; i < frames; i++) { now += DT; engine_step(e, DT * 2, 0, 0, 0); engine_report_gpu(e, DT * 0.1, DT); } };
    cpuBound(120);
    const int before = engine_stats(e).level;
    cpuBound(120 * 8);
    CHECK(engine_stats(e).level == before, "CPU-bound does not drop detail: %d -> %d", before, engine_stats(e).level);
    s = run(120 * 40, 0.5);
    CHECK(s.level < heavy, "light GPU climbs back: %d -> %d (load %.2f)", heavy, s.level, s.gpuLoad);
    engine_set_thermal(e, 3);
    stepN(e, 2, "thermal");
    CHECK(engine_stats(e).level >= 5, "critical thermal caps detail: %d", engine_stats(e).level);
    engine_set_thermal(e, 0);
    engine_set_system(e, "autoQuality", 0);
    stepN(e, 130, "manual");
    s = engine_stats(e);
    CHECK(s.quality == 1 && s.ssTaps == 4, "autoQuality off: full detail");
    engine_set_system(e, "autoQuality", 1);
    printf("ladder: level %d fps %.1f refresh %.0f gpu %.2f render %dx%d\n", s.level, s.fps, s.refreshHz, s.gpuLoad, s.renderW, s.renderH);
  }

  // ---- paused, clear ----
  {
    engine_set_paused(e, true);
    const Frame& a = engine_step(e, DT, 0, 0, 0);
    const float g = a.genT;
    engine_step(e, DT, 0, 0, 0);
    CHECK(a.genT == g, "paused frames do not advance");
    engine_set_paused(e, false);
    engine_clear_pens(e);
    stepN(e, 1, "clear");
    CHECK(engine_stats(e).ripples == 0, "clear");
  }

  // ---- steady state: no allocations while posting and stepping ----
  {
    engine_set_param(e, "autoPens", 8, nullptr);
    engine_set_param(e, "brush", 0, "sparks");
    stepN(e, 10, "warm");
    const long a0 = gAllocs.load();
    for (int i = 0; i < 600; i++) {
      engine_set_param(e, "fbRotate", std::sin(i * 0.1) * 30, nullptr);
      engine_pointer(e, 1, PTR_MOVE, 200 + i, 300 + (i % 50), now, true);
      HandObs h = hand(0.4f, OPEN, true);
      engine_hands(e, &h, 1, now);
      stepN(e, 1, "steady");
      EngineEvent ev; while (engine_poll_event(e, &ev)) {}
    }
    CHECK(gAllocs.load() == a0, "allocations in steady state: %ld", gAllocs.load() - a0);
  }

  // ---- threads: posts from other threads while the render thread steps ----
  {
    std::atomic<bool> done{false};
    const double t0 = now;   // other threads post with their own clock reading
    std::thread ui([&] { for (int i = 0; !done; i++) { engine_set_param(e, "spinSpeed", (i % 90) - 45, nullptr); engine_pointer(e, 2, i % 3 ? PTR_MOVE : PTR_DOWN, (float)(i % 1400), 400, t0 + i * 1e-4, false); engine_stats(e); engine_hands_state(e); std::this_thread::yield(); } });
    std::thread cam([&] { for (int i = 0; !done; i++) { HandObs h = hand(0.5f, i % 2 ? OPEN : PINCH, false); engine_hands(e, &h, 1, t0 + i * 0.03); std::this_thread::yield(); } });
    std::thread gpu([&] { while (!done) { engine_report_gpu(e, 0.003, DT); EngineEvent ev; engine_poll_event(e, &ev); std::this_thread::yield(); } });
    engine_set_hands_enabled(e, true, now);
    for (int i = 0; i < 1200; i++) { now += DT; checkFrame(engine_step(e, DT, 0.1f, 0.1f, 0.1f), "threads"); }
    done = true; ui.join(); cam.join(); gpu.join();
  }

  // ---- a hand in a sky moves smoothly: the camera sees it 15 times a second, the sky
  // runs at 120, and the finger's speed in the sky must change from frame to frame by only
  // a small part of itself (no stair steps, no lurches) ----
  {
    Engine* e = engine_create();   // its own engine: no pointers left over from the tests above
    engine_resize(e, W, H, 2);
    engine_set_hands_enabled(e, true, now);
    for (auto kv : {std::pair<const char*, const char*>{"mode", "none"}, {"starKind", "galaxy"}, {"mirror", "false"}})
      engine_set_param(e, kv.first, 0, kv.second);
    engine_set_param(e, "stars", 1, nullptr); engine_set_param(e, "symmetry", 4, nullptr); engine_set_param(e, "spinSpeed", 0, nullptr);
    stepN(e, 120, "sky");
    double vPrev[2] = {0, 0}, sumV = 0, maxJump = 0; int n = 0;
    for (int i = 0; i < 480; i++) {
      if (i % 8 == 0) { HandObs h = hand(0.62f + 0.12f * std::sin(i * 0.006f), POINT, true); engine_hands(e, &h, 1, now); }
      const Frame* fr = stepN(e, 1, "hand in the sky");
      if (fr->starCores <= fr->starAuto) continue;
      const float* v = fr->starVel + fr->starAuto * 2;
      if (i > 120 && n > 0) {
        sumV += std::hypot(v[0], v[1]);
        maxJump = std::max(maxJump, std::hypot(v[0] - vPrev[0], v[1] - vPrev[1]));
      }
      vPrev[0] = v[0]; vPrev[1] = v[1]; n++;
    }
    const double meanV = sumV / std::max(1, n - 121);
    printf("hand in a sky: mean finger speed %.4f, largest change in one frame %.4f (%.0f%%)\n", meanV, maxJump, 100 * maxJump / std::max(meanV, 1e-9));
    CHECK(n > 300, "the finger stays in the sky (%d frames)", n);
    CHECK(maxJump < 0.12 * meanV, "the finger's speed changes smoothly (largest step %.0f%% of its mean speed)", 100 * maxJump / std::max(meanV, 1e-9));
    engine_destroy(e);
  }

  // ---- hand looks: each look builds sane batches from a tracked hand, without allocating ----
  {
    engine_set_hands_enabled(e, true, now);
    engine_set_system(e, "handLookOn", 1);
    engine_set_param(e, "mode", 0, "kaleido");
    for (int look = 1; look <= 5; look++) {
      engine_set_system(e, "handLook", look);
      const LookData* L = nullptr;
      long a0 = 0;
      for (int i = 0; i < 240; i++) {
        if (i == 120) a0 = gAllocs.load();
        HandObs h = hand(0.4f + 0.2f * std::sin(i * 0.05f), i % 90 < 60 ? OPEN : PINCH, true);
        if (i % 4 == 0) engine_hands(e, &h, 1, now);
        L = stepN(e, 1, "looks")->looks;
      }
      CHECK(gAllocs.load() == a0, "look %d allocates in steady state: %ld", look, gAllocs.load() - a0);
      CHECK(L != nullptr, "look %d: nothing built for a tracked hand", look);
      if (!L) continue;
      CHECK(L->look == look && L->nSeg <= LOOK_MAX_SEG && L->nVert <= LOOK_MAX_VERT && L->nStrip <= LOOK_MAX_STRIP, "look %d: counts", look);
      CHECK(look == 5 ? L->auraN == 1 : L->nSeg > 0, "look %d: segs %d aura %d", look, L->nSeg, L->auraN);
      if (look == 3 || look == 4) CHECK(L->nStrip == 5, "look %d: %d strips", look, L->nStrip);
      bool ok = true;
      for (int i = 0; i < L->nSeg * 12 && ok; i++) ok = finite(L->seg[i]);
      for (int i = 0; i < L->nVert * 6 && ok; i++) ok = finite(L->vert[i]);
      CHECK(ok, "look %d: a value is not a number", look);
    }
    engine_set_system(e, "handLookOn", 0);
    CHECK(stepN(e, 1, "looks off")->looks == nullptr, "no look once it is switched off");
    engine_set_hands_enabled(e, false, now);
  }

  // ---- gestures: a hand acts on the picture ----
  {
    engine_set_hands_enabled(e, true, now);
    engine_set_param(e, "mode", 0, "kaleido");
    double t = now;
    auto feed = [&](const HandObs* hs, int n, int frames) { for (int i = 0; i < frames; i++) { t += 1 / 30.0; engine_hands(e, hs, n, t); now += 1 / 30.0; stepN(e, 4, "gestures"); } };
    HandObs open = hand(0.5f, OPEN, true), pinch = hand(0.5f, PINCH, true);
    feed(&open, 1, 10);
    int taps = engine_stats(e).taps;
    feed(&pinch, 1, 3); feed(&open, 1, 3);
    CHECK(engine_stats(e).taps == taps + 1, "a quick pinch and release taps (%d taps)", engine_stats(e).taps - taps);

    taps = engine_stats(e).taps;
    feed(&pinch, 1, 12);   // held: no tap
    for (int i = 0; i < 4; i++) { HandObs h = hand(0.5f, PINCH, true, 1 + 0.12f * (i + 1)); feed(&h, 1, 1); }
    CHECK(engine_stats(e).taps > taps, "a pinched hand punched toward the camera bursts");
    feed(&open, 1, 30);

    // An open hand sweeping across stirs the picture (a drag in a ripple slot).
    int stirred = 0;
    for (int i = 0; i < 20; i++) {
      HandObs h = hand(0.3f + i * 0.02f, OPEN, true);
      t += 1 / 30.0; engine_hands(e, &h, 1, t); now += 1 / 30.0;
      for (int k = 0; k < 4; k++) { const Frame* f = stepN(e, 1, "stir"); stirred = std::max(stirred, f->ripN - engine_stats(e).ripples); }
    }
    CHECK(stirred > 0, "an open hand on the move stirs the picture");

    // Two pinched hands: the bridge charges as they stretch and snaps when both let go.
    HandObs two[2] = {hand(0.35f, PINCH, false), hand(0.65f, PINCH, true)};
    feed(two, 2, 6);
    for (int i = 0; i < 10; i++) { two[0] = hand(0.35f - i * 0.012f, PINCH, false); two[1] = hand(0.65f + i * 0.012f, PINCH, true); feed(two, 2, 1); }
    CHECK(stepN(e, 1, "bridge")->looks != nullptr, "the bridge of light is drawn while two hands hold");
    taps = engine_stats(e).taps;
    HandObs rel[2] = {hand(0.23f, OPEN, false), hand(0.77f, OPEN, true)};
    feed(rel, 2, 3);
    CHECK(engine_stats(e).taps > taps, "letting go of both hands snaps the bridge");
    engine_set_hands_enabled(e, false, now);
  }

  // ---- the 3D shapes: a drag flies over the surface and glides on; a dive sinks closer ----
  {
    engine_pointer(e, 2, PTR_UP, 0, 400, now, false);   // the threads test leaves its finger down
    engine_set_param(e, "gen", 0, "fractal3d");
    engine_set_param(e, "rmShape", 0, "bulb");
    stepN(e, 10, "3d");
    const float yaw0 = stepN(e, 1, "3d")->rmView[0];
    engine_pointer(e, 3, PTR_DOWN, 600, 400, now, false);
    for (int i = 1; i <= 30; i++) { engine_pointer(e, 3, PTR_MOVE, 600 + i * 8, 400, now + i * DT, false); stepN(e, 1, "turn"); }
    engine_pointer(e, 3, PTR_UP, 840, 400, now + 31 * DT, false);
    const float yaw1 = stepN(e, 1, "3d")->rmView[0];
    // Over a surface the ground follows the finger: a drag to the right flies the other way.
    CHECK(yaw1 - yaw0 < -0.03f, "dragging moves over the surface with the finger (yaw %f -> %f)", yaw0, yaw1);
    const float yaw2 = stepN(e, 30, "spin on")->rmView[0];
    CHECK(std::fabs(yaw2 - yaw1) > 0.01f, "the shape spins on after a flick");
    const float z0 = stepN(e, 1, "3d")->rmView[2];
    for (int i = 0; i < 20; i++) { engine_scroll(e, 0, -40, false); stepN(e, 1, "dive"); }
    CHECK(std::fabs(stepN(e, 20, "dive")->rmView[2] - z0) > 0.01f, "a dive zooms the shape");
    engine_set_param(e, "gen", 0, "none");
  }

  // ---- every parameter applies, past the 64th (the dirty flags) ----
  {
    engine_set_param(e, "exposure", 1.7, nullptr);
    engine_set_param(e, "vignette", 0.9, nullptr);
    const Frame* f = stepN(e, 4, "late params");
    CHECK(std::fabs(f->P.exposure - 1.7f) < 1e-4f && std::fabs(f->P.vignette - 0.9f) < 1e-4f,
          "exposure %f vignette %f", f->P.exposure, f->P.vignette);
  }

  // ---- a copy steps on its own; the live engine is untouched ----
  {
    Engine* c = engine_clone(e);
    const float spin0 = engine_step(e, 0, 0, 0, 0).fold.spin;
    for (int i = 0; i < 60; i++) checkFrame(engine_step(c, DT, 0.2f, 0.2f, 0.2f), "clone");
    CHECK(engine_step(e, 0, 0, 0, 0).fold.spin == spin0, "the live engine did not move while its copy stepped");
    engine_set_param(c, "exposure", 0.5, nullptr);
    CHECK(std::fabs(stepN(e, 2, "after clone")->P.exposure - 1.7f) < 1e-4f, "a copy's settings stay its own");
    engine_destroy(c);
  }

  engine_destroy(e);
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
