// Internal to core/: the Engine struct and the modules it is built from.
#pragma once
#include "engine.h"
#include "rmflight.h"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <mutex>

constexpr double TAU = 6.283185307179586;
constexpr double PI = 3.141592653589793;

// JS Math.round (halves round up, also for negatives) and Math.sign.
// A mutex that a copy of its owner (engine_clone) gets fresh instead of copying.
struct Mutex : std::mutex {
  Mutex() = default;
  Mutex(const Mutex&) : std::mutex() {}
  Mutex& operator=(const Mutex&) { return *this; }
};

inline double jsround(double x) { return std::floor(x + 0.5); }
inline double jssign(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
inline double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

struct Rng {             // stands in for Math.random
  uint64_t s = 0x9E3779B97F4A7C15ull;
  double next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return ((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0); }
};

// ---- fold.cpp ----
double gnoise(double x, double y);
struct FoldSys {
  Fold f;
  double spin = 0, cx = 0, cy = 0, warpT = 0, drPhase = 0, kAnglePhase = 0;   // double accumulators
  // The lens places (mode >= 6) lean toward the hand that is painting, and the black
  // hole swells while it is being fed. Set by the engine before update().
  bool lean = false; double leanX = 0, leanY = 0, feed = 0, fed = 0;
  // The steered places (mode >= 10) follow the painting hand closely with this point,
  // which wanders on its own when nobody is touching. It rides in uHyp.zw.
  double fx = 0, fy = 0;
  void update(const Params& P, double dt, double time, double tiltX, double tiltY);
  void addSpin(double a) { spin += a; f.spin = (float)spin; }
  void apply(double x, double y, double out[2]) const;
};
void palette_update(PaletteState& pal, int palette, double dt);
void palette_write(const PaletteState& pal, double t, float* arr, double gain);

struct Motion {
  double spinVel = 0, diveVel = 0, pend = 0;
  bool held = false;
  int used = 0;
  enum { tA, tD, cA, cD, KEYS };
  double ref[KEYS] = {}; bool has[KEYS] = {};
  double take(int key, double v, double play);
  void drop(int a, int b) { has[a] = has[b] = false; }
  void feed(double a) { pend += a; }
  void spin(double v) { used++; spinVel = clampd(spinVel + v, -8, 8); }
  void dive(double v) { used++; diveVel = clampd(diveVel + v, -1.6, 1.6); }
  void update(double dt, FoldSys& fs);
};

struct EventQueue {      // render thread pushes, app polls
  Mutex m;
  enum { CAP = 256 };
  EngineEvent ev[CAP]; int head = 0, n = 0;
  void push(const EngineEvent& e) { std::lock_guard<std::mutex> l(m); if (n == CAP) return; ev[(head + n++) % CAP] = e; }
  bool pop(EngineEvent* out) { std::lock_guard<std::mutex> l(m); if (!n) return false; *out = ev[head]; head = (head + 1) % CAP; n--; return true; }
};

// ---- pens.cpp ----
struct Pen {
  enum { MOUSE, TOUCH, AUTO, HAND };
  int kind = AUTO;
  double x = 0, y = 0, px = 0, py = 0, hue = 0, offset = 0, v = 0, vx = 0, vy = 0, acc = 0, t0 = 0, sx = 0, sy = 0, tx = 0, ty = 0;
  bool has = false, down = false, inside = false;
  double cbx = 0, cby = 0; bool cb = false;      // wet paint: where this pen was last frame
  // A tracked hand's fingertip (Pens::setHands): the camera's last sample, its velocity,
  // when it came, and the spring that carries the pen smoothly toward it.
  double hvx = 0, hvy = 0, sampleT = 0, fvx = 0, fvy = 0;
  double rx = 0, ry = 0, dvx = 0, dvy = 0;        // the last raw sample and its smoothed speed (One Euro filter)
  float q[96]; int qn = 0;
};

struct Pens {
  enum { MAX_SEG = 16384, PMAX = 4096, MAX_PTR = 16, MAX_AUTO = 8 };
  struct Ptr { int id; Pen p; };
  Ptr pointers[MAX_PTR]; int nPtr = 0;          // insertion order, like the web's Map
  Pen autos[MAX_AUTO]; int nAuto = 0;
  Pen hands[10];
  float parts[PMAX * 8];                        // x, y, vx, vy, life, hue, fx, fy
  int pCount = 0;
  double emitAcc = 0, autoT = 0;
  int n = 0;
  double lastT = 0, dt = 0, palPhase = 0;
  double clock = 0;                               // seconds, advanced by build(): when hand samples arrive
  const Frame* fr = nullptr;
  double ux = 0, uy = 0;
  float data[MAX_SEG * 8];
  struct Touch { int id; double x, y; };
  Touch touches[MAX_PTR]; int nTouch = 0;
  double gd = 0, ga = 0, gt = 0, gdN = 0, gaN = 0, gAng = 0, gLog = 0;
  float tapQ[96]; int tapN = 0;                 // taps waiting: x, y (p-space), strength (1: a click)
  float rip[16] = {}, ripAge[4] = {}, ripPow[4] = {};   // ripPow: the chamber's ripple times the tap's strength
  // A hand stirring the picture (an open hand on the move): its palm and velocity in view
  // fractions (origin top left), refreshed by each tracker sample and carried between them.
  struct Stir { bool on = false; double u = 0, v = 0, vu = 0, vv = 0, at = 0; } stirs[2];
  int ripN = 0, taps = 0;
  double tapX = 0, tapY = 0;                    // where the last tap landed (p-space)
  int combN = 0;                                // wet-paint drags this frame, stored after the ripples
  void comb(Pen& p, bool active);
  double energy = 0, moved = 0, painted = 0;
  float col[3] = {};

  // Context set by the engine before use.
  const Params* P = nullptr;
  FoldSys* fs = nullptr;
  Motion* motion = nullptr;
  const PaletteState* pal = nullptr;
  EventQueue* events = nullptr;
  Rng* rng = nullptr;
  float viewW = 1, viewH = 1;

  // input (web: the listeners in Pens.attach)
  void pointerDown(int id, bool mouse, double cx, double cy, double t);
  void pointerMove(int id, bool mouse, double cx, double cy, double t);
  void pointerUp(int id, bool mouse, double cx, double cy, double t, bool cancel);
  void pointerLeave(int id);
  void wheel(double dx, double dy, bool shift);
  void gestureBegin(double t);
  void gestureTwist(double da, double t);
  void gesturePinch(double m, double t);
  void gestureEnd();

  void setHands(const bool on[10], const float tips[10][2]);
  // A tap in the air (u, v: view fractions, origin top left): what a click does there,
  // `power` times as strong; negative pulls the wave inward.
  void airTap(double u, double v, double power);
  void setStir(int hand, bool on, double u, double v, double vu, double vv);
  int build(double dt, double time, const Frame& frame, double palPhase);   // frame: its pace and fb* fields
  void clear() { pCount = 0; ripN = 0; }

 private:
  Pen* get(int id, bool mouse);
  Touch* touch(int id);
  void measure();
  void toP(double clientX, double clientY, double& ox, double& oy) const;
  void push(double ax, double ay, double bx, double by, double rad);
  double foldScale(double x, double y);
  void carried(double x, double y);
  void stroke(double ax, double ay, double bx, double by, double rad);
  bool spawn(double x, double y, double vx, double vy, double life);
  void emit(Pen& p, double d);
  void burst(double x, double y, double power = 1);
  void toView(double u, double v, double& x, double& y) const;   // view fractions to p-space (with the fisheye)
  void stir(Stir& s);
  void updateRipples();
  void updateParticles(double time);
  void autoPos(Pen& p, int i, int count);
  double colorT(Pen& p, double v);
  void run(Pen& p, bool active);
  void drawPen(Pen& p, bool active);
  void pluck(double x, double y, double v);
};

// ---- hands.cpp (web Inputs: onHands and what it uses) ----
struct Hands {
  struct H {
    bool seen = false, pinch = false, ext[5] = {}; float lx[21] = {}, ly[21] = {}; double gx = 0, gy = 0, ang = 0, scale = 1;
    // Gestures: how fast the hand's apparent size changes (log per second: + toward the
    // camera), when and where the current pinch began, and the palm's last place.
    double ls = 0, sAt = 0, rate = 0, pinchT = 0, pinchU = 0, pinchV = 0, pinchMoved = 0, pokeCool = 0, punchCool = 0;
    double palmU = 0, palmV = 0, palmAt = 0;
  };
  H hs[2];
  bool tipOn[10] = {}; float tips[10][2] = {};
  struct { int mode = 0; double ang = 0, dist = 1, t = 0, acc = 0, sm = 0, lacc = 0, lsm = 0; } grab;
  // Two pinched hands hold a bridge of light that charges as you stretch it (0..1.5);
  // letting go of both at once snaps it.
  struct { double startDist = 1, held = 0, charge = 0, at = 0, mu = 0, mv = 0, power = 0; bool pending = false; } bridge;
  struct { double since = 0; bool fired = false; double ax = 0, ay = 0, bx = 0, by = 0, quietUntil = 0; } palm;   // quietUntil: no step right after a snap
  double seenAt = 0;    // ms, like performance.now()
  bool enabled = false;
  float va = 16.f / 9.f;                           // camera aspect

  // Context
  Pens* pens = nullptr; Motion* motion = nullptr; EventQueue* events = nullptr;
  float viewAspect = 16.f / 9.f;

  void onHands(const HandObs* list, int n, double nowMs);
 private:
  void track(H& h, const HandObs& o);
  void holdGlass(double nowMs);
  void gestures(H& h, int slot, bool wasPinch, double nowMs);
};

// ---- engine.cpp ----
// Hand looks (looks.cpp; web Looks): smoothed hands and the state of ribbons and curtains.
class Looks {
 public:
  const LookData* build(int look, double dt, double time, double aspect, const struct Hands& src);
  void reset();
 private:
  enum { RN = 40, TRAIL = 256 };
  struct RGBf { float r, g, b; };
  struct Node { double x, y, px, py, tw, tv; };
  struct Trail {   // a tip's recent path: x, y, time
    double e[TRAIL][3]; int head = 0, n = 0;
    void push(double x, double y, double t) { double* v = e[(head + n) % TRAIL]; v[0] = x; v[1] = y; v[2] = t; if (n < TRAIL) n++; else head = (head + 1) % TRAIL; }
    void drop() { head = (head + 1) % TRAIL; n--; }
    const double* at(int i) const { return e[(head + i) % TRAIL]; }
  };
  struct Hand {
    float p[42] = {}; double a = 0, S = 0.5, pinch = 0; bool init = false, rbOn = false;
    Trail tr[5]; Node rb[5][RN];
    void forget() { init = false; rbOn = false; for (Trail& t : tr) t.n = t.head = 0; }
  };
  Hand hands[2];
  LookData out;
  void addSeg(float ax, float ay, float bx, float by, float w, RGBf c, float alpha, float sharp, float glow);
  void addV(float x, float y, float a, float b, float c, float e);
  void strip(int first, int kind);
  void ring(const Hand& H, RGBf col, float A);
  void constellation(Hand& H, int i, double t);
  void neon(Hand& H, int i, double t);
  void aurora(Hand& H, int i, double t);
  void ribbons(Hand& H, int i, double t, double dt);
  void aura(Hand& H);
  void bridge(const struct Hands& src, double aspect, double t, double dt);
  double bridgeA = 0;   // the bridge's presence, eased
};

struct InputEv {
  enum { POINTER, SCROLL, GBEGIN, GROTATE, GMAGNIFY, GEND };
  int type, id, kind; float x, y; double t; bool flag;
};

struct Engine {
  // Inbox: written from any thread, drained at the start of engine_step.
  struct Inbox {
    Mutex m;
    Params P; std::bitset<PARAM_COUNT> dirty;
    System sys; bool sysDirty = false;
    enum { QCAP = 2048, HCAP = 4 };
    InputEv q[QCAP]; int qn = 0;
    struct HandFrame { HandObs h[2]; int n; double t; } hq[HCAP]; int hHead = 0, hn = 0;
    int handsEnable = -1; double handsEnableAt = 0;   // -1: no change
    bool twoHands = true, clearPens = false, resetBreath = false, paused = false, camOn = false;
    float camAspect = 16.f / 9.f;
    float viewW = 0, viewH = 0, scale = 1; bool resized = false;
    double gpuSum = 0, gpuMax = 0; int gpuN = 0; double frameInterval = 0;
    int thermal = 0;
  } in;
  // Drained copies (render thread only).
  InputEv q[Inbox::QCAP]; Inbox::HandFrame hq[Inbox::HCAP];

  // Parameters: target as sent, P as eased.
  Params target, P;
  double rampLeft[PARAM_COUNT] = {};
  System sys;

  FoldSys fs;
  Motion motion;
  Pens pens;
  Hands hands;
  PaletteState pal;
  EventQueue events;
  Rng rng;
  Frame f;
  bool starsOn = false; int starKind = 0;           // the sky place that was on last frame
  double heldT = 0, heldX = 0, heldY = 0, quietT = 0;   // how long a finger has gathered, and where
  double lookX = 0, lookY = 0;                     // where the hand is on screen, eased: the sky's parallax
  struct { double pres = 0, x = 0, y = 0, vx = 0, vy = 0; } handSky[10];   // each tracked finger in the sky: how present, where last seen
  double skyZoom = 1; int skyTaps = 0;              // the sky's own zoom (pinch or scroll dives into it); taps seen
  // Pulsar: where its beams point, how fast they turn and how hard they shine: a finger
  // aims them and spins them. Quasar: where its jets point.
  double aim = 0, aimVel = 2.4, aimPower = 1, jetAim = 1.5707963; bool aimed = false;

  double time = 0;
  double breathT = 0; bool breathIn = false; int breathN = 0;
  double fbWarpT = 0, genT = 0, rmZ = 0, rmT = 0, palPhase = 0, sheenT = 0;
  // The 3D shapes in the hand: turned, spinning on, zoomed and morphed (step, see Frame::rmView).
  double rmYaw = 0, rmPitch = 0, rmYawV = 0, rmPitchV = 0, rmZoom = 1, rmMx = 0, rmMy = 0, rmLx = 0, rmLy = 0; bool rmHeld = false;
  // The surface flight over the compact maths shapes (rmflight.h), which pen steers it, and
  // the shape last seen (a new shape starts its flight afresh).
  RmCam rmCam; int rmLead = -1, rmLastShape = -1;
  double lung = -1;
  bool paused = false, camOn = false, twoHands = true;

  // detail ladder
  int level = 0; double quality = 1, refreshHz = 60, fpsAcc = 0; int fpsFrames = 0, good = 0;
  struct { bool on = false; int level = 0; double fps = 0, gpu = 0; } trial;
  int cooldown = 0, ceiling = 0, ceilingFor = 0, thermal = 0;
  double gpuSum = 0, gpuLoad = 0; int gpuN = 0;
  int ssTaps = 4, rmSteps = 76;
  float fps = 0;
  bool firstDone = false;
  float viewW = 0, viewH = 0, scale = 1;
  int drawW = 1, drawH = 1, W = 16, H = 16, FW = 16, FH = 16; float FS = 1;
  int capW = 16, capH = 16, capFW = 16, capFH = 16;

  Looks looks;
  Mutex statsM;
  EngineStats stats{};
  HandsState handsState{};
};
