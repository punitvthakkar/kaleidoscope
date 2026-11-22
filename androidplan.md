# fractonaut on Android: plan

Target: Galaxy S22 Ultra, Snapdragon 8 Gen 1 (Adreno 730), over USB (debugging on). Orchestrator: Fable. Workers: at most 3 Opus subagents in total, named A, B, C below (B is resumed for phase 3, not replaced). The owner checks on the phone; nobody else touches it.

## Goal

The Mac app's result on the phone: same interface and copy, native picture at the display's refresh rate (120 Hz), sound, films, bounded memory. HDR on screen and in films where the device proves it can; standard range otherwise.

## Rules

- **Phone is read-only.** Allowed: install, launch (with intent extras), `logcat`, `android screen capture`, `dumpsys`, pulling files. Never taps, swipes, key events or settings changes. Anything needing a finger is listed for the owner.
- **YAGNI.** No engine, no Vulkan, no Compose, no Oboe, no Swappy, no dependency injection. One activity. The only dependency beyond the Android Gradle plugin, Kotlin and the NDK is MediaPipe's hand tracker (phase 3); anything else needs a written reason.
- **No implicit state.**
  - Every tool path, SDK package and version is pinned in `android/build.sh` and the Gradle files; nothing relies on shell environment, an IDE, or a running daemon.
  - Device capabilities are probed once at start into one `Caps` struct, logged in one line, and passed down. No code asks the system again later or assumes a capability.
  - Lifecycle is one explicit state machine (surface: none / ready; app: foreground / background; film: idle / recording / stopping). Each piece of state has one owner and one thread; cross-thread traffic goes through the engine's existing inbox or a single message queue.
  - Agents state in every report what was run on the device and what was not. The orchestrator re-runs each gate itself before accepting it.
- **Mac build must keep passing** (`mac/build.sh`) after any change to shared code.
- **Copy is frozen.** No new user-visible strings. Camera and microphone use the system's own permission dialogs.
- **Token economy.** Agents read headers and the named line ranges, not whole files. Reports under 300 words. No documentation files. The orchestrator reads reports and device evidence, not agent transcripts.

## What already exists and is reused unchanged

| Part | Path | Use on Android |
|---|---|---|
| Engine (fold, pens, hands logic, ladder) | `mac/core/*.cpp`, `engine.h`, `state.h` | Compiled by the NDK as is |
| Parameter generator | `mac/tools/gen_params.mjs` | Same build step |
| Synthesiser | `mac/core/synth.*` | As is, behind an AAudio callback |
| Interface + protocol | `mac/tools/make_ui.mjs`, `mac/web/shim.js` | Same page in a transparent WebView; same messages |
| Shaders | GLSL ES 3.00 inside `index.html` (1879–2551) | Extracted at build time and run as is: WebGL2 is OpenGL ES 3.0 |
| Renderer logic | `index.html` 2552–3037, and `mac/render/renderer.mm` for the native changes | Ported to C++ on GLES 3 |
| HDR tone curve, flash guard changes | `mac/render/shaders/post.metal` | Applied to the extracted final shader as one anchored patch |
| Hand landmarks format | `HandObs` in `state.h` is already MediaPipe's layout | MediaPipe results pass straight to `engine_hands` |
| HLG colour maths | `mac/platform/recorder.mm` | Reference for the film pass |

New code lives in `android/`. Shared code stays where it is; no moves or renames.

## Toolchain (verified on this Mac, 2026-11-15)

