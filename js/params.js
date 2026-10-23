// Parameter schema, palettes and chambers. Everything visual lives in `params`;
// the UI, chambers, URL hash, MIDI and morphing all work off this one schema.

const MODES = ['none', 'kaleido', 'kifs', 'hyperbolic', 'droste', 'tiles'];
const GENERATORS = ['none', 'rings', 'petals', 'flow', 'cells', 'orbs', 'fractal3d'];
const BRUSHES = ['neon', 'dots', 'sparks'];
const INK_COLORS = ['cycle', 'speed', 'angle', 'solid'];
const PEN_PATHS = ['lissajous', 'rose', 'noise', 'spiral'];

// Plain-language names for option values (internal values stay stable for URLs).
const OPTION_NAMES = {
  none: 'None', kaleido: 'Mandala mirrors', kifs: 'Fractal folds', hyperbolic: 'Circle limit', droste: 'Spiral', tiles: 'Maze',
  rings: 'Rings', petals: 'Petals', flow: 'Flow lines', cells: 'Cells', orbs: 'Orbs', fractal3d: 'Lattice',
  neon: 'Line', dots: 'Dots', sparks: 'Sparks',
  cycle: 'Shifting', speed: 'By speed', angle: 'By angle', solid: 'Steady',
  lissajous: 'Weave', rose: 'Petal', noise: 'Drift', spiral: 'Spiral',
};

// Inigo Quilez cosine palettes: color(t) = a + b * cos(2π(c t + d))
const PALETTES = {
  rainbow: [[.5, .5, .5], [.5, .5, .5], [1, 1, 1], [0, .33, .67]],
  neon:    [[.5, .5, .5], [.5, .5, .5], [1, 1, 1], [0, .1, .2]],
  vapor:   [[.8, .5, .4], [.2, .4, .2], [2, 1, 1], [0, .25, .25]],
  sunset:  [[.5, .5, .5], [.5, .5, .5], [1, .7, .4], [0, .15, .2]],
  ember:   [[.5, .3, .2], [.5, .3, .2], [1, 1, 1], [0, .1, .2]],
  acid:    [[.5, .5, .5], [.5, .5, .5], [1, 1, .5], [.8, .9, .3]],
  toxic:   [[.5, .5, .5], [.5, .5, .5], [1, 1, 1], [.3, .2, .2]],
  ocean:   [[.5, .5, .5], [.5, .5, .5], [2, 1, 0], [.5, .2, .25]],
  ice:     [[.4, .6, .8], [.4, .4, .3], [1, 1, 1], [0, .1, .15]],
  magenta: [[.5, .5, .5], [.5, .5, .5], [1, 1, 1], [.6, .1, .9]],
  mono:    [[.5, .5, .5], [.5, .5, .5], [1, 1, 1], [0, 0, 0]],
};

