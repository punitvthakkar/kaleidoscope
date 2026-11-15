# fractonaut 2026

*Out on an infinite adventure.*

A calm, GPU-driven kaleidoscope for the browser. It opens in its stillest mood with sound and hand tracking off. While the orb is awake, a Sound button sits to its right and a Hands button to its left, so either is one tap away; they settle out of sight with the orb. Sound fades out when you leave, and comes back with your first touch if you had it on last time. Calm and Sound are also on the first ring of the orb. One HTML file on raw WebGL2 (plus chamber thumbnails in `thumbs/`), with no build step and no dependencies.

**Live:** https://punitvthakkar.github.io/kaleidoscope/

A kaleidoscope is mirrors, light and time, so the controls are three questions:

- **Chamber** shapes the mirrors: mandala, tunnel, fractal folds, Escher-style hyperbolic tiling, a Droste spiral, liquid, a 3D lattice, a mirrored maze, a dream current
- **Light** is what shines into them: your hand, wanderers that paint on their own, patterns, your camera, your fingertips
- **Mood** is how it moves and fades: color, pace, echo, melt, glow, and sound (the glass can sing as you paint, or listen to music in the room and move with it)

The interface is one orb, and the whole thing is meant as a quiet place to settle rather than a tool to operate. At rest the orb is a small iris in the colours of the current palette, and nothing else is on screen. Tap it and a halo opens around the centre of the picture:

- **Place, Colour, Calm, Light** shape where you are. Their discs show the thing itself: the place you are in, the colours you are sitting in, how gently it is moving.
- **Breathe** turns the orb into a breathing guide: it swells for four seconds and falls for six, and the glass (and the drone, if sound is on) move with it.
- **Sound, Listen, Drift** are ways of being there: the glass sings as you paint, moves to music in the room, or wanders on its own while you watch.
- **Keep** saves a picture, a film or a link.

**Calm** is five feelings rather than four sliders: Still, Soft, Flowing, Bright, Wild. The one you choose stays with you from place to place. The individual dials are one step further in, under Fine tune.

Inside any view the orb becomes the way back: an arrow steps out, a cross closes. The halo never takes the picture away. You can reach past it and paint, and it thins out while you do, so you can feel a place before you settle on it. Swipe or scroll the orb to change place without opening anything.

**Wander** travels between chambers on its own. Every underlying setting is under ⋯ → All settings, and the URL always reproduces the current view.

## Run locally

```sh
python3 -m http.server 3325
# open http://localhost:3325
```

The camera, microphone and fingertip tracking need `localhost` or HTTPS.

## Gestures

Tap or click to drop a ripple · scroll or pinch to dive in and out · twist two fingers (or Shift + scroll) to spin the glass. Dives and spins coast and settle on their own.

With **Fingertips** on (Light panel, or T), the camera reads your hands and nothing needs touching:

- **Raise a finger to paint.** Point for one light, hold up two or three, or open your hand for five. Each finger keeps its own color.
- **Curl your fingers to rest.** A fist lifts every pen, so you can move without drawing.
- **Pinch to hold the glass.** With one hand pinched, turn your wrist like a dial to spin it and move toward or away from the camera to dive. With both hands pinched, turn and stretch as if holding a wheel, the same twist and pinch as two fingers on a touch screen. Let go and it coasts.
While one hand is pinching, its other three fingers still paint if you raise them, so a single hand can turn the glass and draw at once; curl them to turn without drawing.

Both hands are tracked by default, so you can hold the glass with one and paint with the other, turn it with both like a wheel, or show both palms and hold still to step to the next chamber. **Both hands** appears beside Hands under Light while Hands is on; switching it off tracks one hand, which is lighter on the laptop.

The tracker runs only as hard as the hand needs: 30 times a second while it moves or holds the glass, 15 while it is slow or still, a few glances a second once it leaves, and once a second after ten seconds with nobody there. If the laptop still runs warm, **Track hands on the processor** and **Half frame rate while tracking hands** are there to try.

## Recording

Record (Keep → Film, or R) captures the picture and, if Sound or Listen is on when you start, what you hear. The bitrate follows the size of the picture rather than a fixed 40 Mbps. Films are H.264 in MP4, which opens everywhere and which every device encodes quickly; HEVC is not used, because Safari on a Mac encodes it in software at a few frames a second and runs out of memory.

Browsers write MP4 in fragments, which phone galleries and editors cut off after the first couple of seconds. Before a film is saved it is rewritten as an ordinary MP4 (the picture and sound are not re-encoded), so it opens at full length everywhere. On a phone, the saved toast offers the share sheet, which is the way into Photos. The sound in a film fades in and out instead of starting and stopping mid-note. A film is capped near 2.6 million pixels (about 2000 × 1300 on a large screen), because full-size frames on a big display can outrun a browser's encoder; and the film is read back as it is written to see how far the encoder has got. If the encoder trails the clock by two seconds the recording pauses until it has caught up (the film skips instead of memory filling), and if it cannot keep up at all, or goes silent for several seconds, the film ends early and what exists is saved.

## Sound

The drone and the notes are set with room to spare rather than held down by a compressor: rumble below the lowest note is filtered off, the root of the drone sits under its fifth and octave, notes start rounded and ring on through two damped echo loops, and a soft ceiling at the very end keeps a pile-up of notes from ever reaching full scale.

## Privacy and security

Nothing leaves the device: camera and microphone are processed in the page. A content security policy lets the page talk only to itself and to the two hosts the hand tracker loads from (jsDelivr for the library, Google's model storage for the model). The tracking library is pinned to `@mediapipe/tasks-vision` 0.10.35 and checked against its hash before it runs; 1.0 and later send usage statistics to Google, which is why the pin is deliberate. `MEDIAPIPE_CHANGELOG.md` has the release notes that decision was based on.

## Keys

Space turn the glass · 1–9 / ← → chambers · ↑ ↓ mirrors · W wander · O open the halo · G breathe · S sound · M listen · E all settings · I save image · R record · H hide controls · ? all keys

## Notes

- Brightness changes are eased by a flash guard (All settings → Journey & performance → Soften flashes) to reduce photosensitivity risk. While it is on, average brightness is also held under a ceiling (Brightness ceiling, 30% by default).
- Sound and music only ever move the picture (zoom, spin, color drift); they never drive brightness, so the flash guard still holds.
- The picture renders at the display's refresh rate. A detail ladder (All settings → Journey & performance → Keep motion smooth) trades supersampling first and resolution second to hold it.
- The interface uses Material Next, an independent design language built on Material 3. It is not an official Google design.
