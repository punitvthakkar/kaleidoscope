# Kaleidoscope

A calm, GPU-driven kaleidoscope for the browser. One HTML file on raw WebGL2 (plus chamber thumbnails in `thumbs/`), with no build step and no dependencies.

**Live:** https://punitvthakkar.github.io/kaleidoscope/

A kaleidoscope is mirrors, light and time, so the controls are three questions:

- **Chamber** shapes the mirrors: mandala, tunnel, fractal folds, Escher-style hyperbolic tiling, a Droste spiral, liquid, a 3D lattice, a mirrored maze, a dream current
- **Light** is what shines into them: your hand, wanderers that paint on their own, patterns, your camera, your fingertips
- **Mood** is how it moves and fades: color, pace, echo, melt, glow, and sound (the glass can sing as you paint, or listen to music in the room and move with it)

**Wander** travels between chambers on its own. Every underlying setting is under ⋯ → All settings, and the URL always reproduces the current view.

## Run locally

```sh
python3 -m http.server 3325
# open http://localhost:3325
```

The camera, microphone and fingertip tracking need `localhost` or HTTPS.

## Gestures

Tap or click to drop a ripple · scroll or pinch to dive in and out · twist two fingers (or Shift + scroll) to spin the glass. Dives and spins coast and settle on their own.

## Keys

Space turn the glass · 1–9 / ← → chambers · ↑ ↓ mirrors · W wander · S sound · M listen · E all settings · I save image · R record · H hide controls · ? all keys

## Notes

- Brightness changes are eased by a flash guard (All settings → Journey & performance → Soften flashes) to reduce photosensitivity risk. While it is on, average brightness is also held under a ceiling (Brightness ceiling, 30% by default).
- Sound and music only ever move the picture (zoom, spin, color drift); they never drive brightness, so the flash guard still holds.
- The picture renders at the display's refresh rate. A detail ladder (All settings → Journey & performance → Keep motion smooth) trades supersampling first and resolution second to hold it.
- The interface uses Material Next, an independent design language built on Material 3. It is not an official Google design.
