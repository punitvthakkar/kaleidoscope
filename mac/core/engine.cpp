// Web: Main (step, the detail ladder, the breathing guide) plus the native inbox,
// parameter easing and outbound events.
#include "engine_impl.h"
#include "rmflight.h"
#include <cstring>
#include <new>

// Detail ladder, cheapest savings first: supersampling taps go before resolution,
// because on a dense screen fewer taps are invisible and a softer picture is not.
static const float LEVELS[10][2] = {{4, 1}, {2, 1}, {1, 1}, {1, 0.85f}, {1, 0.72f}, {1, 0.6f}, {1, 0.5f}, {1, 0.42f}, {1, 0.35f}, {1, 0.3f}};
static const int NLEVELS = 10;
// Highest detail allowed per thermal state (nominal, fair, serious, critical).
static const int THERMAL_MIN[4] = {0, 1, 3, 5};

static const double EASE = 1.0 / 60;   // params arrive at ~60 Hz; reach each target in one interval

// Points the modules at their own engine's parts (after creating or copying it).
static void wire(Engine* e) {
  Pens& p = e->pens;
  p.P = &e->P; p.fs = &e->fs; p.motion = &e->motion; p.pal = &e->pal; p.events = &e->events; p.rng = &e->rng;
  if (p.fr) p.fr = &e->f;
  Hands& h = e->hands;
  h.pens = &e->pens; h.motion = &e->motion; h.events = &e->events;
  e->f.ink = p.data; e->f.rip = p.rip;
}

Engine* engine_create() {
  Engine* e = new Engine();
  Pens& p = e->pens;
  for (int k = 0; k < 10; k++) { p.hands[k] = Pen(); p.hands[k].kind = Pen::HAND; p.hands[k].hue = p.hands[k].offset = k * 0.11; }
  wire(e);
  static_assert(sizeof(PaletteState) == sizeof(PALETTES[0]));
  memcpy(&e->pal, PALETTES[0], sizeof(PaletteState));
  return e;
}

Engine* engine_clone(Engine* src) {
  Engine* e = new Engine(*src);
  wire(e);
  return e;
}

void engine_destroy(Engine* e) { delete e; }

// ---------------- posts (any thread) ----------------
void engine_set_param(Engine* e, const char* key, double num, const char* str) {
  const ParamInfo* pi = param_find(key);
  if (!pi) return;
  std::lock_guard<std::mutex> l(e->in.m);
  if (params_set(e->in.P, pi, num, str)) e->in.dirty.set(pi - PARAM_INFO);
}

void engine_set_system(Engine* e, const char* key, double num) {
  std::lock_guard<std::mutex> l(e->in.m);
  if (system_set(e->in.sys, key, num)) e->in.sysDirty = true;
}

static void post(Engine* e, const InputEv& ev) {
  std::lock_guard<std::mutex> l(e->in.m);
  if (e->in.qn < Engine::Inbox::QCAP) e->in.q[e->in.qn++] = ev;
}

void engine_pointer(Engine* e, int id, int kind, float x, float y, double t, bool isMouse) { post(e, {InputEv::POINTER, id, kind, x, y, t, isMouse}); }
void engine_scroll(Engine* e, float dx, float dy, bool shift) { post(e, {InputEv::SCROLL, 0, 0, dx, dy, 0, shift}); }
void engine_gesture_begin(Engine* e, double t) { post(e, {InputEv::GBEGIN, 0, 0, 0, 0, t, false}); }
void engine_magnify(Engine* e, float m, double t) { post(e, {InputEv::GMAGNIFY, 0, 0, m, 0, t, false}); }
void engine_rotate(Engine* e, float radians, double t) { post(e, {InputEv::GROTATE, 0, 0, radians, 0, t, false}); }
void engine_gesture_end(Engine* e) { post(e, {InputEv::GEND, 0, 0, 0, 0, 0, false}); }

void engine_hands(Engine* e, const HandObs* hands, int n, double t) {
  auto& in = e->in;
  std::lock_guard<std::mutex> l(in.m);
  if (in.hn == Engine::Inbox::HCAP) { in.hHead = (in.hHead + 1) % Engine::Inbox::HCAP; in.hn--; }   // drop the oldest
  auto& hf = in.hq[(in.hHead + in.hn++) % Engine::Inbox::HCAP];
  hf.n = std::max(0, std::min(2, n)); hf.t = t;
  for (int i = 0; i < hf.n; i++) hf.h[i] = hands[i];
}

