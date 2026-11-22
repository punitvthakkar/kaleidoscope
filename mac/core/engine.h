// Per-frame engine: the native port of the web version's Palette, Fold, Motion,
// Pens, the hand-gesture half of Inputs, and Main (index.html).
//
// Threads: every function except engine_step may be called from any thread. Posts
// land in a small mutex-guarded inbox that engine_step drains on the render thread.
// Timestamps everywhere are seconds on one clock (CACurrentMediaTime).
#pragma once
#include "state.h"

struct Engine;

Engine* engine_create();
void engine_destroy(Engine* e);
// Render thread, between steps: an independent copy of the engine as it is now (its own
// inbox and events), for stepping ahead without touching the live one. Free with engine_destroy.
Engine* engine_clone(Engine* e);

// ---- interface (the web overlay stays the authority on params and system) ----
// Enum values come as str, numbers and booleans (0/1) as num. Float params that are
// not stepped ease linearly to the new value over 1/60 s; the rest snap.
void engine_set_param(Engine* e, const char* key, double num, const char* str);
void engine_set_system(Engine* e, const char* key, double num);

// ---- pointer input, in view points with the origin at the top left ----
enum { PTR_MOVE, PTR_DOWN, PTR_UP, PTR_CANCEL, PTR_LEAVE };
// isMouse: the web's pointerType === 'mouse'. A mouse pen persists across strokes
// (and hovers when inkHover is on); any other pointer is also a finger on the glass.
// Send every sample: samples between frames are queued like coalesced events.
void engine_pointer(Engine* e, int id, int kind, float x, float y, double t, bool isMouse);
// Wheel deltas in the web's WheelEvent convention (pixels, dy > 0 scrolls down):
// pass -scrollingDeltaX / -scrollingDeltaY from NSEvent.
void engine_scroll(Engine* e, float dx, float dy, bool shift);
// Trackpad twist and pinch: routed through the web's two-finger maths.
// magnify: NSEvent.magnification (scale *= 1 + m). rotate: radians, counterclockwise
// positive (NSEvent.rotation is degrees: pass rotation * pi / 180).
void engine_gesture_begin(Engine* e, double t);
void engine_magnify(Engine* e, float m, double t);
void engine_rotate(Engine* e, float radians, double t);
void engine_gesture_end(Engine* e);

// ---- hands ----
// n is 0..2; 0 means no hand is seen. t is the camera frame's timestamp.
void engine_hands(Engine* e, const HandObs* hands, int n, double t);
void engine_set_hands_enabled(Engine* e, bool on, double now);
void engine_set_two_hands(Engine* e, bool on);
void engine_set_camera_aspect(Engine* e, float widthOverHeight);   // default 16/9

// ---- commands ----
void engine_clear_pens(Engine* e);          // web Pens.clear (the renderer clears its own buffers)
void engine_reset_breathing(Engine* e);     // web Main.breathe
void engine_set_paused(Engine* e, bool paused);
void engine_set_cam_on(Engine* e, bool on); // Frame.camOn
void engine_set_thermal(Engine* e, int state);   // 0 nominal .. 3 critical

// Any thread (the app calls it from main); applied at the next step.
void engine_resize(Engine* e, float viewW, float viewH, float backingScale);

// ---- render thread ----
// dt is clamped to 0.05 like the web. bass/mid/high are the mic levels (web Sound.level).
// While paused this only drains the inbox and returns the previous frame: skip rendering.
const Frame& engine_step(Engine* e, double dt, float bass, float mid, float high);
// From the command buffer's completion handler: GPU seconds the frame took, and the
// display's frame interval.
void engine_report_gpu(Engine* e, double gpuSeconds, double frameInterval);

// ---- outbound events (web: Sound.pluck, UI.nextChamber, UI.breathCue, mn.haptic) ----
enum { EV_PLUCK, EV_NEXT_CHAMBER, EV_BREATH_CUE, EV_HAPTIC };
enum { HAPTIC_TICK, HAPTIC_CONFIRM };
struct EngineEvent {
  int type;
  float x, y, v;     // pluck: position in p-space and strength
  int a, b;          // breath cue: a = inhale (0/1), b = count; haptic: a = HAPTIC_*
};
bool engine_poll_event(Engine* e, EngineEvent* out);

// ---- state, published at the end of each engine_step ----
struct EngineStats {
  // sizes (web Main.resize): drawable = canvas, render = Renderer.resize(w, h, echoScale)
  int drawW, drawH, renderW, renderH, echoW, echoH;
  float echoScale;
  int capW, capH, capEchoW, capEchoH;   // the largest render and echo sizes any detail level uses here
  int ssTaps, rmSteps;        // Renderer.setDetail(taps, steps)
  float fps, quality, refreshHz, gpuLoad;   // gpuLoad: GPU time / frame budget, last second
  int level;
  bool halfRate;              // web halved(): coolHands with hands on; render every other frame
  // Pens and sound
  int particles, ripples, taps;
  float painted, energy, lung;   // lung is -1 unless the breathing guide is on (web Sound.update)
  int motionUsed;
};
struct HandsState {              // web Inputs.state plus the grip markers (#grab0, #grab1)
  bool cam, hands, twoHands;
  int grabMode;                  // 0 none, 1 one hand holds the glass, 2 both
  bool grabOn[2];
  float grabX[2], grabY[2];      // 0..1 of the view, origin top left
};
EngineStats engine_stats(Engine* e);
HandsState engine_hands_state(Engine* e);