// key: [default, min | options, max, step, group, label, turn]
// `turn` marks what "Turn the glass" may vary (true = full range, [lo, hi] =
// a calmer range). Defaults are deliberately slow; Pace scales all motion.
const SCHEMA = {
  // Chamber: the shape of the mirrors
  mode:          ['kaleido', MODES, null, null, 'Chamber', 'Mirror shape', false],
  symmetry:      [6, 2, 24, 1, 'Chamber', 'Mirrors', [4, 12]],
  mirror:        [true, null, null, null, 'Chamber', 'Reflect, not only rotate', false],
  foldInLoop:    [true, null, null, null, 'Chamber', 'Fold the echoes too', false],
  spinSpeed:     [2, -45, 45, 0.1, 'Chamber', 'Mirror turning °/s', [-4, 4]],
  viewZoom:      [1, 0.2, 4, 0.01, 'Chamber', 'Lens zoom', [0.7, 1.5]],
  fisheye:       [0, -1, 1, 0.01, 'Chamber', 'Lens bulge', [-0.2, 0.4]],
  warpAmount:    [0, 0, 1, 0.01, 'Chamber', 'Melt', [0, 0.35]],
  warpScale:     [1.5, 0.2, 6, 0.01, 'Chamber', 'Melt size', [0.6, 2.5]],
  warpSpeed:     [0.08, 0, 1, 0.01, 'Chamber', 'Melt speed', [0, 0.15]],
  kifsIters:     [5, 1, 10, 1, 'Fractal', 'Fold depth', [4, 8]],
  kifsScale:     [1.5, 1.05, 2.5, 0.01, 'Fractal', 'Fold scale', [1.25, 1.8]],
  kifsAngle:     [20, 0, 180, 0.1, 'Fractal', 'Twist', true],
  kifsAngleSpeed:[1.5, -20, 20, 0.1, 'Fractal', 'Twist speed °/s', [-3, 3]],
  kifsOffX:      [0.6, -1, 1.5, 0.01, 'Fractal', 'Offset across', [0.2, 1]],
  kifsOffY:      [0.3, -1, 1.5, 0.01, 'Fractal', 'Offset up', [0, 0.8]],
  hypQ:          [4, 3, 12, 1, 'Circle limit', 'Tiles meeting at a corner', [3, 6]],
  hypDrift:      [0.15, 0, 1, 0.01, 'Circle limit', 'Drift', [0, 0.35]],
  drosteScale:   [3, 1.5, 10, 0.01, 'Spiral', 'Repeat ratio', [2, 5]],
  drosteSpeed:   [0.12, -1.5, 1.5, 0.01, 'Spiral', 'Zoom speed', [-0.25, 0.25]],
  tileScale:     [2, 0.5, 8, 0.01, 'Maze', 'Tiles', [1.5, 3.5]],

  // Echo: the feedback loop that remembers light
  fbDecay:       [0.94, 0, 1, 0.001, 'Echo', 'Echo length', [0.85, 0.98]],
  fbZoom:        [1.004, 0.95, 1.05, 0.0005, 'Echo', 'Flow inward ↔ outward', [0.994, 1.012]],
  fbRotate:      [0, -90, 90, 0.1, 'Echo', 'Flow rotation °/s', [-10, 10]],
  fbSwirl:       [0, -0.05, 0.05, 0.001, 'Echo', 'Swirl', [-0.008, 0.008]],
  fbWarp:        [0, 0, 0.02, 0.0001, 'Echo', 'Shimmer', [0, 0.004]],
  fbHueShift:    [6, -180, 180, 1, 'Echo', 'Color drift °/s', [-20, 20]],
  fbSat:         [1, 0, 2, 0.01, 'Echo', 'Saturation', [0.9, 1.2]],
  fbSharpen:     [0, 0, 1, 0.01, 'Echo', 'Sharpen', false],

  // Light: what you paint with, and the wanderers that paint on their own
  brush:         ['neon', BRUSHES, null, null, 'Light', 'Brush', false],
  inkHover:      [true, null, null, null, 'Light', 'Paint without clicking', false],
  inkSize:       [1, 0.2, 5, 0.01, 'Light', 'Brush size', [0.8, 1.6]],
  inkGlow:       [1.3, 0, 3, 0.01, 'Light', 'Brush glow', [0.8, 1.8]],
  inkBright:     [2, 0.1, 5, 0.01, 'Light', 'Brush brightness', false],
  inkSpeed:      [1, 0, 3, 0.01, 'Light', 'Speed thickens strokes', false],
  inkColor:      ['cycle', INK_COLORS, null, null, 'Light', 'Brush color', true],
  autoPens:      [0, 0, 8, 1, 'Light', 'Wanderers', [1, 4]],
  autoPath:      ['lissajous', PEN_PATHS, null, null, 'Light', 'Wanderer path', true],
  autoSpeed:     [0.35, 0.05, 3, 0.01, 'Light', 'Wanderer speed', [0.2, 0.55]],

  gen:           ['none', GENERATORS, null, null, 'Patterns', 'Pattern', false],
  genMix:        [0.3, 0, 2, 0.01, 'Patterns', 'Strength', [0.2, 0.5]],
  genScale:      [1, 0.2, 5, 0.01, 'Patterns', 'Size', [0.6, 2]],
  genSpeed:      [0.35, 0, 3, 0.01, 'Patterns', 'Speed', [0.2, 0.5]],
  genRadius:     [0, 0, 1.5, 0.01, 'Patterns', 'Source size (0 = everywhere)', false],
  rmScale:       [2.6, 1.5, 3.5, 0.01, 'Patterns', 'Lattice fold', [2.2, 3]],
  rmFly:         [0.25, -2, 2, 0.01, 'Patterns', 'Lattice flight speed', [-0.3, 0.4]],

  camMix:        [0.8, 0, 2, 0.01, 'Camera', 'Strength', false],
  camZoom:       [1, 0.2, 4, 0.01, 'Camera', 'Zoom', false],
  camColorize:   [0.5, 0, 1, 0.01, 'Camera', 'Recolor', false],

  // Mood: color, time and finish
  pace:          [1, 0.1, 2.5, 0.01, 'Color & time', 'Pace', false],
  palette:       ['rainbow', Object.keys(PALETTES), null, null, 'Color & time', 'Palette', true],
  paletteSpeed:  [0.015, -0.3, 0.3, 0.001, 'Color & time', 'Palette drift', [-0.03, 0.03]],
  paletteMap:    [0, 0, 1, 0.01, 'Color & time', 'Recolor everything', [0, 0.6]],

  bloom:         [0.8, 0, 3, 0.01, 'Finish', 'Glow', [0.6, 1.3]],
  bloomThreshold:[0.8, 0, 2, 0.01, 'Finish', 'Glow threshold', false],
  chroma:        [0.2, 0, 3, 0.01, 'Finish', 'Prism edges', [0, 0.6]],
  grain:         [0.015, 0, 0.3, 0.001, 'Finish', 'Grain', false],
  vignette:      [0.4, 0, 1.5, 0.01, 'Finish', 'Vignette', false],
  exposure:      [1, 0.2, 3, 0.01, 'Finish', 'Brightness', false],
};