void engine_set_hands_enabled(Engine* e, bool on, double now) { std::lock_guard<std::mutex> l(e->in.m); e->in.handsEnable = on; e->in.handsEnableAt = now; }
void engine_set_two_hands(Engine* e, bool on) { std::lock_guard<std::mutex> l(e->in.m); e->in.twoHands = on; }
void engine_set_camera_aspect(Engine* e, float a) { std::lock_guard<std::mutex> l(e->in.m); if (a > 0) e->in.camAspect = a; }
void engine_clear_pens(Engine* e) { std::lock_guard<std::mutex> l(e->in.m); e->in.clearPens = true; }
void engine_reset_breathing(Engine* e) { std::lock_guard<std::mutex> l(e->in.m); e->in.resetBreath = true; }
void engine_set_paused(Engine* e, bool v) { std::lock_guard<std::mutex> l(e->in.m); e->in.paused = v; }
void engine_set_cam_on(Engine* e, bool v) { std::lock_guard<std::mutex> l(e->in.m); e->in.camOn = v; }
void engine_set_thermal(Engine* e, int s) { std::lock_guard<std::mutex> l(e->in.m); e->in.thermal = std::max(0, std::min(3, s)); }

void engine_resize(Engine* e, float w, float h, float scale) {
  std::lock_guard<std::mutex> l(e->in.m);
  e->in.viewW = w; e->in.viewH = h; e->in.scale = scale > 0 ? scale : 1; e->in.resized = true;
}

void engine_report_gpu(Engine* e, double gpu, double interval) {
  std::lock_guard<std::mutex> l(e->in.m);
  if (gpu >= 0 && gpu < 1) { e->in.gpuSum += gpu; e->in.gpuN++; }
  if (interval > 0) e->in.frameInterval = interval;
}

bool engine_poll_event(Engine* e, EngineEvent* out) { return e->events.pop(out); }

EngineStats engine_stats(Engine* e) { std::lock_guard<std::mutex> l(e->statsM); return e->stats; }
HandsState engine_hands_state(Engine* e) { std::lock_guard<std::mutex> l(e->statsM); return e->handsState; }

// ---------------- detail ladder ----------------
static int minLevel(Engine* e) { return e->sys.autoQuality ? THERMAL_MIN[e->thermal] : 0; }

static void applyLevel(Engine* e) {
  const bool on = e->sys.autoQuality;
  e->quality = on ? LEVELS[e->level][1] : 1;
  e->ssTaps = on ? (int)LEVELS[e->level][0] : 4;
  e->rmSteps = e->level >= 4 && on ? 56 : 76;
}

static void setLevel(Engine* e, int l) {
  e->level = std::max(minLevel(e), std::min(NLEVELS - 1, l));
  applyLevel(e);
}

static bool halved(Engine* e) { return e->sys.coolHands && e->hands.enabled; }

// Canvas size (drawable) and the renderer's size; web Main.resize without the film cap.
static void resize(Engine* e) {
  if (e->viewW <= 0 || e->viewH <= 0) return;
  const double dpr = e->scale;
  const double k = std::min(1.0, e->sys.maxDpr / dpr);
  const double fw = jsround(e->viewW * dpr), fh = jsround(e->viewH * dpr);
  e->drawW = (int)std::max(1.0, jsround(fw * k)); e->drawH = (int)std::max(1.0, jsround(fh * k));
  // Echo detail tops out at 1.5 device pixels per point; past that it costs memory
  // and fill rate for softness nobody can see.
  auto sizes = [&](double quality, int& w, int& h, int& fw, int& fh) {
    const double s = e->sys.renderScale * quality;
    const double density = e->drawW * s / std::max(1.0f, e->viewW);
    const double fs = density > 1.5 ? std::max(0.6, 1.5 / density) : 1;
    w = (int)std::max(16.0, jsround(e->drawW * s)); h = (int)std::max(16.0, jsround(e->drawH * s));
    fw = (int)std::max(16.0, jsround(w * fs)); fh = (int)std::max(16.0, jsround(h * fs));
    return fs;
  };
  e->FS = (float)sizes(e->quality, e->W, e->H, e->FW, e->FH);
  // Quality 1 is the top of the ladder, and the echo grows with it: the renderer allocates
  // these once per window size and draws each level into part of them.
  sizes(1.0, e->capW, e->capH, e->capFW, e->capFH);
}

// Start near where this screen will settle, instead of stuttering down to it.
static void firstLevel(Engine* e) {
  const double dpr = std::min((double)e->scale, (double)e->sys.maxDpr);
  const double px = (double)e->viewW * e->viewH * dpr * dpr;
  const double budget = 5.2e6;
  e->level = dpr >= 2.75 ? 2 : dpr >= 1.75 ? 1 : 0;   // the denser the screen, the less supersampling it needs
  if (px > budget) { e->level = 2; while (e->level < 6 && px * LEVELS[e->level][1] * LEVELS[e->level][1] > budget) e->level++; }
  e->ceilingFor = 0;
  setLevel(e, e->level);
}

