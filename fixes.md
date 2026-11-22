# Fixes from the 120 fps review (hands-dev)

Work through these in order and tick each one off when it's done. Line numbers are from the working tree as of 2026-11-22; if they have moved, search for the symbol instead. `mac/core/*` is compiled into both the Mac and Android apps, so a fix there fixes both.

Rules:

*   Change only what each item says.
*   After each section, build the Mac app and the Android app and load the web page. Fix anything that broke before moving on.

## 1\. Bugs (do first)

- [x] **Settings flags overflow.** `mac/core/engine_impl.h:180`, `mac/core/engine.cpp:36` and `:200-207`. * Problem: `dirty` is a `uint64_t`, but `PARAM_COUNT` is 69, so `chroma`, `sheen`, `grain`, `vignette` and `exposure` are never applied. * Fix: change `dirty` to `std::bitset<PARAM_COUNT>`, update the drain loop to match, and add `static_assert(PARAM_COUNT <= 128)` or similar.
- [x] **Android ladder with no GPU timer.** `android/native/host.cpp:509-514`. * Problem: when `R.ladGpuN == 0`, the host still reports a load of 0.3, so detail is never lowered. * Fix: in that case, skip `engine_report_gpu` so the engine falls back to judging by fps alone.
- [x] **Web: double tap on Hands.** `index.html` `setHands` (~4290). * Problem: two taps during the camera prompt create two workers, and only one is ever stopped. * Fix: after `await openStream()`, add `if (worker) return;`. * Also call `URL.revokeObjectURL(url)` right after `new Worker(url)`.

## 2\. Frame hitches

- [x] **Mac brightness hitch.** `mac/render/renderer.mm:711`. * Problem: `hdrBound()` (~1 ms) reruns whenever the HDR headroom changes by any amount. * Fix: round the peak to steps of 0.05 before comparing it with `r->peakFor`.
- [x] **Web Visit flask.** `index.html` `setFill` (~6115). * Fix: keep the `#flask` element in a variable. * Write `--fill`, `aria-valuenow` and the `full` class only when `Math.round(v*100)` changes.
- [x] **Web Skeleton overlay.** `index.html` ~1738-1800. * Pre-render one glow image per tint (a small offscreen canvas) and draw it with `drawImage` and `globalAlpha`, instead of calling `createRadialGradient` every frame. * Draw the 21 joints as one path with a single `fill()`. * Move the `[[7,.06],[1.3,.5]]`\-style arrays to constants, and replace `TIPS.forEach` with a `for` loop. * Use the canvas size that is already tracked, not `innerWidth`/`innerHeight`.
- [x] **Render targets reallocated on every ladder step**, in all three apps: * Web: `Renderer.resize`. * Mac: `renderer.mm:257-289`. * Android: `renderer.cpp:384-422`. * Fix: allocate at the largest render size once, and render each ladder level into a smaller viewport, scaling UVs to match. Reallocate only when the window itself changes size.
- [x] **Web refresh rate measured once.** `index.html` ~6704-6740. * Fix: keep re-measuring `refreshHz` from the rolling median `dt`. * After a failed trial, double `cooldown` each time (8, 16, 32…, up to 120 s) instead of a fixed 8.
- [x] **Web fps count hides stalls.** `index.html` ~6719. * Fix: count fps from the raw `now - last`. Keep the 0.05 cap only on the `dt` given to the simulation.
- [x] **Web unfocused window at 30 fps.** `index.html` ~6721. * Fix: replace `!document.hasFocus()` with `document.visibilityState !== 'visible'`.
- [x] **Half-rate mode ("coolHands").** * Mac (`view.mm:183`): set `_link.preferredFrameRateRange` to half the rate, and remove the `_tick` parity skip. * Android (`host.cpp:433`): call `ANativeWindow_setFrameRate(window, refreshHz/2, …)` when `halfRate` changes, and remove the parity skip.
- [x] **Timing that assumes 60 Hz.** Base these on elapsed time: * `mac/core/synth.cpp:359`: use `k = pow(0.45, dt*60)`. * `synth.cpp:464` (`tick % 8`) and web `Sound.update` (~4721, every 8th frame): use a time accumulator that fires every 0.125 s.
- [x] **Recording does double work at 120 Hz.** * Mac (`renderer.mm:716-727`): render into `capTex` only on frames the recorder will keep, i.e. frames on its 60 fps grid. Add a `recorder_wants(hostTime)` check. * Mac: put the HLG conversion (`recorder.mm:407-419`) in its own command buffer, committed right after the frame's. * Android (`recorder.cpp:276-341`): apply the same "only on kept frames" check. * Android: end the GPU timer query after the recorder hook, not before it. * Android: cap the encode size at 1080p.
- [x] **Android render-thread priority.** `host.cpp` `renderMain` (~557). * Call `setpriority(PRIO_PROCESS, gettid(), -10)`. * Create an `APerformanceHintSession` (API 33+, behind `__builtin_available`) and report the work duration each frame.
- [x] **Android framebuffer invalidation.** `renderer.cpp` 791, 815, 889, 935, 1008. * Call `glInvalidateFramebuffer` on targets the pass fully overwrites. * Not on the bloom upsample, which blends into the existing contents.
- [x] **Mipmaps every frame.** Web `index.html:3541`, Mac `renderer.mm:611`. * Fix: skip generating mipmaps for modes that barely shrink the image (start with Pool, Hyperspace and Planet), using a per-mode flag.
- [x] **Android snapshot.** `renderer.cpp:1070-1107`. * Read the pixels into a PBO with a fence, and map it a frame later. * Move `encodePNG` to the `saveImage` worker thread.
- [x] **Export blocks the render thread** (Mac `renderer.mm:784-801`, Android `renderer.cpp:1119-1135`). * Spread the 60 frames over several ticks. * Step a copy of the engine, not the live one. * Android: don't `postWait` from the main thread while an export runs.

