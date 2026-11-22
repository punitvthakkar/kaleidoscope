// The native side of the app shell (android/native/host.cpp): the render
// thread, EGL, the engine, sound out, and the bridge to Kotlin. This header is the
// whole surface between Kotlin, the host, the renderer and films / Listen.
//
// ONE CLOCK: seconds of CLOCK_MONOTONIC everywhere (host_now). MotionEvent event
// times, Choreographer frame times and AAudio timestamps are all on it.
//
// STATE MACHINE (owned by the render thread; every transition is one message):
//   surface: none | ready        app: foreground | background
//   film:    idle | recording | stopping
// Frames are drawn only in (ready, foreground). Going to the background hushes
// sound, stops a film (it is saved), closes camera and microphone.
//
// THREADS
//   main (Kotlin UI)   host_start, surface and lifecycle calls, host_message, touch
//   render             everything GL, engine_step, renderer_*, frame hook
//   audio              AAudio callbacks only
// Cross-thread traffic: the engine's own inbox (engine_* setters are thread-safe)
// and host_post_render. Nothing else is shared.
#pragma once
#include <cstdint>
#include <functional>
#include "caps.h"

struct Engine;
struct Renderer;

double host_now();

// ---------------- Kotlin -> native (JNI wrappers in host.cpp, 1:1) ----------------
// Class: app.fractonaut.Host (Kotlin `object`), library "fractonaut".
struct DeviceFacts {     // the Kotlin half of Caps
  int sdk, displayW, displayH; float refreshHz, density;
  bool displayHlg; float displayMaxNits;
  const char* hevcEncoder; bool hevcMain10; const char* avcEncoder;
  const char* filesDir;  // app-private directory (shaders unpacked here, debug dumps)
  int handsDelegate;     // Caps.handsDelegate: the tracker delegate chosen by measurement
};
void host_start(const DeviceFacts&);                    // once, onCreate; starts the render thread
void host_surface_created(void* aNativeWindow);         // takes its own reference
void host_surface_changed(int w, int h);                // pixels
void host_surface_destroyed();                          // returns after the render thread let go of it
void host_foreground(bool on);                          // onStart / onStop
// Pointers over the picture, in pixels (the host converts to dp for the engine).
// kind is the engine's PTR_*; t is MotionEvent.getEventTime() / 1000.
void host_touch(int id, int kind, float x, float y, double t);
// About 1 Hz. status (PowerManager.getCurrentThermalStatus) drives the engine's thermal
// cap; headroom (getThermalHeadroom) is only printed in the stats line.
void host_thermal(float headroom, int status);
// One message from the page (see mac/web/shim.js PROTOCOL). Kotlin parses the JSON
// batch and forwards each message in order; `set` arrives as one call per key.
void host_set_param(const char* key, double num, const char* str);
void host_set_system(const char* key, double num);
// Every other message type: name plus its fields, flattened. Absent fields are 0 / "".
struct PageMessage { const char* type; bool on; int id; double num; const char* str; int w, h; };
void host_message(const PageMessage&);
// Camera light and Hands. Kotlin owns Camera2 and the tracker, answers the
// page's `camera` / `hands` messages itself, and drives the engine through these.
// The camera picture: a SurfaceTexture created detached (SurfaceTexture(false)); the
// render thread attaches it to a GL_TEXTURE_EXTERNAL_OES texture, updates it every
// frame and passes it to renderer_set_camera. aspect: the upright image's width/height.
// Null stops; returns after the render thread let go of it (main thread).
void host_camera_texture(void* aSurfaceTexture, float aspect);   // ASurfaceTexture*, the host takes it
void host_hands_enabled(bool on);                       // engine_set_hands_enabled
void host_camera_aspect(float widthOverHeight);         // engine_set_camera_aspect (upright image)
bool host_grip_held();                                 // a hand holds the glass; any thread
// One tracked frame (tracker thread): MediaPipe's landmarks in the upright, unmirrored
// camera image, as the web passes them; t is the frame's time on host_now's clock.
struct HandObs;
void host_hands(const HandObs* hands, int n, double t);
// Permission outcome for a `mic` request Kotlin had to ask for.
void host_permission(int id, bool granted);
// Debug intent extras (never user-facing), forwarded once at launch:
//   fx.stats (bool)      one line per second to logcat, tag "fx":
//                        "stats fps=… gpu_ms=… size=WxH level=… dropped=… mem_mb=… thermal=… film=…"
//   fx.script (string)   JavaScript run in the page after `ready`
//   fx.stroke (bool)     paint one synthetic stroke across the picture
//   fx.chambers (bool)   render each chamber in turn and dump snapshots to
//                        <filesDir>/chambers/<slug>.png, logging "chamber <slug> gpu_ms=…"
//   fx.record (int)      record a film of that many seconds, then log "film <path> …"
//   fx.hands (bool)      turn Hands on or off through the page (Kotlin runs it)
void host_debug(const char* key, const char* value);