// Once a second: step detail down when frames are missed or the GPU uses more than 80%
// of the frame, back up after four good seconds with the GPU under 60%. (GPUs clock down
// when lightly loaded, so a frame-paced load seldom reads under about half even with
// room to spare; a climb that overshoots is undone and remembered as a ceiling.)
static void adapt(Engine* e, double dt) {
  e->fpsAcc += dt; e->fpsFrames++;
  if (e->fpsAcc < 1) return;
  const double fps = e->fpsFrames / e->fpsAcc;
  e->fpsAcc = 0; e->fpsFrames = 0;
  e->fps = (float)fps;
  const bool haveGpu = e->gpuN > 0;
  const double hz = halved(e) ? e->refreshHz / 2 : e->refreshHz;
  const double load = haveGpu ? e->gpuSum / e->gpuN * hz : 0;
  e->gpuSum = 0; e->gpuN = 0; e->gpuLoad = load;
  if (!e->sys.autoQuality) { applyLevel(e); return; }
  e->cooldown--; e->ceilingFor--;
  // Missed frames only count against detail when the GPU is busy enough to be the cause.
  const bool slow = (haveGpu && load > 0.8) || (fps < hz * 0.9 && (!haveGpu || load > 0.5));
  if (e->trial.on && slow && fps < e->trial.fps * 1.05 && (!haveGpu || load > e->trial.gpu * 0.9)) {
    // Dropping detail bought nothing, so the GPU is not what is holding us back.
    const int back = e->trial.level; e->trial.on = false; e->cooldown = 8;
    setLevel(e, back);
  } else if (slow && e->cooldown <= 0) {
    if (e->good < 0) { e->ceiling = e->level + 1; e->ceilingFor = 45; }   // we only just climbed here: remember it
    e->trial = {true, e->level, fps, load}; e->good = 0;
    setLevel(e, e->level + (fps < hz * 0.6 || load > 1.2 ? 2 : 1));
  } else if (!slow) {
    e->trial.on = false;
    const int floor = std::max(e->ceilingFor > 0 ? e->ceiling : 0, minLevel(e));
    if (fps > hz * 0.97 && (!haveGpu || load < 0.6) && e->level > floor && ++e->good >= 4) { e->good = -3; setLevel(e, e->level - 1); }
    else if (e->good < 0) e->good++;
  }
}

// ---------------- inbox ----------------
static void ease(Engine* e, double dt) {
  for (int i = 0; i < PARAM_COUNT; i++) {
    if (e->rampLeft[i] <= 0) continue;
    float& v = *(float*)((char*)&e->P + PARAM_INFO[i].offset);
    const float t = *(const float*)((const char*)&e->target + PARAM_INFO[i].offset);
    if (dt >= e->rampLeft[i]) { v = t; e->rampLeft[i] = 0; }
    else { v += (float)((t - v) * dt / e->rampLeft[i]); e->rampLeft[i] -= dt; }
  }
}