- Present: `android` CLI 1.0 (`~/.local/bin/android`), SDK at `~/Library/Android/sdk` with platform 36, build-tools 36.0.0, platform-tools (`adb` is there but not on PATH), Gradle 9.7.1, node.
- Missing: the NDK and a system JDK (`/usr/bin/java` finds none; use the JDK Homebrew's Gradle runs on, by explicit path).
- Install the NDK with `android sdk install`. No CMake: `android/build.sh` calls the NDK's compiler directly, as `mac/build.sh` does, and hands Gradle the finished library to package. Deploy with `android run --apks=… --device=…`. Look with `android screen capture`.

## Phase 0: orchestrator, before any agent

1. Install the NDK; write `android/build.sh` that sets every path explicitly, builds, and prints the APK path. No separate test app: the script is first proven by agent B's solid-colour milestone.
2. Probe the phone and paste the results into the "Device facts" section at the end of this file: confirm the SoC and GPU, Android version, display modes and HDR types, GLES version and the extensions that matter (`OES_EGL_image_external_essl3`, `EXT_color_buffer_half_float`, `EXT_disjoint_timer_query`, `EGL_EXT_gl_colorspace_bt2020_hlg`, `EGL_EXT_gl_colorspace_scrgb_linear`, `EGL_ANDROID_recordable`, `EGL_ANDROID_presentation_time`), and HEVC encoders with their profiles (Main10, HLG support).
3. From those facts decide and record: HDR on screen yes/no and which EGL colour space; film format (HEVC Main10 HLG, or the standard-range fallback); starting detail level.
4. Write the two contracts agents build against: `android/native/renderer.h` (same shape as the Mac's: create, resize, set detail, render(Frame, headroom), veil, clear/restore, snapshot, capture hook, camera texture, GPU time) and `android/native/host.h` (what Kotlin calls: surface created/changed/destroyed, touch, message from page, pause/resume; what native calls back: message to page, haptic, saved file).

## Phase 1: two agents in parallel

**Agent A: picture (C++, GLES 3).** Owns `android/native/render/`.
- Build step that extracts the GLSL from `index.html` (fail loudly if a shader is missing) and applies the HDR patch to the final shader.
- Port the pipeline to GLES 3 with the Mac's native changes: fixed targets per size, queued clear/restore/veil, GPU time through timer queries, snapshot as PNG bytes.
- Output path from `Caps`: standard range, or HDR surface with the headroom the display reports. With headroom 1 the picture must equal the web's.
- Flash guard and brightness ceiling exactly as on the Mac, including values above white.
- Gate: all nine chambers render on the phone through a debug intent that dumps snapshots; orchestrator compares them with the Mac's renders. GPU ms per chamber at native resolution reported.

**Agent B: app (Kotlin + JNI glue).** Owns `android/app/`, `android/native/host.cpp`, Gradle files.
- One activity, immersive, a `SurfaceView` for the picture under a transparent `WebView` serving the built page from assets on a fixed origin (so preferences and the URL hash persist), no network permission in the manifest.
- Bridge: a ten-line adapter so `shim.js` posts through `addJavascriptInterface` and receives `__fx.state` / `__fx.event` unchanged. Implement every message in the shim's protocol comment; `share` opens the system share sheet, stills go to MediaStore.
- Render thread paced by the display's vsync callback with the surface asking for the top refresh rate; engine stepped there; thermal headroom fed to the engine's thermal hint.
- Touch: all pointers forwarded to the engine (it already has the multi-touch twist and pinch path) unless the page reported `overUI`; the page still receives them for the interface.
- Sound: AAudio output running the synthesiser; hush on background; stream closed when sound is off.
- Haptics from engine events. Wake lock from the `awake` message.
- Debug intent extras (not user-facing): one line of stats per second to logcat (fps, GPU ms, size, detail level, dropped frames, memory, thermal), run a script in the page, paint a stroke, dump snapshots.
- Gate: app launches; orchestrator sees picture plus interface in `android screen capture`; 120 fps and flat memory over two minutes in logcat; background → foreground cycle leaves no leak and no black surface.

B's first milestone, before anything else: a solid colour drawn through `renderer.h`, built with `android/build.sh` and installed on the phone. That proves the build chain; A tests on the device from then on.

## Phase 2: one agent

**Agent C: films, then Listen.** Owns `android/native/record/`, `android/native/audio_in.cpp`.
- Film: MediaCodec hardware encoder fed through its input surface, a second EGL surface the final pass draws into at 60 fps with presentation times from the frame clock, AAC from the synthesiser tap, MediaMuxer writing an ordinary MP4 into MediaStore so the existing toasts stay true.
- Format from `Caps`: HEVC Main10 HLG when the encoder and EGL colour space exist, otherwise HEVC or H.264 standard range. Never software.
- Bounded by construction: a frame is drawn to the encoder only if the previous one was accepted; otherwise it is skipped and counted. The render thread never waits on the encoder.
- Listen: AAudio input opened only while Listen is on, the same three band levels as `mac/platform/audio.mm`.
- Gate: a 30-second film at native resolution recorded through the debug intent; pulled to the Mac and checked with ffprobe (codec, bit depth, colour tags, 60 fps, length, audio); memory flat in logcat while recording; dropped frames reported.

## Phase 3: agent B, resumed

**Hands and camera light.** Owns `android/app/` camera and tracker files. Starts after B's and A's gates pass; runs alongside C.
- Camera: Camera2 on the front camera with two outputs from one session: a `SurfaceTexture` the renderer samples as an external texture (camera light, no copies), and a small `ImageReader` for the tracker. Opened only while Camera light or Hands is on, fully closed otherwise. Late frames are dropped, never queued.
- Tracker: MediaPipe `tasks-vision` HandLandmarker in live-stream mode, up to two hands, on its own thread. Results go to `engine_hands` unchanged: the engine already mirrors x and reads MediaPipe's handedness the way the web does.
- Cadence from `engine_hands_interval`, as on the Mac, so the tracker runs only as often as the hand needs.
- Privacy, matching the web's reasoning:
  - Pin the newest release before 1.0 (the web pins 0.10.35 because 1.0 and later send usage statistics). The orchestrator confirms the exact Android artifact version in phase 0 and records it under Device facts.
  - The model file is fetched once at build time, checked against a pinned hash, and shipped inside the app. Nothing is downloaded on the phone.
  - The manifest still has no network permission, so the library cannot send anything whatever its version does. The orchestrator checks the merged manifest for permissions the library adds and strips them.
- Delegate: measure GPU against CPU on the phone and pick by tracker milliseconds and by what it costs the picture's frame rate; record the choice in `Caps`.
- Gate: with Hands on through the debug intent, logcat shows detections at the expected tiers, picture still at 120 fps, memory flat, camera closed within a second of Hands going off. Whether a real hand paints is an owner check.

## Deliberately not in this plan

- MIDI, tilt, tablets, other phones, Play Store packaging, release signing.

## Orchestrator's review at each gate

- Run the gate on the phone personally; read the stats line, look at the captures.
- Taste check against the Mac build: same colours, same motion, interface untouched, nothing added that nobody asked for. Cut anything that is not needed for the gate.
- Read the diff for hidden state: globals, capability checks outside `Caps`, work on the wrong thread, anything that depends on call order without saying so.
- Confirm `mac/build.sh` still passes.

## For the owner to check by hand

Touch painting and two-finger twist and pinch; orb, halo and every panel; sound by ear; HDR by eye; a film in the Gallery and in Instagram; share sheet; rotation; heat after ten minutes; microphone and camera permission prompts; Hands (raise a finger to paint, fist to rest, pinch to hold the glass, both palms to step on) and camera light.

## Risks, in order

1. **HDR is unproven until probed.** The Snapdragon 8 Gen 1 is expected to offer HDR EGL colour spaces and a hardware Main10 HLG encoder, but that comes from memory, not a test. Phase 0 settles it; the standard-range path is built first either way.
2. **Fill rate.** 3088 × 1440 float buffers at 120 Hz is heavy for the Adreno 730. The detail ladder starts low and climbs on measured GPU time; the gate is 120 fps held, not full resolution.
3. **Heat.** Sustained 120 Hz will throttle. The thermal hint caps detail; ten-minute behaviour is an owner check.
4. **Encoder surface blocking.** Some drivers block on swap when the encoder is behind. Phase 2 measures it; the skip rule above is the guard.
5. **Tracker against the picture.** Hand tracking on the GPU competes with rendering; on the processor it adds heat. The delegate is chosen by measurement, and Hands may cost a detail level.
6. **MediaPipe's build weight.** It adds native libraries and may pull in permissions or other libraries; the merged manifest and APK size are checked at the gate.
7. **WebView over SurfaceView.** Transparency and touch pass-through behave differently across WebView versions; verified at B's gate before anything depends on it.

## Device facts

Probed over adb on 2026-11-15 (read-only: getprop, dumpsys, codec list).

| Fact | Value |
|---|---|
| Phone | SM-S908E, serial R5CT31919SD, SoC SM8450 (Snapdragon 8 Gen 1), arm64-v8a |
| System | Android 16 (API 36) |
| GPU | Adreno 730, OpenGL ES 3.2 V@0615.98 |
| Display | The owner's setting is FHD+: **1080 × 2316**, not 3088 × 1440 (a settings change, so it stays). 120 / 96 / 60 / 48 / 30 / 24 / 10 Hz at every resolution |
| Display HDR | HDR10, HLG, HDR10+ (types 2, 3, 4); reports 450 nits max, 225 average. `hdrSdrRatio` is NaN: no SDR-relative headroom reporting |
| EGL | `bt2020_hlg`, `bt2020_pq`, `scrgb_linear`, `display_p3`, `pixel_format_float`, `ANDROID_recordable`, `ANDROID_presentation_time`: all present |
| GL extensions | From the app's `caps` line: `OES_EGL_image_external_essl3`, `EXT_color_buffer_half_float`, `EXT_color_buffer_float`, `EXT_disjoint_timer_query` all present; max texture 16384; a 10-10-10-2 window config exists |
| Encoders | `c2.qti.hevc.encoder` (hardware): Main, Main10, Main10HDR10, Main10HDR10Plus, to level 6, 160 Mbit/s. `c2.qti.avc.encoder` (hardware) |
| MediaPipe | `com.google.mediapipe:tasks-vision:0.10.35` exists on Google Maven and is the newest before 1.0.0 (same version the web pins) |
| Toolchain | NDK 30.0.16248370, AGP 9.5.1 is the newest stable, Gradle 9.7.1 on Homebrew OpenJDK 26.0.2. The shared engine and synthesiser compile with the NDK unchanged |

### Decisions

- **Native resolution** means 1080 × 2316 at 120 Hz. Fill rate is a third of what risk 2 assumed.
- **HDR on screen: yes, as BT.2020 HLG**, 10-bit (RGBA 10-10-10-2 window with `EGL_GL_COLORSPACE_BT2020_HLG_EXT`). Reasons: the display lists HLG; the phone reports no SDR-relative headroom, so the Mac's extended-range route (float scRGB plus a live headroom) has nothing to read; and the screen and the film then share one encoding in the final shader. Headroom is a constant, 4.9 (HLG peak 1000 nits over 203-nit reference white), kept in `Caps`. It is used only if the surface is actually created and SurfaceFlinger then shows the layer as BT2020 HLG; otherwise `Caps.screen` stays standard range. Standard range is built first.
- **Film: HEVC Main10 HLG** on `c2.qti.hevc.encoder`; fallback HEVC 8-bit, then H.264, both hardware.
- **Starting detail**: the engine's own ladder from its lowest level, climbing on measured GPU time.
- **One phone, two agents**: device use is serialised with a lock directory (`android/build/device.lock`); each agent builds into its own `android/build/<name>`.
- **Contracts**: `android/native/caps.h`, `renderer.h`, `host.h`.

### Phone safety (owner's instruction, 2026-11-15)

Nothing on the phone outside the package `app.fractonaut` and its private data may be changed or deleted, by anyone, for any reason. Enforced in `.claude/settings.local.json` (auto-mode rules and deny rules) and repeated in every agent's brief. The app's manifest carries CAMERA, RECORD_AUDIO, VIBRATE and WAKE_LOCK only. Uninstalling the app or clearing its data needs the owner's word.

### Gate log

- **B (app), 2026-11-15, re-run by the orchestrator with power saving off:** `screen=hlg`, display at 120 Hz, 117–120 fps over two minutes (three seconds dipped to 90–97), memory flat at about 220 MB (graphics 110 MB), no errors. Accepted on the numbers. Leaving and returning to the app is an owner check.
- **Thermal:** B mapped thermal headroom to the engine's states with its own thresholds, which capped a merely warm phone at level 5. Changed to the system's thermal status (`host_thermal(headroom, status)`); headroom is only printed.
- **Memory incident:** the phone was already short of memory (swap two-thirds full). Chamber test runs at 1080 × 2316 with echo scale 1 reached 500–636 MB (a resize held two full target sets) and the system closed the owner's background apps. Rules added: force-stop before releasing the device lock, short runs, chamber runs at the engine's own drawable size.
- **A (picture), accepted after one round.** With the Mac render test's exact inputs at 1120 × 704 the phone matches the Mac at 32.5–62.8 dB PSNR (fractal lowest: chaotic, identical by eye; the rest 39.8 and up). Re-run by the orchestrator: same numbers; all ten chambers render through the real engine.
- **Shared engine bug found and fixed** (`mac/core/engine.cpp`, `ease`): a float parameter eased to 0 kept a tiny positive residue, and the shaders test `genRadius > 0`, so generators vanished after a chamber change. The Mac app had it too. Engine tests and `mac/build.sh` pass.
- **GPU ms per chamber at the app's drawable size, 768 × 1647** (orchestrator's run): mandala 6.4, tunnel 2.0, fractal 6.9, circle-limit 5.8, spiral 4.9, liquid 6.1, crystal 17.7, maze 6.7, dissolve 6.9, dream 2.9. Crystal is over the 8.3 ms budget at that size; the ladder lowers resolution for it.
- **C (films), accepted; re-run by the orchestrator:** a 30-second film through `fx.record`: HEVC Main 10, yuv420p10le, 1080 × 2316, BT.2020 / HLG, 29.8 s, 1785 frames (3 skipped), screen at 120 fps after the first seconds, memory 231–248 MB, heap about 34 MB while recording. The Qualcomm encoder refuses 10-10-10-2 RGBA surface buffers, so the film pass converts to 10-bit YUV through `EGL_EXT_yuv_surface` / `GL_EXT_YUV_target`; `FILM_HEVC10_HLG` now requires those. Sound in films, Listen, gallery saves and stop-on-background were not run (owner checks, by the owner's decision).
- **B (hands and camera light), accepted on paper by the owner's decision:** built; refusal path verified on the phone; merged manifest carries the four permissions only (MediaPipe's telemetry components and network permissions stripped); model fetched at build time against a pinned hash; APK 28 MB. Camera permission is currently refused on the phone, so detection tiers, 120 fps with Hands on, the GPU-against-CPU choice (defaults to CPU) and camera close timing are untested.
- **Ladder:** it does climb back after the start-up dip (level 5 to 3 within about 20 s in the film run).
- **Icon:** neon sunburst (`icons/make_icons.mjs`), in the Android adaptive icon (with themed layer), the Mac `.icns` (built by `mac/build.sh` from `icons/mac-1024.png`) and the web favicon.
- **Shareable APK:** `android/dist/fractonaut.apk`, a copy of the debug build (debug-signed, arm64, Android 12 and later).
- **Resolution (owner: "throttling is pretty bad"), 2026-11-16.** Two causes, both on the Android side. (1) Thermal: Android's MODERATE was mapped to the engine's "serious", capping a warm phone at 85%; now none/light are nominal, moderate is fair, severe is serious. (2) The ladder reads GPU time as load, but this GPU slows its clock when work is light, so its time sits at 70–80% of the frame at any size and detail only ever stepped down (461 × 988 with 5 ms frames). The host now reports what happened instead (frames missed with the GPU as the cause, hold while the interface loads, or free to climb); the engine's ladder is unchanged. Measured: 768 × 1647 held at levels 1–2 for 26 s on a hot phone, where it used to sit at 461 × 988.
- **Hands at 60 Hz is the system's rule, not the app's.** When the package opens a camera, Android adds a display vote capping the render rate at 30–60 Hz (seen in the display log at camera open, over the app's own 120 Hz requests). The only override is a window attribute hidden from apps (`preferredMaxDisplayRefreshRate`: NoSuchFieldException). Also measured with Hands on: tracker 40–60 ms per frame on the CPU plus 30–40 ms converting each camera image in Kotlin, with the heap swinging to 100 MB; the conversion should move to native code.
- **Hands, 2026-11-16.** (1) Camera image conversion moved from Kotlin to C++ (`android/native/yuv.cpp`, tested on the Mac for colour, rotation and bounds): 30–40 ms a frame down to 2–4 ms on the phone. (2) Delegate measured: CPU 33–39 ms, GPU 44–53 ms with stalls, so CPU stays. With a hand in view the tracker takes 48–75 ms (10–15 results a second); tracking two hands keeps the palm detector running every frame, so one-hand tracking should be markedly faster (not measured). The memory swing with Hands on (heap 50–98 MB) is the tracker's own, not the conversion. (3) Hand-to-screen mapping on Android: instead of the middle 80% of the camera picture stretched over the view, a window with the view's own shape (62% of the picture's height, about 39% of its width on this phone, starting 11% from the top), so motion is the same scale in both directions and the hand stays in sight at the screen's edges. Done in the host by rescaling landmarks and telling the engine the view's aspect; the shared hand logic is unchanged. Not judged by eye: the owner's check.