// Params a chamber change leaves alone: they belong to the viewer, not the place.
const STICKY = ['pace', 'inkHover'];

// System settings: not part of chambers, the URL or Turn.
const SYSTEM_DEFAULTS = {
  renderScale: 1,      // multiplier on device pixels (before auto quality)
  maxDpr: 2,
  autoQuality: true,
  flashGuard: true,    // damps full-screen brightness pulses (photosensitivity)
  morphTime: 6,
  autoVJ: false,
  autoVJInterval: 40,
};

const DEFAULTS = Object.fromEntries(Object.entries(SCHEMA).map(([k, s]) => [k, s[0]]));
const params = { ...DEFAULTS };
const system = { ...SYSTEM_DEFAULTS };

// Chambers: the places the explorer visits. `line` says what the mirrors do.
const CHAMBERS = [
  { name: 'Mandala', slug: 'mandala', line: 'Mirrors meeting at a single point',
    p: { symmetry: 8, fbDecay: 0.97, fbZoom: 1.004, fbRotate: 3, autoPens: 3, autoPath: 'rose', bloom: 1, inkSize: 1.2, inkGlow: 1.5, fbHueShift: 8, spinSpeed: 1.5 } },
  { name: 'Tunnel', slug: 'tunnel', line: 'Light streaming from a vanishing point',
    p: { symmetry: 8, foldInLoop: false, fbZoom: 1.012, fbRotate: 6, fbDecay: 0.975, gen: 'petals', genRadius: 0.3, genMix: 0.5, fbHueShift: 12, autoPens: 2 } },
  { name: 'Fractal', slug: 'fractal', line: 'Mirrors folded inside mirrors',
    p: { mode: 'kifs', kifsIters: 7, kifsScale: 1.4, kifsOffX: 0.8, kifsOffY: 0.2, kifsAngleSpeed: 1, fbZoom: 1.004, fbDecay: 0.88, gen: 'cells', genMix: 0.35, palette: 'magenta', chroma: 0.4, autoPens: 2 } },
  { name: 'Circle limit', slug: 'circle-limit', line: 'Tiles shrinking forever toward the edge',
    p: { mode: 'hyperbolic', symmetry: 7, hypQ: 3, foldInLoop: false, gen: 'petals', genMix: 0.55, genSpeed: 0.18, fbDecay: 0.92, hypDrift: 0.25, palette: 'vapor', exposure: 1.2 } },
  { name: 'Spiral', slug: 'spiral', line: 'A picture that contains itself',
    p: { mode: 'droste', drosteScale: 3.2, foldInLoop: false, gen: 'flow', genMix: 0.35, fbDecay: 0.9, palette: 'sunset', autoPens: 2 } },
  { name: 'Liquid', slug: 'liquid', line: 'Mirrors that melt as you watch',
    p: { symmetry: 12, warpAmount: 0.4, fbWarp: 0.004, fbDecay: 0.97, fbZoom: 1.005, palette: 'ocean', paletteMap: 0.45, gen: 'flow', genMix: 0.3, autoPens: 3 } },
  { name: 'Crystal', slug: 'crystal', line: 'Drifting through a folded lattice',
    p: { symmetry: 6, gen: 'fractal3d', genMix: 0.7, fbDecay: 0.6, bloom: 1, chroma: 0.5, fbHueShift: 8 } },
  { name: 'Maze', slug: 'maze', line: 'A wallpaper of mirrored tiles',
    p: { mode: 'tiles', tileScale: 2.5, gen: 'cells', genMix: 0.4, chroma: 0.6, fbDecay: 0.9, fbRotate: 3, palette: 'toxic', autoPens: 2 } },
  { name: 'Dissolve', slug: 'dissolve', line: 'Curved space folding back on itself',
    p: { mode: 'hyperbolic', symmetry: 5, hypQ: 4, fbZoom: 1, fbDecay: 0.9, chroma: 0.8, fisheye: 0.25, gen: 'flow', genMix: 0.5, genSpeed: 0.2, hypDrift: 0, fbHueShift: 10, autoPens: 0, exposure: 1.3, bloom: 1.1 } },
];