static void drain(Engine* e) {
  auto& in = e->in;
  int qn, hn, handsEnable; double handsAt;
  bool clearPens, resetBreath, resized;
  {
    std::lock_guard<std::mutex> l(in.m);
    static_assert(PARAM_COUNT <= 128, "dirty flags are drained one bit at a time");
    for (int i = 0; in.dirty.any() && i < PARAM_COUNT; i++) {
      if (!in.dirty.test(i)) continue;
      const ParamInfo& pi = PARAM_INFO[i];
      const size_t sz = pi.type == PT_FLOAT ? sizeof(float) : pi.type == PT_BOOL ? sizeof(bool) : sizeof(int);
      memcpy((char*)&e->target + pi.offset, (char*)&in.P + pi.offset, sz);
      if (pi.type == PT_FLOAT && !pi.stepped) e->rampLeft[i] = EASE;
      else { memcpy((char*)&e->P + pi.offset, (char*)&in.P + pi.offset, sz); e->rampLeft[i] = 0; }
    }
    in.dirty.reset();
    if (in.sysDirty) { e->sys = in.sys; in.sysDirty = false; }
    qn = in.qn; memcpy(e->q, in.q, qn * sizeof(InputEv)); in.qn = 0;
    hn = in.hn;
    for (int i = 0; i < hn; i++) e->hq[i] = in.hq[(in.hHead + i) % Engine::Inbox::HCAP];
    in.hn = 0; in.hHead = 0;
    handsEnable = in.handsEnable; handsAt = in.handsEnableAt; in.handsEnable = -1;
    e->twoHands = in.twoHands; e->paused = in.paused; e->camOn = in.camOn; e->hands.va = in.camAspect;
    clearPens = in.clearPens; resetBreath = in.resetBreath; in.clearPens = in.resetBreath = false;
    resized = in.resized; in.resized = false;
    if (resized) { e->viewW = in.viewW; e->viewH = in.viewH; e->scale = in.scale; }
    e->gpuSum += in.gpuSum; e->gpuN += in.gpuN; in.gpuSum = 0; in.gpuN = 0;
    if (in.frameInterval > 0) e->refreshHz = std::min(240.0, 1 / in.frameInterval);
    e->thermal = in.thermal;
  }
  if (resized && e->viewW > 0 && e->viewH > 0) {
    e->pens.viewW = e->viewW; e->pens.viewH = e->viewH; e->hands.viewAspect = e->viewW / e->viewH;
    if (!e->firstDone) { e->firstDone = true; firstLevel(e); }
  }
  if (clearPens) e->pens.clear();
  if (resetBreath) { e->breathT = 0; e->breathIn = false; e->breathN = 0; }
  if (handsEnable == 1 && !e->hands.enabled) { e->hands.enabled = true; e->hands.seenAt = handsAt * 1000; }
  else if (handsEnable == 0 && e->hands.enabled) { e->hands.enabled = false; e->hands.onHands(nullptr, 0, handsAt * 1000); }

  Pens& p = e->pens;
  for (int i = 0; i < qn; i++) {
    const InputEv& ev = e->q[i];
    const double tms = ev.t * 1000;
    switch (ev.type) {
      case InputEv::POINTER:
        if (ev.kind == PTR_DOWN) p.pointerDown(ev.id, ev.flag, ev.x, ev.y, tms);
        else if (ev.kind == PTR_MOVE) p.pointerMove(ev.id, ev.flag, ev.x, ev.y, tms);
        else if (ev.kind == PTR_UP || ev.kind == PTR_CANCEL) p.pointerUp(ev.id, ev.flag, ev.x, ev.y, tms, ev.kind == PTR_CANCEL);
        else if (ev.kind == PTR_LEAVE) p.pointerLeave(ev.id);
        break;
      case InputEv::SCROLL: p.wheel(ev.x, ev.y, ev.flag); break;
      case InputEv::GBEGIN: p.gestureBegin(tms); break;
      // Screen y points down in the web's maths, so counterclockwise is negative there.
      case InputEv::GROTATE: p.gestureTwist(-ev.x, tms); break;
      case InputEv::GMAGNIFY: p.gesturePinch(ev.x, tms); break;
      case InputEv::GEND: p.gestureEnd(); break;
    }
  }
  if (e->hands.enabled)
    for (int i = 0; i < hn; i++) e->hands.onHands(e->hq[i].h, e->hq[i].n, e->hq[i].t * 1000);
}