## 3\. Memory

- [x] **Mac audio tap.** `recorder.mm:425`. * Problem: the tap takes a lock and retains objects on the audio thread. * Fix: give it a raw ring pointer, published atomically at start and cleared only after `audio_film_end()`.
- [x] **Mac display-link callback.** Wrap the callback body in `view.mm:179-227` in `@autoreleasepool`.
- [x] **Mac skeleton event.** `view.mm:247-256` and `web.mm:138`. * Pass the C JSON string straight to `evaluateJavaScript`, without parsing and re-serialising it.
- [ ] **Mac camera buffers.** Release `camRef` once it has been copied into `camHeld`.
- [x] **Mac recorder timer.** `recorder.mm:240`: change the polling interval from 5 ms to 25 ms.
- [x] **Android veil and backup targets.** `renderer.cpp:395-396`. * Allocate them only when a veil or undo starts, outside the frame, and free them afterwards.
- [x] **Android uniforms.** `renderer.cpp:76` `Prog::at`. * Look up uniform locations once at link time and store them in a struct per program. No name lookups during a frame.
- [x] **Android camera copy.** `renderer.cpp:557`. * Copy only when `updateTexImage` delivered a new frame.
- [x] **Android per-frame duplicates.** * Call `pollQueries` once per frame (it is called at 1090 and 1154). * Take one `engine_stats` snapshot per frame.
- [x] **Android JNI.** `host.cpp:844-866`. * Add `ExceptionCheck`/`ExceptionClear` after each `CallStaticVoidMethod`.
- [x] **Web hot-path allocations:** * `Visit.tick` (~6186): replace `queue.slice()` with a backwards loop over the queue itself. * `Pens.lead` (~3903): return a reused scratch array instead of a new `[x, y]`. * `onHands` (~4418): reuse one `Float32Array(42)` per hand slot instead of `flatMap`/`map`/`reduce`.

## 4\. Cleanup (dead code and duplication)

