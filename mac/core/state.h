// Shared state between the engine (core/) and the renderer (render/).
// Field names follow the web version (index.html: `fold`, and `f` in Main), so the
// two can be read side by side.
#pragma once
#include "params_gen.h"

// Per-frame fold state: written by the engine's fold update, read by pens and renderer.
struct Fold {
  int mode = 1; float sym = 6; bool mirror = true;
  float spin = 0, cx = 0, cy = 0, vz = 1, fish = 0;
  float warpAmt = 0, warpScale = 1.5f, warpT = 0;
  float kIters = 5, kScale = 1.42f, kAngle = 0, kOffX = 0.72f, kOffY = 0.28f;
  float hypC = 2, hypR = 1.7f, hypAx = 0, hypAy = 0;
  float drL = 1, drPhase = 0, tile = 2;
};

// Palette coefficients after easing (web: Palette.a..d).
struct PaletteState { float a[3], b[3], c[3], d[3]; };

// Hand looks (looks.cpp; web Looks): batches in picture space (y up, height -1..1).
//   seg: glowing segments, 12 floats each: a.xy, b.xy, rgb (HDR), radius, alpha, sharpness, glow, 0.
//        A point of light is a segment of no length. Light: added.
//   vert/strips: triangle strips, 6 floats a vertex: xy, then by kind
//        0 satin ribbon: across (-1..1), along (0..1), cos of the twist, hue fraction + presence in whole 255ths
//        1 aurora curtain: u along the path, v down the curtain, hue (0..1), strength
//   aura: per hand its 21 points and (palm xy, size, presence); drawn on top only, in `box`.
//   feed: the share drawn into the echoes over the ink's mirror copies (only when inkCopies > 1
//        and sys.handLookFeed); everything is also drawn once on top of the scene, before bloom.
enum { LOOK_MAX_SEG = 640, LOOK_MAX_VERT = 3200, LOOK_MAX_STRIP = 10 };
constexpr float LOOK_ON_TOP = 0.6f;   // gain on top: the hands sit quietly over the picture (web: LOOK_ON_TOP)
struct LookData {
  int look = 0;
  int nSeg = 0; float seg[LOOK_MAX_SEG * 12];
  int nVert = 0; float vert[LOOK_MAX_VERT * 6];
  int nStrip = 0; int stripFirst[LOOK_MAX_STRIP], stripCount[LOOK_MAX_STRIP], stripKind[LOOK_MAX_STRIP];
  float feed = 0;
  int auraN = 0; float auraPts[84]; float auraHand[8]; float breath = 0; float box[4];
  float time = 0;
};

// Everything the renderer needs for one frame (web: `f` in Main.step, plus the
// objects Renderer.render read directly).
struct Frame {
  float n = 1, dt = 1 / 60.f, pace = 1, decay = 1, hue = 0;
  float fbRot = 0, fbZoom = 1, fbSwirl = 0, fbWarp = 0, fbWarpT = 0;
  float genT = 0, rmZ = 0, rmT = 0, palPhase = 0;
  float dream = 0, dreamX = 0, dreamY = 0, sheenT = 0;
  bool camOn = false;
  int inkCount = 0, inkCopies = 1;   // ink segments this frame; GPU mirror copies per segment
  const float* ink = nullptr;        // inkCount * 8 floats: ax, ay, bx, by, r, g, b, radius
  const float* rip = nullptr;        // ripples, laid out as web Pens.rip
  int ripN = 0;
  const LookData* looks = nullptr;   // the hands' look this frame (null: none shown)
  // The 3D shapes (gen fractal3d): how the hand has turned them (yaw, pitch, radians), the
  // camera's zoom (1: as framed), and where the hand morphs them (x, y, eased to 0 when let go).
  float rmView[3] = {0, 0, 1}, rmMorphXY[2] = {};
  // The maths shapes: the quaternion c and the 4D slice this frame, and the surface flight's
  // camera (position and height above the surface; forward; up), used when rmSurf is set.
  float rmQ[4] = {}, rmSlice = 0, rmCam[12] = {}; bool rmSurf = false;
  // The sky places (P.stars > 0; P.starKind says which, see stars.metal). A sky lives
  // inside one wedge of the kaleidoscope, in its own space (the wedge between angle 0
  // and pi/sym, the chamber's spin taken out, the sky's zoom divided out); the mirrors
  // show it 2 * sym times. The heavy cores the points of light feel are x, y, mass and a
  // fourth value (a place's own: where its beams or jets point; a finger: 1 while it only
  // hovers), with each core's velocity. The first starAuto are the place's own.
  int starCores = 0, starAuto = 0;
  float starCore[32] = {}, starVel[16] = {};
  float starBlast[3] = {};           // a burst (a tap, or a supernova let go): where, and how hard
  float starView[5] = {1, 1, 0, 0, 0};   // the sky on screen: zoom, cos and sin of its turn, centre
  float starTime = 0, starPulse = 0; // the sky's clock, and its slow breath (0..1)
  float starLook[2] = {};            // the hand's place on screen, eased (0 when none): parallax
  // Every finger bends the sky's light like a mass bends space: a lens at each finger (in
  // the wedge, as x, y and its Einstein radius squared), which the scene pass applies at
  // every reflection.
  float starLens[24] = {}; int starLensN = 0;
  bool starsReset = false;           // the place was just entered: seed every point