// ---------------- frame ----------------
static void step(Engine* e, double dt, float bass, float mid, float high) {
  const Params& P = e->P;
  const System& system = e->sys;
  Frame& f = e->f;
  const double n = dt * 60, rad = PI / 180;
  const double pdt = dt * P.pace, pn = n * P.pace;
  e->time += pdt;
  const double time = e->time;
  palette_update(e->pal, P.palette, dt);
  e->motion.update(dt, e->fs);
  // The lens places lean toward whichever finger or hand is painting.
  e->fs.lean = false; e->fs.feed = e->pens.energy;
  for (int k = 0; k < e->pens.nPtr && !e->fs.lean; k++) {
    const Pen& p = e->pens.pointers[k].p;
    if (p.down) { e->fs.lean = true; e->fs.leanX = p.x; e->fs.leanY = p.y; }
  }
  for (const Pen& p : e->pens.hands) {
    if (e->fs.lean) break;
    if (p.down) { e->fs.lean = true; e->fs.leanX = p.x; e->fs.leanY = p.y; }
  }
  e->fs.update(P, pdt, time, 0, 0);

  // Slow breathing, and whatever the room is playing. All of it moves the picture;
  // none of it changes brightness, so the flash guard has nothing new to catch.
  double breath = P.breath * std::sin(time * 0.37), lung = 0.5;
  if (system.breathGuide) {
    e->breathT = std::fmod(e->breathT + dt, 10);
    const bool inh = e->breathT < 4;
    lung = inh ? 0.5 - 0.5 * std::cos(PI * e->breathT / 4) : 0.5 + 0.5 * std::cos(PI * (e->breathT - 4) / 6);
    breath = (lung * 2 - 1) * 1.5;
    if (inh != e->breathIn) { e->breathIn = inh; e->events.push({EV_BREATH_CUE, 0, 0, 0, inh, e->breathN}); if (!inh) e->breathN++; }
  }
  e->fs.addSpin(mid * pdt * 0.3);
  e->fs.f.fish = (float)(P.fisheye + breath * 0.045 + bass * 0.12);

  const Motion& M = e->motion;
  f.n = (float)n;
  f.dt = (float)dt;
  f.pace = P.pace;
  f.decay = (float)std::pow((double)P.fbDecay, n);
  f.hue = (float)(P.fbHueShift * rad * pdt);
  f.fbRot = (float)(P.fbRotate * rad * pdt + (M.held ? 0 : M.spinVel * dt * 0.35));
  f.fbZoom = (float)(std::pow((double)P.fbZoom, pn) * std::exp(M.diveVel * dt + breath * 0.1 * (system.breathGuide ? dt : pdt) + bass * bass * 0.9 * dt));
  f.fbSwirl = (float)(P.fbSwirl * pn);
  f.fbWarp = (float)(P.fbWarp * pn);
  e->fbWarpT += pdt * 0.5; f.fbWarpT = (float)e->fbWarpT;
  e->genT += pdt * P.genSpeed * (1 + mid * 1.5); f.genT = (float)e->genT;
  e->rmZ += pdt * P.rmFly * (1 + bass * 2); f.rmZ = (float)e->rmZ;
  // The 3D shapes answer touch, the cursor and the hands alike. The maths shapes are never
  // seen from outside: the camera flies low over their surface, which fills the picture; a
  // drag (or a hand) steers the flight, a dive sinks toward the surface, where finer detail
  // keeps resolving. The hand never touches the formula itself: c drifts on its own along a
  // slow closed loop, so the pattern changes only gradually.
  if (P.gen == GEN_fractal3d) {
    const int shape = P.rmShape;
    const bool surf = rm_surface(shape);
    if (shape != e->rmLastShape) {
      e->rmLastShape = shape; e->rmYaw = 0.6; e->rmPitch = 0.35; e->rmYawV = e->rmPitchV = 0; e->rmZoom = 1; e->rmCam = RmCam{};
    }
    // One pen leads; a different pen taking over starts a new hold rather than a jump.
    bool held = false; double lx = 0, ly = 0; int lead = -1;
    auto take = [&](const Pen& p, int id) { if (!held && p.down) { held = true; lx = p.x; ly = p.y; lead = id; } };
    // (Pointers by their id; hands as -2, -3, ...; -1 is nobody.)
    for (int k = 0; k < e->pens.nPtr; k++) if (e->pens.pointers[k].id == e->rmLead) take(e->pens.pointers[k].p, e->rmLead);
    for (int k = 0; k < 10; k++) if (-2 - k == e->rmLead) take(e->pens.hands[k], -2 - k);
    for (int k = 0; k < e->pens.nPtr; k++) take(e->pens.pointers[k].p, e->pens.pointers[k].id);
    for (int k = 0; k < 10; k++) take(e->pens.hands[k], -2 - k);
    // Over a surface the ground under the finger moves with it (whatever the height: the
    // turn is scaled by the camera's height over the surface); around a shape, a drag turns it.
    const double gk = surf ? e->rmCam.gs / 1.5 : 1.0;
    if (held && e->rmHeld && lead == e->rmLead && dt > 0) {
      const double dyaw = (lx - e->rmLx) * (surf ? -gk : 2.2), dpitch = (ly - e->rmLy) * (surf ? gk : 1.8);
      e->rmYaw += dyaw; e->rmPitch = clampd(e->rmPitch + dpitch, -1.2, 1.2);
      e->rmYawV += (dyaw / dt - e->rmYawV) * std::min(1.0, dt * 8); e->rmPitchV += (dpitch / dt - e->rmPitchV) * std::min(1.0, dt * 8);
    } else if (!held) {
      e->rmYaw += e->rmYawV * dt; e->rmPitch = clampd(e->rmPitch + e->rmPitchV * dt, -1.2, 1.2);
      e->rmYawV *= std::exp(-dt * 1.2); e->rmPitchV *= std::exp(-dt * 2.5);
      // Left alone, the flight drifts on, slowly: the ground passes at a few percent of the
      // picture a second, at any height.
      if (surf) e->rmYawV += (0.06 * gk * 1.5 - e->rmYawV) * (1 - std::exp(-dt * 0.4));
    }
    e->rmHeld = held; e->rmLead = lead; e->rmLx = lx; e->rmLy = ly;
    e->rmMx = 0; e->rmMy = 0;
    e->rmZoom = clampd(e->rmZoom * std::exp(M.diveVel * dt * 1.6), surf ? 0.5 : 0.35, surf ? 200.0 : 4.0);

    rm_frame(shape, e->rmT, P.rmMorph, P.rmDetail, P.rmSlice, e->rmYaw, e->rmPitch, e->rmZoom, e->rmYawV, dt, e->rmCam, f);
  }
  f.rmView[0] = (float)e->rmYaw; f.rmView[1] = (float)e->rmPitch; f.rmView[2] = (float)e->rmZoom;
  f.rmMorphXY[0] = (float)e->rmMx; f.rmMorphXY[1] = (float)e->rmMy;
  e->rmT += pdt; f.rmT = (float)e->rmT;
  e->palPhase += pdt * P.paletteSpeed + high * dt * 0.12; f.palPhase = (float)e->palPhase;
  f.dream = (float)std::min(0.5, P.fbDream * 0.05 * pn);
  f.dreamX = (float)(0.3 * std::cos(time * 0.11));
  f.dreamY = (float)(0.3 * std::sin(time * 0.143));
  e->sheenT += pdt * 0.21; f.sheenT = (float)e->sheenT;

  f.camOn = e->camOn;
  f.inkCount = e->pens.build(dt, time, f, e->palPhase);
  f.ripN = e->pens.ripN + e->pens.combN;
  // The sky places. Each lives in one wedge of the kaleidoscope and the mirrors show it
  // 2 * sym times, so a finger (and a hovering cursor) is folded into that wedge and
  // every star near any of its reflections answers it.
  f.starsReset = P.stars > 0 && !e->starsOn;
  e->starsOn = P.stars > 0;
  if (P.starKind != e->starKind) { e->starKind = P.starKind; f.starsReset = e->starsOn; }
  if (f.starsReset) { e->skyZoom = 1; e->skyTaps = e->pens.taps; e->heldT = e->quietT = 0; }
  f.starCores = f.starAuto = 0;
  f.starBlast[2] = 0;
  if (e->starsOn) {
    const Fold& fo = e->fs.f;
    // Skies assume the short side is the height; shrink to fit a portrait screen.
    const double fit = e->viewW > 0 && e->viewH > 0 ? std::min(1.0, (double)e->viewW / e->viewH) : 1.0;
    // The sky keeps its own zoom: pinch or scroll dives in, Lens zoom and spin apply on top.
    // Its points live within 2.6 of its centre (sky_layout.h), so it never zooms out past the
    // screen's corners: the sky stays boundless instead of shrinking to a disc with an edge.
    const double aspect = e->viewW > 0 && e->viewH > 0 ? (double)e->viewW / e->viewH : 16.0 / 9;
    const double fill = std::hypot(aspect, 1.0) * 1.05 / (2.6 * std::max(1e-3, (double)P.viewZoom) * fit);
    e->skyZoom = clampd(e->skyZoom * std::exp(M.diveVel * dt * 1.6), std::max(0.3, fill), std::max(8.0, fill));
    const double zoom = e->skyZoom * P.viewZoom * fit, sc = std::cos((double)fo.spin), ss = std::sin((double)fo.spin);
    const double sym = std::max(2.0, (double)fo.sym); const bool mir = fo.mirror;
    f.starView[0] = (float)zoom; f.starView[1] = (float)sc; f.starView[2] = (float)ss;
    f.starView[3] = fo.cx; f.starView[4] = fo.cy;
    // Into the sky's open plane (not yet folded): the centre, turn and zoom taken out.
    auto toSky = [&](double x, double y, double& ox, double& oy) { x -= fo.cx; y -= fo.cy; ox = (sc * x + ss * y) / zoom; oy = (-ss * x + sc * y) / zoom; };
    auto toSkyV = [&](double x, double y, double& ox, double& oy) { ox = (sc * x + ss * y) / zoom; oy = (-ss * x + sc * y) / zoom; };
    const int kind = P.starKind;
    star_own_cores(f, kind, time);
    auto blast = [&](double x, double y, double s) {
      double vx = 0, vy = 0; star_fold(sym, mir, x, y, vx, vy);
      f.starBlast[0] = (float)x; f.starBlast[1] = (float)y; f.starBlast[2] = (float)s;
    };

    // The leading finger: the first one pressed (or, failing that, a hovering cursor),
    // in the sky's open plane.
    bool held = false, near = false; double hx = 0, hy = 0, sx = 0, sy = 0;
    auto hovering = [&](const Pen& p) { return p.kind == Pen::MOUSE && !p.down && p.inside && P.inkHover; };
    auto lead = [&](const Pen& p) { if (!held && p.down) { held = true; sx = p.x; sy = p.y; toSky(p.x, p.y, hx, hy); } };
    for (int k = 0; k < e->pens.nPtr; k++) lead(e->pens.pointers[k].p);
    for (const Pen& p : e->pens.hands) lead(p);
    if (!held) for (int k = 0; k < e->pens.nPtr; k++) if (hovering(e->pens.pointers[k].p)) { const Pen& p = e->pens.pointers[k].p; near = true; sx = p.x; sy = p.y; toSky(p.x, p.y, hx, hy); break; }
    // Where the hand is, eased, so the sky can be looked into: its near stars shift
    // against the hand and the far ones hardly move. With no hand it drifts home.
    {
      const double k = 1 - std::exp(-dt * (held || near ? 4.0 : 0.8));
      e->lookX += ((held || near ? sx : 0) - e->lookX) * k; e->lookY += ((held || near ? sy : 0) - e->lookY) * k;
      if (f.starsReset) e->lookX = e->lookY = 0;
      f.starLook[0] = (float)e->lookX; f.starLook[1] = (float)e->lookY;
    }
    const bool landed = held && e->heldT == 0;       // it touched down this frame
    const double sdt = std::max(dt, 1e-3);
    const bool aiming = held || (near && kind != 2);   // a hovering cursor bends the pulsar's beams but leaves their spin alone

    // Pulsar: the beams point at the finger. Circling the star swings them like a wheel,
    // and they keep the spin they were given, easing back to their own slow turn. The
    // further out the finger, the harder they shine.
    if (kind == 2) {
      if (aiming) {
        const double ang = std::atan2(hy, hx);
        double d = ang - e->aim;
        d -= TAU * std::floor(d / TAU + 0.5);
        if (!landed && e->aimed) e->aimVel += (clampd(d / sdt, -16, 16) - e->aimVel) * std::min(1.0, dt * 14);
        e->aim = ang; e->aimed = true;
        e->aimPower += (clampd(std::hypot(hx, hy) / 0.6, 0.5, 1.6) - e->aimPower) * std::min(1.0, dt * 8);
      } else {
        e->aimed = false;
        e->aim += e->aimVel * dt;
        e->aimVel += ((e->aimVel < 0 ? -2.4 : 2.4) - e->aimVel) * std::min(1.0, dt * 0.3);
        e->aimPower += (1 - e->aimPower) * std::min(1.0, dt * 0.6);
      }
      f.starCore[3] = (float)e->aim; f.starVel[0] = (float)e->aimPower; f.starVel[1] = (float)e->aimVel;
    }
    // Quasar: the jets point along the line through the finger.
    if (kind == 3) {
      if (aiming && std::hypot(hx, hy) > 0.08) {
        double d = std::atan2(hy, hx) - e->jetAim;
        d -= PI * std::floor(d / PI + 0.5);          // a jet has two ends: take the nearer way round
        e->jetAim += d * std::min(1.0, dt * 9);
      } else e->jetAim += 0.05 * std::sin(time * 0.2) * dt;
      f.starCore[3] = (float)e->jetAim;
    }

    // Every finger is a core (the supernova's grows as it gathers); a hovering cursor is light.
    const float mass = star_finger_mass(kind) * (float)(kind == 4 ? 0.5 + 0.5 * std::min(e->heldT / 1.5, 1.0) : 0.8 + 0.2 * std::min(e->heldT / 0.6, 1.0));
    auto finger = [&](const Pen& p) {
      const bool hov = hovering(p);
      if (!p.down && !hov) return;
      double x, y, vx, vy;
      toSky(p.x, p.y, x, y); toSkyV(p.vx, p.vy, vx, vy);
      star_add_finger(f, sym, mir, x, y, vx, vy, hov ? star_finger_mass(kind) : mass, hov);
    };
    for (int k = 0; k < e->pens.nPtr; k++) finger(e->pens.pointers[k].p);
    // Tracked fingers fade in and out so tracker flicker never makes the sky jump.
    for (int k = 0; k < 10; k++) {
      const Pen& p = e->pens.hands[k];
      auto& H = e->handSky[k];
      H.pres += ((p.down ? 1.0 : 0.0) - H.pres) * std::min(1.0, dt * (p.down ? 6.0 : 3.0));
      if (p.down) { toSky(p.x, p.y, H.x, H.y); toSkyV(p.vx, p.vy, H.vx, H.vy); }
      else { H.vx *= std::exp(-dt * 4); H.vy *= std::exp(-dt * 4); }
      if (f.starsReset) H.pres = 0;
      if (H.pres > 0.01) star_add_finger(f, sym, mir, H.x, H.y, H.vx, H.vy, mass, false, (float)(H.pres * H.pres * (3 - 2 * H.pres)));
    }
    // The wanderers stir the sky as lightly as a hovering cursor, so it is never still.
    const int wander = std::min(e->pens.nAuto, (int)jsround(P.autoPens));
    for (int i = 0; i < wander; i++) {
      const Pen& p = e->pens.autos[i];
      double x, y, vx, vy;
      toSky(p.x, p.y, x, y); toSkyV(p.vx, p.vy, vx, vy);
      star_add_finger(f, sym, mir, x, y, vx, vy, star_finger_mass(kind) * 0.6f, true);
    }

    if (kind == 4) f.starLensN = 0;   // in the supernova a held finger is the star it gathers: no lens to hide it
    if (held) { e->heldT += dt; e->heldX = hx; e->heldY = hy; e->quietT = 0; }
    else {
      e->quietT += dt;
      // Supernova: the longer a finger gathered, the harder it bursts on letting go. Left
      // alone, the middle goes off by itself every so often, so the place is never still.
      if (kind == 4) {
        if (e->heldT > 0.25) blast(e->heldX, e->heldY, 0.9 + 0.9 * std::min(e->heldT, 3.0));
        else if (e->quietT > 5) {
          // By turns the heart goes off, and then a ring of stars around it at once.
          if (std::fmod(time, 10.0) < 5) blast(0, 0, 1.8); else blast(0.3, 0.1, 1.5);
          e->quietT = 0;
        }
      }
      e->heldT = 0;
    }
    // A tap is a burst, in every sky.
    if (e->pens.taps != e->skyTaps) {
      e->skyTaps = e->pens.taps;
      double x, y; toSky(e->pens.tapX, e->pens.tapY, x, y);
      blast(x, y, 1.0);
    }
  }
  const Fold& fd = e->fs.f;
  const bool gpuMirrors = fd.mode == 0 || (fd.mode == 1 && fd.warpAmt <= 0);
  f.inkCopies = gpuMirrors ? (fd.mirror ? 2 * (int)fd.sym : (int)fd.sym) : 1;
  // The look is drawn while shown; the two-hand bridge whenever two hands hold the glass.
  const int look = system.handLookOn ? (int)system.handLook : 0;
  if (e->hands.enabled && (look > 0 || e->hands.grab.mode == 2))
    f.looks = e->looks.build(look, dt, time, e->drawH > 0 ? (double)e->drawW / e->drawH : 1.0, e->hands);
  else { if (f.looks) e->looks.reset(); f.looks = nullptr; }
  e->lung = system.breathGuide ? lung : -1;

  f.fold = fd; f.P = P; f.sys = system; f.pal = e->pal;
}

