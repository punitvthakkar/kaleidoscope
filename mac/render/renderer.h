// Metal port of the web Renderer (index.html, `Renderer`): raymarch → feedback
// ping-pong (mipmapped) → ink → scene (supersampled fold, filigree, veil) → bloom →
// final, with the flash guard and brightness ceiling. Output is extended linear
// sRGB (1.0 = SDR white) for an rgba16Float, EDR-enabled CAMetalLayer.
//
// Threads: renderer_upload_camera, renderer_clear/restore/start_veil and
// renderer_snapshot may be called from any thread. Everything else belongs to the
// render thread.
#pragma once
#import <Metal/Metal.h>
#include <CoreVideo/CoreVideo.h>
#include "../core/state.h"

struct Renderer;

enum { RENDERER_MAX_SEG = 16384 };   // ink segments per frame (web: Renderer.MAX_SEG)

// Compiles shaders/*.metal from shaderDir. Returns null and sets *error on failure
// (the error carries the compiler's full log).
Renderer* renderer_create(id<MTLDevice> device, NSString* shaderDir, NSError** error);
void renderer_destroy(Renderer*);

// Render resolution and echo (feedback) scale, as Renderer.resize(w, h, fs). The
// feedback image is resampled, so nothing resets. False if out of GPU memory.
// cap*: the largest sizes this window's detail levels use (EngineStats cap*). Targets are
// allocated at those once; 0 allocates at exactly this size.
bool renderer_resize(Renderer*, int w, int h, float echoScale, int capW = 0, int capH = 0, int capEchoW = 0, int capEchoH = 0);
void renderer_set_detail(Renderer*, int taps, int steps);
void renderer_clear(Renderer*);            // keeps an undo copy for 8 s
void renderer_restore(Renderer*);
void renderer_start_veil(Renderer*, float duration);
void renderer_fade_veil(Renderer*, float dt);

// Encodes and commits one frame. The final pass fills `target` (the web's canvas;
// any size, usually the drawable's texture, rgba16Float). `drawable`, if given, is
// presented. `headroom` is the screen's current EDR headroom (>= 1); with 1 the
// picture is the web's. Blocks while two frames are already in flight.
void renderer_render(Renderer*, const Frame&, id<MTLTexture> target, id<MTLDrawable> drawable, float headroom);

// Film capture: while on, the final pass renders into an rgba16Float texture of the
// target's size (extended linear sRGB) which is then copied to the target.
void renderer_set_capture(Renderer*, bool on);
id<MTLTexture> renderer_capture_texture(Renderer*);
// Per-frame hook for the film recorder: while capture is on, called on the render
// thread after the final pass is encoded and before the frame's command buffer is
// committed, with that buffer and the capture texture. nil clears it.
void renderer_set_frame_hook(Renderer*, void (^)(id<MTLCommandBuffer> cmd, id<MTLTexture> capture));
// Optional, render thread: whether the hook wants this frame. Frames it does not want
// render straight to the target, without the capture texture or the hook. nil: every frame.
void renderer_set_capture_filter(Renderer*, bool (^)(void));

// The next rendered frame as an 8-bit sRGB PNG, tone-mapped for SDR exactly as the
// web saves it. `done` runs on a Metal completion thread.
void renderer_snapshot(Renderer*, void (^done)(NSData* png));

// The web's exportHiRes(longEdge): renders `frames` steps (web: 60 at 1/60 s) at
// longEdge on the target's aspect, saves the last as PNG, then returns to the old
// size. `step(dt)` advances the engine and returns its frame. Call on the render
// thread between frames. `done` gets nil if the GPU ran out of memory.
void renderer_export_hires(Renderer*, int longEdge, int frames, const Frame* (^step)(float dt),
                           void (^done)(NSData* png, int w, int h));
// The same, spread over the live frames so they never wait: begin, then one
// renderer_export_step per live frame (render thread) with a frame from a copy of the
// engine (engine_clone), while renderer_export_active. The export draws into targets of
// its own, so the live picture is untouched. begin returns false if one is running.
bool renderer_export_begin(Renderer*, int longEdge, int frames, void (^done)(NSData* png, int w, int h));
bool renderer_export_active(Renderer*);
void renderer_export_step(Renderer*, const Frame& f);

// Camera frame (BGRA, IOSurface-backed), zero-copy. Replaces uploadCamera(video).
void renderer_upload_camera(Renderer*, CVPixelBufferRef);

// Waits until every frame submitted so far has finished on the GPU.
void renderer_finish(Renderer*);

// Optional per-frame callback with the frame's GPU seconds (runs on a Metal completion thread).
void renderer_set_gpu_time_handler(Renderer*, void (^)(double seconds));

void renderer_size(Renderer*, int* w, int* h);        // render resolution
int renderer_max_size(Renderer*);
