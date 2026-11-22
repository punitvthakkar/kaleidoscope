// GLES 3 port of the web Renderer (index.html, `Renderer`) with the Mac's native
// changes (mac/render/renderer.h is the model): raymarch -> feedback ping-pong ->
// ink -> scene -> bloom -> final, with the flash guard and brightness ceiling.
// Implemented in android/native/render/.
//
// Threads: every function belongs to the render thread, with the host's EGL
// context current. Other threads reach the renderer through host_post_render.
// The context outlives window surfaces, so nothing here is rebuilt when the app
// goes to the background and returns.
//
// The renderer never touches EGL, never swaps, and keeps no state outside
// `Renderer`. It leaves framebuffer 0 bound on return from render / draw_final.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "caps.h"
#include "../../mac/core/state.h"

struct Renderer;

enum { RENDERER_MAX_SEG = 16384 };   // ink segments per frame (web: Renderer.MAX_SEG)

// Compiles the shaders extracted from index.html at build time (shipped as assets;
// `shaderDir` is a directory of plain files the host unpacked or a path the build
// defines, see android/build.sh). Null on failure, with the full compiler log in err.
Renderer* renderer_create(const Caps&, const char* shaderDir, char* err, int errLen);
void renderer_destroy(Renderer*);

// The largest render and echo sizes the detail ladder can ask for (EngineStats.cap*).
// Targets are allocated at these sizes and reallocated only when they change; every
// ladder level renders into the top-left part of them.
void renderer_reserve(Renderer*, int capW, int capH, int capEchoW, int capEchoH);
// Render resolution and echo (feedback) scale, as Renderer.resize(w, h, fs). The
// feedback image is resampled, so nothing resets. Reallocates only beyond the reserved
// sizes. False if out of GPU memory.
bool renderer_resize(Renderer*, int w, int h, float echoScale);
void renderer_set_detail(Renderer*, int taps, int steps);
void renderer_clear(Renderer*);            // keeps an undo copy for 8 s
void renderer_restore(Renderer*);
void renderer_start_veil(Renderer*, float duration);
void renderer_fade_veil(Renderer*, float dt);

// Renders one frame; the final pass fills framebuffer 0 at targetW x targetH (the
// window surface), encoded as `out`. `headroom` >= 1; with OUT_SDR, or headroom 1,
// the picture equals the web's. The host swaps.
void renderer_render(Renderer*, const Frame&, int targetW, int targetH, Output out, float headroom);
// Closes the frame's GPU timer: after renderer_render and the frame hook, before the swap.
void renderer_frame_end(Renderer*);
// After the swap: the work that must not delay the frame (a pending still's readback,
// one step of a running export, finished readbacks handed out).
void renderer_after_swap(Renderer*);
// True while an export runs or a still is on its way.
bool renderer_busy(Renderer*);

// Film: draws the final pass of the frame just rendered again, into whatever
// framebuffer is bound (the encoder's surface, made current by the recorder), at
// w x h, encoded as `out`. No other state changes.
void renderer_draw_final(Renderer*, int w, int h, Output out, float headroom);

// A still: RGBA8 rows bottom-up (as glReadPixels), w x h. Empty on failure.
using RendererPixels = std::function<void(std::vector<uint8_t> rgba, int w, int h)>;
// The next rendered frame, tone-mapped for SDR exactly as the web saves it, read back
// without a stall (a frame or two later). `done` runs on the render thread.
void renderer_snapshot(Renderer*, RendererPixels done);
// RGBA8 rows bottom-up -> 8-bit RGB PNG. Any thread. Empty on failure.
std::vector<uint8_t> renderer_encode_png(const uint8_t* rgba, int w, int h);

// The web's exportHiRes: renders `frames` steps (web: 60 at 1/60 s) at w x h in
// targets of its own, one step per renderer_after_swap, and hands out the last.
// `step(dt)` advances an engine and returns its frame; it and `done` are kept until
// the export ends. `done` gets empty pixels if the GPU ran out of memory or an export
// is already running.
void renderer_export_hires(Renderer*, int w, int h, int frames,
                           std::function<const Frame&(float dt)> step, RendererPixels done);

// Camera light: a GL_TEXTURE_EXTERNAL_OES texture the host updates from a
// SurfaceTexture, its 4x4 transform (column-major, SurfaceTexture.getTransformMatrix),
// and the upright image's width / height (web: videoWidth / videoHeight).
// tex 0 turns it off. The matrix is copied.
void renderer_set_camera(Renderer*, uint32_t tex, const float* transform16, float aspect);

// GPU seconds of the latest completed frame (timer query, collected at the start of
// each renderer_render); 0 until one completes or when Caps.timerQuery is false.
double renderer_gpu_seconds(Renderer*);

void renderer_size(Renderer*, int* w, int* h);        // render resolution
float renderer_echo_scale(Renderer*);
int renderer_max_size(Renderer*);