static void publish(Engine* e) {
  std::lock_guard<std::mutex> l(e->statsM);
  EngineStats& s = e->stats;
  s.drawW = e->drawW; s.drawH = e->drawH; s.renderW = e->W; s.renderH = e->H; s.echoW = e->FW; s.echoH = e->FH; s.echoScale = e->FS;
  s.capW = std::max(e->W, e->capW); s.capH = std::max(e->H, e->capH); s.capEchoW = std::max(e->FW, e->capFW); s.capEchoH = std::max(e->FH, e->capFH);
  s.ssTaps = e->ssTaps; s.rmSteps = e->rmSteps;
  s.fps = e->fps; s.quality = (float)e->quality; s.refreshHz = (float)e->refreshHz; s.gpuLoad = (float)e->gpuLoad; s.level = e->level;
  s.halfRate = halved(e);
  s.particles = e->pens.pCount; s.ripples = e->pens.ripN; s.taps = e->pens.taps;
  s.painted = (float)e->pens.painted; s.energy = (float)e->pens.energy; s.lung = (float)e->lung;
  s.motionUsed = e->motion.used;
  HandsState& h = e->handsState;
  h.cam = e->camOn; h.hands = e->hands.enabled; h.twoHands = e->twoHands; h.grabMode = e->hands.grab.mode;
  for (int i = 0; i < 2; i++) {
    const auto& H = e->hands.hs[i];
    h.grabOn[i] = H.seen && H.pinch; h.grabX[i] = (float)H.gx; h.grabY[i] = (float)H.gy;
  }
}

const Frame& engine_step(Engine* e, double dt, float bass, float mid, float high) {
  drain(e);
  if (e->paused) { publish(e); return e->f; }
  dt = clampd(dt, 0, 0.05);
  if (e->sys.autoQuality && e->level < minLevel(e)) setLevel(e, e->level);   // thermal cap
  adapt(e, dt);
  resize(e);
  ease(e, dt);
  step(e, dt, bass, mid, high);
  publish(e);
  return e->f;
}