// Live palette: eases toward the selected palette so switches never pop.
const Palette = {
  a: new Float32Array(PALETTES.rainbow[0]), b: new Float32Array(PALETTES.rainbow[1]),
  c: new Float32Array(PALETTES.rainbow[2]), d: new Float32Array(PALETTES.rainbow[3]),
  update(dt) {
    const t = PALETTES[params.palette] || PALETTES.rainbow, k = Math.min(1, dt * 0.8);
    [this.a, this.b, this.c, this.d].forEach((v, i) => {
      for (let j = 0; j < 3; j++) v[j] += (t[i][j] - v[j]) * k;
    });
  },
  // Writes rgb * gain into arr at offset (no allocation).
  write(t, arr, o, gain) {
    for (let j = 0; j < 3; j++) {
      arr[o + j] = (this.a[j] + this.b[j] * Math.cos(6.283185307 * (this.c[j] * t + this.d[j]))) * gain;
    }
  },
  // CSS gradient preview of a palette (computed colors, not theme colors).
  css(name) {
    const [a, b, c, d] = PALETTES[name];
    const stops = [0, 0.25, 0.5, 0.75, 1].map((t) => {
      const ch = [0, 1, 2].map((j) => Math.round(255 * Math.min(1, Math.max(0, a[j] + b[j] * Math.cos(6.283185307 * (c[j] * t + d[j]))))));
      return `rgb(${ch.join(' ')})`;
    });
    return `conic-gradient(from 90deg, ${stops.join(', ')})`;
  },
};