  Fold fold;
  Params P;                          // already eased by the engine
  System sys;
  PaletteState pal;
};

// Folds a point of the sky's open plane into its wedge (0 <= angle <= pi/sym when
// mirrored, 0 <= angle < 2pi/sym when not) and turns a velocity the same way, so a
// finger anywhere on screen lands where its picture is drawn. stars.metal: starFold.
inline void star_fold(double sym, bool mirror, double& x, double& y, double& vx, double& vy) {
  const double seg = 6.283185307179586 / sym, r = __builtin_sqrt(x * x + y * y);
  double a = __builtin_atan2(y, x);
  const double k = __builtin_floor(a / seg);
  a -= k * seg;
  double turn = -k * seg, c = __builtin_cos(turn), s = __builtin_sin(turn);
  double ux = c * vx - s * vy, uy = s * vx + c * vy;
  if (mirror && a > seg * 0.5) {
    a = seg - a;
    const double c2 = __builtin_cos(seg), s2 = __builtin_sin(seg);
    const double rx = c2 * ux + s2 * uy, ry = s2 * ux - c2 * uy;
    ux = rx; uy = ry;
  }
  x = r * __builtin_cos(a); y = r * __builtin_sin(a); vx = ux; vy = uy;
}

// The cores a sky place has of its own (engine.cpp adds the fingers after them). Every
// place is centred on the kaleidoscope's middle, so its own core sits at the origin.
inline void star_own_cores(Frame& f, int kind, double time) {
  f.starCores = f.starAuto = 0; f.starLensN = 0;
  auto core = [&](double m, double w) {
    float* c = f.starCore + f.starCores * 4;
    c[0] = 0; c[1] = 0; c[2] = (float)m; c[3] = (float)w;
    f.starVel[f.starCores * 2] = f.starVel[f.starCores * 2 + 1] = 0;
    f.starCores++;
  };
  if (kind == 0) core(0.03, 0);                                  // the galaxy's heart
  else if (kind == 2) {                                          // the pulsar: light, or its own beams could not escape
    core(0.0005, time * 2.4);                                    // its fourth value: where the beams point
    f.starVel[0] = 1; f.starVel[1] = 2.4f;                       // velocity slots: their power and how fast they turn
  } else if (kind == 3) core(0.05, 1.5707963 + 0.3 * __builtin_sin(time * 0.2));   // the quasar; where the jets point
  else if (kind == 4) core(0.035, 0);
  f.starAuto = f.starCores;
  f.starTime = (float)time;
  // A slow breath every sky shares: the light swells and settles about every eleven seconds.
  f.starPulse = (float)(0.5 - 0.5 * __builtin_cos(time * 0.57));
}
// How hard a pressed finger pulls in each sky place. A hovering one pulls a fifth as hard
// (the pulsar's beams are bent as much by a hovering cursor as by a finger).
inline float star_finger_mass(int kind) {
  static const float m[6] = {0.07f, 0.02f, 0.005f, 0.03f, 0.11f, 0.0f};
  return m[kind >= 0 && kind < 6 ? kind : 0];
}
// Adds a finger at (x, y) in the sky's open plane (before folding), moving at (vx, vy).
// `presence` (0..1) fades a finger in and out, so a hand the camera loses for a moment
// does not make the sky jump. Each finger also bends the sky's light: a lens.
inline void star_add_finger(Frame& f, double sym, bool mirror, double x, double y, double vx, double vy, float mass, bool hover, float presence = 1) {
  star_fold(sym, mirror, x, y, vx, vy);
  if (f.starLensN < 8) {
    float* l = f.starLens + f.starLensN * 3;
    l[0] = (float)x; l[1] = (float)y; l[2] = (hover ? 0.005f : 0.012f) * presence;   // Einstein radius about 0.07 of the sky (hovering) or 0.11 (pressed)
    f.starLensN++;
  }
  if (f.starCores >= 8) return;
  mass *= presence;
  float* c = f.starCore + f.starCores * 4;
  c[0] = (float)x; c[1] = (float)y; c[2] = hover && mass > 0.006f ? mass * 0.2f : mass; c[3] = hover ? 1.f : 0.f;
  f.starVel[f.starCores * 2] = (float)vx; f.starVel[f.starCores * 2 + 1] = (float)vy;
  f.starCores++;
}

// One tracked hand, in the conventions of MediaPipe's HandLandmarker result (which
// the web version's hand logic was written against): 21 landmarks in MediaPipe
// order, x and y normalised 0..1 in the unmirrored camera image with the origin at
// the top left. Depth is not available natively, so there is no z.
struct HandObs { float x[21], y[21]; bool right; float score; };
