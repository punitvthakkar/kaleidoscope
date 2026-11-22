# fractonaut: where it stands, and what a native version could do

Written 2026-11-15, after commit `cf0b17f` (live at https://punitvthakkar.github.io/kaleidoscope/).

## Where the web version stands

One HTML file on raw WebGL2, no build step. The only outside dependency is the hand tracker (`@mediapipe/tasks-vision` 0.10.35, pinned and hash-checked) and its model.

**Picture**
- Nine chambers, rendered into 16-bit float buffers and tone-mapped to standard range on the way to the screen. The data is HDR inside; the output is not.
- Runs at the display's refresh rate (120 fps in all nine chambers on an M5 Max at 1280 × 715 in the test browser). A detail ladder trades supersampling, then resolution, to hold it.
- Not measured on a phone.

**Sound**
- Drone plus plucked notes, mastered with headroom instead of a compressor: sub-bass filtered, two echo loops, a soft ceiling at the end. Notes are panned by position.
- Starts off. Sound and Hands buttons sit beside the orb.

**Recording**
- Standard-range H.264 in MP4 through the browser's `MediaRecorder`.
- The fragmented MP4 that browsers write is rewritten as an ordinary MP4 before saving, so galleries and Instagram show the whole film.
- Memory guard: the film is read back as it is written; recording pauses if the encoder trails by two seconds and ends early if it cannot keep up. The canvas is capped near 2.6 million pixels while filming.
- Sound in a film fades in and out.

**Decided**
- No HDR in the web version, on screen or in files.
- No HEVC in the web version.

**Not yet verified on a real device**
- MP4 recording in Safari and Chrome with the new guard (the test browser only records WebM). The MP4 rewrite and the guard were tested on ffmpeg-made files and in simulation.
- Whether the saved film now opens at full length in the phone gallery and Instagram.

## What the web could not do, and why

These are the reasons a native version is worth considering. Items marked *(research)* come from reading browser source and vendor documents on 2026-11-15 and were not tested on a device.

| Wanted | What stopped it |
|---|---|
| HDR on screen | WebGL has no HDR output in any shipped browser. The only route is a WebGPU canvas, and handing each WebGL frame to WebGPU was slow on the Mac (over 3 ms a frame). Safari's WebGL canvas is 8-bit regardless *(research)*. |
| HDR in the film | `MediaRecorder` gives 8-bit standard range. Safari cannot build a 10-bit video frame; Chrome's 10-bit HEVC encode is behind a flag; Chrome on Android takes 8-bit input only *(research)*. |
| HEVC films | Safari on the Mac encoded HEVC in software at about 8 frames a second while the page supplied 60. The backlog reached 28 GB and the tab was killed. A page cannot require the hardware encoder. |
| Bounded memory while recording | Safari's recorder queues raw frames without limit *(research)*; the page can only watch from outside and pause. |
| Films at full screen resolution | Follows from the above: full-size frames (3024 × 1964) are what outran the encoder. |

Nothing from Fraunhofer HHI's recent work applies to real-time capture in a browser *(research)*. VVenC is an offline encoder, and its VVC output does not play in phone galleries.

## What a native version makes possible

A native app owns the three things a web page only borrows: the display surface, the encoder, and the memory.

**True HDR on screen**
- Render straight to an extended-range surface (on Apple: a Metal layer with extended dynamic range; on Android: an HDR surface). No bridge, no per-frame copy.
- The renderer already produces HDR values. The tone curve would let highlights run past white instead of rolling off at it, and wide-gamut colour comes with the same surface.
- The flash guard and brightness ceiling must be re-tuned for real highlight brightness. This is a safety requirement, not polish.

**HDR HEVC films that galleries recognise**
- Hardware HEVC Main10, required rather than requested, so there is no silent fall to software.
- HLG, not PQ: it is what phone cameras produce and what galleries expect *(research)*. Container tagging: `hvc1` sample entry, colour box with primaries 9, transfer 18, matrix 9 *(research)*.
- Frames go from the renderer to the encoder on the GPU. The encoder reports back-pressure directly, so frames are dropped at the source and memory stays flat by construction.
- Recording at full resolution and 60 fps becomes realistic; the M5 Max has two hardware encode engines *(research; unmeasured here)*.
- Whether Instagram and Google Photos show such a file as HDR needs testing.

**Efficiency**
- One pipeline end to end, with no browser compositor in between.
- Direct control of frame pacing on 120 Hz displays, and of thermal behaviour on phones.
- Hand tracking can use the platform's own vision framework instead of a downloaded model.

**Things the web cannot offer at all**
- Haptics tied to the picture, on iPhone as well as Android.
- Spatial audio with head tracking.
- Saving straight to the photo library.
- Running as a screen saver, a visualiser for music playing elsewhere on the device, or on an external display.

## Routes, cheapest first

1. **Small macOS recorder beside the web app.** Capture the browser window in HDR with ScreenCaptureKit and encode with VideoToolbox. Only useful once the page itself shows HDR, which the web version does not, so this route is parked.
2. **Native shell around the web renderer** (WKWebView, Capacitor, Tauri). Does not help: the embedded engine has the same limits.
3. **Native renderer on Apple platforms first.** Port the shaders to Metal, present to an extended-range layer, record with hardware HEVC Main10 in HLG. One codebase covers Mac, iPhone and iPad. This is the route that delivers everything in the table above.
4. **Android after that.** A second port (Vulkan or GLES, MediaCodec HLG10). HDR encode support varies by device and needs a capability check.
5. **Offline master, either platform.** Log the session's inputs, re-render frame by frame afterwards, and encode with no real-time pressure. Gives the best possible file and suits long or very high-resolution films.

## What carries over, and what does not

- **Carries over as is:** the design (orb, halo, chambers, calm levels), the fold mathematics, the palettes, the sound design and its mastering levels, the MP4 writing code as a reference for container layout.
- **Needs porting:** about 620 lines of GLSL across ten shaders, and about 470 lines of renderer logic. Two WebGL habits have no direct equivalent and need a small redesign: automatic mipmap generation on the echo buffers, and the per-copy instancing used for ink.
- **Needs rebuilding:** input handling, the interface layer, hand tracking, and audio (the Web Audio graph is small enough to rebuild node for node).

## Open questions before starting

- Which platform first: Mac, or iPhone?
- Is the native app a companion to the web version or its replacement?
- Is HDR on screen worth it without HDR films, or are the two one feature?
- How should the photosensitivity limits change when highlights can be several times brighter than white?

## Design notes moved out of the code

### Sky places (mac/render/shaders/stars.metal)

STARS: the sky places. Each is a kaleidoscope of light: its points live inside one
wedge of the chamber (between angle 0 and pi/sym, in the sky's own space: the
chamber's spin and the sky's zoom taken out) and the mirrors show them 2 * sym times.
A point that reaches a mirror bounces off it, which on screen is the same point
passing on into the next reflection. Each point is a texel holding its position (xy)
and velocity (zw).

stars_step_fs moves every point one step. A kaleidoscope only has to draw what is in
one wedge: stars_vs / stars_fs draw each point once, into two textures that cover the
wedge in the sky's own space, and the scene pass (scene.metal, skyLight) turns and
folds every pixel of the screen into the wedge and reads them, so every reflection is
exact and costs nothing more. One texture holds sharp points at the screen's own pixel
density, drawn afresh each frame; the other, coarser, holds their soft light, which
stars_trail_fs carries from frame to frame, fading, so moving points leave trails.

What a hand does: every finger (and a hovering cursor, more lightly) is folded into
the wedge, so it acts at every one of its reflections at once. A finger lights up what
it is near, carries along what it passes through, and pulls; in each sky it also does
one thing of its own. A tap is a burst, thrown outward from every reflection.

sKind says which sky this is:
  0 galaxy     a spiral galaxy turning at the middle; every finger is a sun that
               gathers its stars into rings
  1 nebula     glowing gas on a slow flow that blooms in petals; a finger is a
               whirlpool that winds it into spirals
  2 pulsar     a spinning star whose beams wind into a rosette of spirals; a finger
               aims and spins them, and bends them where it touches
  3 quasar     a disc spiralling into a black hole that throws what it swallows out
               as jets; the jets follow a finger, and a finger tears streams off the disc
  4 supernova  matter gathers on a finger held down and bursts outward when it lets go
  5 aurora     a crown of curtains seen from beneath, rays streaming outward from the
               pole; the curtains reach for a finger