- [x] **Web: stop compiling unused sky code.** Include the `uSky`/`skyLight` GLSL only when `STARS_OK` is set (via a `#define`).
- [x] **Web: name the modes.** * Add `const M = Object.fromEntries(MODES.map((m,i)=>[m,i]))` and use it instead of bare numbers like `fold.mode === 6`. * Add matching `#define`s to the GLSL.
- [x] **Web: share the wedge code.** Factor out a `wedge()` JS helper so mode 11 and mode 1 use the same code.
- [x] **Android: delete dead code.** * `stubs.cpp` and `renderer_stub.cpp`. * The always-true `__has_include` guards and their `#else` branches. * The `kHdrScreen` branch, `HostGl.config` and `renderer_echo_size`.
- [x] **Android: put debug code behind a flag.** Guard `render/debug.cpp`, `mini_engine.inc`, `meanRGB` and `gSkyDebug` with `-DFX_DEBUG`, passed only in dev builds.
- [x] **Android: build a release variant** (minified, not debuggable) for performance testing.
- [x] **`mini_engine.inc`.** Move it into a header under `mac/render` and include that header from both the Mac and Android code. This also restores the missing `starKind == 4` line.
- [x] **Mac: delete unused functions.** `renderer_gpu_seconds`, `renderer_echo_size`, `renderer_echo_scale`, `audio_hush`, `audio_unhush`, `audio_sound_on`, `recorder_stats`. * If tests use any of them, update the tests too.
- [x] **Mac: move `gSlowMs` out of the production path.** Put it behind a test-only `#ifdef`.
- [x] **Hand-tracking cadence is written twice.** Keep the `camera.mm` version. * Delete `engine_hands_interval` and its atomics, and `Hands::heardAt`/`fastUntil`, and update the tests.
- [x] **Mac: read `renderer_max_size()` directly** in `web.mm:92`, without the hop to the render thread.
- [x] **Shared helpers instead of copies.** * One texture helper each on the Mac (four builders: `target`, `ensure`, the stars block, `make`) and Android (`makeTarget`/`skyTarget`). * One EGL config picker and one shader-compile helper on Android.
- [x] **`engine.cpp:180-189`.** Make `rampLeft` a `double` and remove the workaround branch.
- [x] **`engine.cpp:33`.** Don't call `param_find` twice; pass its result into `params_set`.

## 5\. Comments

Delete comments that only restate the code. Cut essay comments to one line saying why, and move longer reasoning to `next.md` if it's worth keeping.

- [x] **Web narration:** `index.html` 1747, 1749, 1766, 2926, and the repeated block at 1759-1760.
- [x] **Web stale comment:** `index.html:5386` says the last pip "turns to the next seven". Delete that clause; the code opens every place at once.
- [x] **Web duplicate:** the "fold isnan away" note appears at 2855 and 3097. Keep one.
- [x] **Essays to cut:** `index.html` 2516-2521 and 2870-2873, `stars.metal:1-32`, `engine.cpp:325-331` and `396-409`, `renderer.mm:503-511`, `host.cpp:275-281`, `497-503` and `779-796`.
- [x] **Android:** fix the garbled comment at `recorder.cpp:99`. * Remove the "agent A/B/C" and "phase 2/3" references in `host.h`, `caps.h`, `renderer.h`, `recorder.cpp` and `host.cpp:726`.
- [x] **Mac:** fix `engine.h:54` (`engine_resize` is called from main). * Remove the `engine_hands_interval` docs at `engine.h:44` once that code is gone. * Remove the dangling note at `web.mm:245`.
- [x] **Keep** comments that state real constraints: the recording memory guard (`index.html` ~~6355) and the resize cap (~~6608).

## Done when

*   All boxes are ticked.
*   The Mac app, the Android app and the web page all build and run.
*   Each chamber holds 120 fps on the M5 Max with hands on.
*   Changing screen brightness causes no hitch.
*   Recording shows no uneven frames.
*   run the mac and android tests in the background without taking over screen