// ---------------- native -> Kotlin (any thread; Kotlin hops to its UI thread) ----------------
void host_page_state(const char* json);                 // window.__fx.state(json)
void host_page_event(const char* name, const char* json);   // window.__fx.event(name, json); json may be "{}"
void host_reply(int id, bool ok, const char* error);    // error: null | "denied" | "unavailable" | "failed"
void host_haptic(int kind);                             // engine HAPTIC_*
void host_awake(bool on);
// Ask Kotlin for a runtime permission ("mic" | "camera"); the answer comes back as
// host_permission(id, granted). Already granted answers at once.
void host_request_permission(const char* what, int id);
// Saved files go to MediaStore (Pictures/fractonaut for image/png, Movies/fractonaut
// for video/mp4) as pending entries. Blocking; never call on the render or audio thread.
int  host_media_open(const char* file, const char* mime);   // fd to write, or -1
void host_media_close(int fd, bool keep);               // publish (keep) or delete the entry
void host_share(const char* file);                      // system share sheet for a saved film
void host_copy(const char* text);

// ---------------- services for the renderer, films and Listen ----------------
const Caps& host_caps();                                // valid once the render thread logged it
// App-private directory. Kotlin copies the APK's assets `shaders/` and `chambers.txt`
// into it at every start, so native code reads plain files: <dir>/shaders is the
// renderer's shaderDir. Debug dumps go under it too (pulled with `run-as`).
const char* host_files_dir();
Engine* host_engine();
// Runs `fn` on the render thread between frames, context current. The single queue
// for everything that is not an engine setter.
void host_post_render(std::function<void()> fn);

// EGL, for the film's encoder surface. Render thread only.
struct HostGl { void* display; void* context; void* screenSurface; Renderer* renderer; };
HostGl host_gl();
// Called on the render thread after each frame's final pass and before the swap, with
// the frame's display time (CLOCK_MONOTONIC nanoseconds). The hook may make another
// surface current but must make the screen surface current again before returning,
// and returns true if it did (the host then makes sure). It must never wait. Null clears it.
struct FrameInfo { int64_t displayNanos; int w, h; float headroom; };
void host_set_frame_hook(std::function<bool(const FrameInfo&)> hook);

// Sound for films, as mac/platform/audio.h: the tap runs on the audio thread (no
// locks, no allocation) with float PCM at audio_sample_rate(); `nanos` is when the
// first frame is heard (CLOCK_MONOTONIC).
using AudioFilmTap = void (*)(const float* left, const float* right, int frames, int64_t nanos);
bool audio_film_has_sound();
double audio_sample_rate();
void audio_film_begin(AudioFilmTap tap);
void audio_film_fade(std::function<void()> silent);     // fades over 0.3 s; `silent` on the render thread after 0.42 s
void audio_film_end();                                  // no tap calls after this returns

// ---------------- films and Listen, called by the host ----------------
// Films (android/native/record/). Events recordStarted / recordSaved / recordFailed
// are sent by the recorder itself through host_page_event.
void record_start(const char* file);                    // from the `record {on:true}` message
void record_stop();                                     // `record {on:false}`, and on going to background
bool record_active();
struct RecordStats { int w, h; int64_t written, dropped; };
RecordStats record_stats();                             // for the stats line
// Listen (android/native/audio_in.cpp): AAudio input feeding synth_mic_push, open
// only while on. False if the microphone could not be opened.
bool audio_in_start();
void audio_in_stop();
