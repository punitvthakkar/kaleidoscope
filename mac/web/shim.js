/* fractonaut native bridge: the interface's view of the engine.

The page is the web interface (index.html) with its engine blocks (shaders,
Renderer, Pens, Inputs, Sound, Mp4, Main) replaced by this file. The JS stays the
authority on `params`, `system` and all interface behaviour; everything per frame
(picture, pens and pointer painting over the picture, motion, sound, hand tracking,
camera, recording, saving files) is native.

================================ PROTOCOL (v1) ================================

TRANSPORT
  JS -> native: window.webkit.messageHandlers.fx.postMessage(batch)
    `batch` is always an ARRAY of message objects, each with a string `type`.
    Messages are queued and posted once per JS task (in practice once per
    animation frame). Within a batch, messages are in the order they happened,
    except that a `set` (if any) always comes first. Apply a batch in order,
    between two native frames.
  native -> JS:
    window.__fx.state(obj)          a few times a second (4-10 Hz is plenty);
                                    any subset of the STATE fields below.
    window.__fx.event(name, payload) one-off events, listed below.
  Both are defined by the time the page's `load` event fires.

JS -> NATIVE MESSAGES
  set        { params?: {key: value}, system?: {key: value} }
             Only what changed since the last `set`. The first `set` (in the
             first batch) is complete: every key of params (SCHEMA) and system
             (SYSTEM_DEFAULTS). Numbers and booleans as they are; enum params
             (mode, gen, brush, inkColor, autoPath, palette) as their option
             string. Numbers arrive at up to 60 Hz while the interface morphs
             between chambers; native eases numeric changes itself. Stepped
             params (symmetry, hypQ, autoPens) are integers and should snap.
  ready      {}  The interface is built and the first `set` has been sent.
             Sent once, in the first batch, after the `set`.
  veil       { seconds: number }  Start the optical dissolve (web
             Renderer.startVeil): hold the picture currently on screen and fade
             it out over `seconds` while the new structure appears beneath.
  clear      {}  Clear the glass: echo buffers, pen strokes and particles
             (web Renderer.clear + Pens.clear). Keep what was there for `restore`.
  restore    {}  Undo the last `clear`.
  breathe    { on: bool }  Breathing guide toggled; restart the 10 s lung clock
             (4 s in, 6 s out) from 0 at this moment. system.breathGuide comes
             in the same batch's `set`. JS runs its own copy of the clock only
             for the "Breathe in/out" words.
  sound      { on: bool }  Synth on/off. When the synth actually starts making
             sound after `on: true`, send event `soundStarted` (natively that
             can be at once; no user gesture is needed).
  root       { chamber: int }  Chamber index 0..9; the synth retunes to it.
  mic        { on: bool, id?: int }  Listen to the room. `on: true` carries an
             `id` and needs a `reply` (after the microphone permission prompt).
             `on: false` has no id and needs no reply.
  camera     { on: bool, id?: int }  Camera picture into the mirrors. Same
             reply rule as `mic`.
  hands      { on: bool, id?: int }  Hand tracking. Same reply rule as `mic`;
             reply ok once the camera is open, then send `handsReady` when the
             tracker is running. If tracking later fails, send `handsFailed`.
  handsOptions { twoHands: bool, cpu: bool }  Track one or two hands; `cpu`
             asks the tracker to keep off the GPU. Sent once at start and on
             every change; applies whether or not tracking is on.
  cameraDevice { device: string }  Which camera to use, by the id from the
             `cameras` event; '' means automatic (the built-in one, facing the
             user). Sent once at start and on every change; applies whether or
             not the camera is running, and changes over at once if it is. An
             id that is not connected means automatic until it is.
  snapshot   { file: string }  Save the next displayed frame (picture only, no
             interface) as PNG to ~/Downloads/<file>. No reply; the interface
             already says "Image saved to downloads".
  export     { width: int, height: int, file: string, id: int }  Render a
             still of the current view at width x height device pixels (never
             more than state.maxSize on either side; aspect already matches
             the window) and save it as PNG to ~/Downloads/<file>. Reply
             ok:true once written, ok:false if it could not be rendered.
  record     { on: true, file: string }  Start a film (with sound if the synth
             or mic is on). Send `recordStarted` when frames are being written,
             or `recordFailed` if it cannot start.
  record     { on: false }  Stop: fade the sound out, finish the file, save it
             to ~/Downloads, then send `recordSaved`. A film that ends on its
             own (encoder or disk trouble) also ends with `recordSaved` (if
             anything was written) or `recordFailed`.
  share      { file: string }  Show the system share sheet for ~/Downloads/<file>
             (the name from `recordSaved`), anchored near the window centre.
  copy       { text: string }  Put plain text on the general pasteboard.
  fullscreen { on: bool }  Enter or leave native window full screen. Always
             answer with event `fullscreen` once the window has changed.
  awake      { on: bool }  Keep the display awake (IOPMAssertion
             PreventUserIdleDisplaySleep) while on.
  overUI     { on: bool }  The pointer is over interface (true) or over the
             bare picture (false). Sent when it changes; starts false. While
             true, native must not paint, dive on scroll, or take trackpad
             gestures; the web view handles the event. While false, native
             handles painting, scroll-to-dive and gestures over the picture.
             (Clicks on the picture still reach the page too; it uses them for
             the tap-to-close and peek behaviours, so forward them as usual.)

REPLY (native -> JS, for messages that carry `id`)
  __fx.event('reply', { id: int, ok: bool, error?: 'denied' | 'unavailable' | 'failed' })
  Every `id` must be answered exactly once.

EVENTS (native -> JS)
  reply          see above
  soundStarted   {}  The synth is audible after a `sound {on:true}`.
  handsReady     {}  Tracking is running after `hands {on:true}`.
  handsFailed    {}  Tracking stopped on its own (camera lost, model error).
                 Native has already turned tracking off.
  cameraFailed   {}  The camera picture stopped on its own (camera lost).
                 Native has already turned camera light off.
  cameras        { list: [{ id: string, label: string }] }  Every camera that
                 can be used now. Send after `ready` and whenever one is
                 connected or removed.
  recordStarted  { sound: bool }  Film is being written; `sound` says whether
                 it has an audio track.
  recordSaved    { file: string }  Film finished and saved as ~/Downloads/<file>.
  recordFailed   {}  Film could not start, or ended with nothing saved.
  fullscreen     { on: bool }  The window entered or left full screen, by any
                 means (the green button, a menu, or a `fullscreen` message).
  nextChamber    {}  Both palms held still (the hands-free gesture).
  grip           { hands: [{ on: bool, x: number, y: number }, { ... }] }
                 Slot 0 = left hand, 1 = right. `on` while that hand pinches to
                 hold the glass; x, y its grip point as a fraction 0..1 of the
                 window (origin top left, as seen on screen). Send on change,
                 at tracking rate while a grip moves.

STATE (native -> JS, any subset; fields keep their last value)
  fps        number  frames per second actually drawn, over the last second
  quality    number  0..1, resolution scale the adaptive quality settled on
  width      int     render target width in pixels (what the picture is drawn at)
  height     int     render target height in pixels
  refreshHz  number  display refresh rate
  maxSize    int     largest texture side the GPU allows (export ceiling)
  painted    number  length of pointer strokes painted so far
  taps       int     taps so far
  particles  int     live spark particles

PAGE AND STORAGE
  Serve the page from a custom URL scheme with a fixed origin (for example
  fractonaut://app/index.html) and the default persistent website data store,
  so localStorage (`mn:` keys) survives relaunches. The URL hash holds the view;
  load the page with a hash to open that view. The web view must be
  transparent (drawsBackground = false): html, body and the canvas are.
================================================================================ */

(() => {
  const native = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.fx;
  const queue = [];
  let scheduled = false, nextId = 1;
  const pending = new Map();
  const sent = { params: {}, system: {} };

  // Values changed since the last `set`; null if none.
  function diff(src, last) {
    let out = null;
    for (const k of Object.keys(src)) {
      if (src[k] !== last[k]) { (out || (out = {}))[k] = src[k]; last[k] = src[k]; }
    }
    return out;
  }

  function flush() {
    scheduled = false;
    const p = diff(params, sent.params), s = diff(system, sent.system);
    if (p || s) queue.unshift({ type: 'set', ...(p ? { params: p } : {}), ...(s ? { system: s } : {}) });
    if (!queue.length) return;
    const batch = queue.splice(0);
    if (native) native.postMessage(batch);
    else stub.receive(batch);
  }

  function post(msg) {
    queue.push(msg);
    if (!scheduled) { scheduled = true; queueMicrotask(flush); }
  }

  function request(msg) {
    return new Promise((resolve) => {
      const id = nextId++;
      pending.set(id, resolve);
      post({ ...msg, id });
    });
  }

  // ---------------- what native tells us ----------------
  const mirror = { fps: 0, quality: 1, width: 0, height: 0, refreshHz: 60, maxSize: 16384, painted: 0, taps: 0, particles: 0 };
  let canvas = null, fsOn = false, film = null;
  const grabEls = () => [document.getElementById('grab0'), document.getElementById('grab1')];

  const handlers = {
    reply({ id, ok, error }) {
      const r = pending.get(id);
      if (!r) return;
      pending.delete(id);
      r({ ok: !!ok, error });
    },
    soundStarted() {
      const f = Sound.onstart;
      if (!f || !Sound.state.synth) return;
      Sound.onstart = null;
      f();
    },
    handsReady() { if (Inputs.state.hands) UI.toast('Hands ready · raise a finger to paint'); },
    handsFailed() {
      if (!Inputs.state.hands) return;
      Inputs.state.hands = false;
      UI.toast("Fingertip tracking couldn't start · check your connection");
      UI.refresh();
    },
    cameraFailed() {
      if (!Inputs.state.cam) return;
      Inputs.state.cam = false;
      UI.toast('The camera stopped');
      UI.refresh();
    },
    cameras(p) {
      camList = (p && Array.isArray(p.list) ? p.list : []).filter((c) => c && c.id).map((c) => ({ id: String(c.id), label: String(c.label || 'Camera') }));
      UI.refresh();
    },
    recordStarted(p) { if (film) film.started(!!(p && p.sound)); },
    recordSaved(p) { const f = film; film = null; if (f) f.saved(p && p.file); },
    recordFailed() { const f = film; film = null; if (f) f.failed(); },
    fullscreen(p) {
      fsOn = !!(p && p.on);
      document.dispatchEvent(new Event('fullscreenchange'));
    },
    nextChamber() { UI.nextChamber(); },
    grip(p) {
      const els = grabEls();
      (p && p.hands || []).forEach((h, i) => {
        if (!els[i] || !h) return;
        els[i].classList.toggle('on', !!h.on);
        if (h.on) els[i].style.transform = `translate3d(${h.x * innerWidth}px, ${h.y * innerHeight}px, 0)`;
      });
    },
  };

  window.__fx = {
    state(s) {
      for (const k of Object.keys(mirror)) if (s && typeof s[k] === 'number') mirror[k] = s[k];
      if (typeof UI !== 'undefined') { UI.stats.fps = mirror.fps; UI.stats.quality = mirror.quality; }
      // The interface reads the picture's size from the canvas (aspect for 4K export).
      if (canvas && mirror.width && mirror.height && (canvas.width !== mirror.width || canvas.height !== mirror.height)) {
        canvas.width = mirror.width; canvas.height = mirror.height;
      }
    },
    event(name, payload) {
      const h = handlers[name];
      if (h) h(payload || {});
      else console.warn('fx: unknown event', name);
    },
  };

  // ---------------- browser features that are now native ----------------
  // Full screen is the window's: the interface keeps reading document.fullscreenElement
  // and listening for fullscreenchange.
  Object.defineProperty(document, 'fullscreenElement', { configurable: true, get: () => (fsOn ? document.documentElement : null) });
  document.exitFullscreen = () => { post({ type: 'fullscreen', on: false }); return Promise.resolve(); };
  document.documentElement.requestFullscreen = () => { post({ type: 'fullscreen', on: true }); return Promise.resolve(); };

  // Screen wake lock becomes a display-sleep assertion.
  Object.defineProperty(navigator, 'wakeLock', {
    configurable: true,
    value: {
      async request() {
        post({ type: 'awake', on: true });
        const lock = new EventTarget();
        let live = true;
        lock.release = async () => {
          if (!live) return;
          live = false;
          post({ type: 'awake', on: false });
          lock.dispatchEvent(new Event('release'));
        };
        return lock;
      },
    },
  });

  // Painting, scrolling and gestures over the bare picture are native.
  let overUI = false;
  document.addEventListener('pointerover', (e) => {
    const t = e.target;
    const on = !(t === canvas || t === document.body || t === document.documentElement);
    if (on !== overUI) { overUI = on; post({ type: 'overUI', on }); }
  }, true);

  // ---------------- facades with the surface the interface uses ----------------
  window.Renderer = {
    startVeil(seconds) { post({ type: 'veil', seconds }); },
    clear() { post({ type: 'clear' }); },
    restore() { post({ type: 'restore' }); },
    resize() {},
    get size() { return [mirror.width, mirror.height]; },
    get maxSize() { return mirror.maxSize; },
  };

  window.Pens = {
    clear() {},                              // part of `clear`
    get painted() { return mirror.painted; },
    get taps() { return mirror.taps; },
    get particles() { return mirror.particles; },
  };

  const S = {
    cam: false, hands: false, twoHands: true, handsCpu: false,
    midi: false, tilt: false, tiltX: 0, tiltY: 0, learnKey: null, camera: '',
  };
  try { if (localStorage.getItem('mn:twohands') === '0') S.twoHands = false; } catch (e) {}
  try { S.camera = localStorage.getItem('mn:camera') || ''; } catch (e) {}
  const handsOptions = () => post({ type: 'handsOptions', twoHands: S.twoHands, cpu: S.handsCpu });
  const cameraDevice = () => post({ type: 'cameraDevice', device: S.camera });
  let camList = [];

  window.Inputs = {
    state: S,
    async setCam(on) {
      if (!on) { S.cam = false; post({ type: 'camera', on: false }); return; }
      const r = await request({ type: 'camera', on: true });
      if (!r.ok) throw new Error("That isn't available here");
      S.cam = true;
    },
    async setHands(on) {
      if (!on) { S.hands = false; post({ type: 'hands', on: false }); return; }
      const r = await request({ type: 'hands', on: true });
      if (!r.ok) throw new Error("Fingertip tracking couldn't start · check your connection");
      S.hands = true;
    },
    setTwoHands(v) {
      S.twoHands = v;
      try { localStorage.setItem('mn:twohands', v ? '1' : '0'); } catch (e) {}
      handsOptions();
    },
    async setHandsCpu(v) { S.handsCpu = v; handsOptions(); },
    async setCamera(id) {
      S.camera = id;
      try { if (id) localStorage.setItem('mn:camera', id); else localStorage.removeItem('mn:camera'); } catch (e) {}
      cameraDevice();
    },
    get cameras() { return camList; },
    // The camera in use: the chosen one, unless it is not connected.
    get camera() { return camList.some((c) => c.id === S.camera) ? S.camera : ''; },
    camerasNote: '',
    // No MIDI natively: the switch stays off.
    async setMidi() { S.midi = false; },
    learn() {},
    async setTilt(on) { if (on) throw new Error('This device has no tilt sensor'); },
    takeFrame: () => false,
    feedHands() {},
    video: null,
  };

  window.Sound = {
    state: { synth: false, mic: false },
    level: { bass: 0, mid: 0, high: 0 },   // consumed natively
    onstart: null,
    setSynth(on) {
      if (on === this.state.synth) return;
      this.state.synth = on;
      post({ type: 'sound', on });
    },
    setRoot(i) { post({ type: 'root', chamber: i }); },
    async setMic(on) {
      if (!on) { this.state.mic = false; post({ type: 'mic', on: false }); return; }
      const r = await request({ type: 'mic', on: true });
      if (!r.ok) throw new Error('Listening needs microphone access · allow it in the address bar');
      this.state.mic = true;
    },
    wake() {},                               // native needs no gesture
    pluck() {}, update() {},
    recordStream: () => null,
    fadeRecord: () => Promise.resolve(),     // native fades as the film ends
    endRecord() {},
  };

  window.Mp4 = {
    // One film at a time. `on` = { started(sound), saved(file), failed() }.
    record(file, on) {
      film = on;
      post({ type: 'record', on: true, file });
      return { stop() { post({ type: 'record', on: false }); } };
    },
    share(file) { post({ type: 'share', file }); },
  };

  // ---------------- the frame loop ----------------
  window.Main = (() => {
    let last = 0, acc = 0, breathT = 0, breathIn = false, breathN = 0;

    // The page is a layer over the native picture: unless something on it moves every
    // frame (UI.busy), 15 updates a second keep its timers and readouts, and it idles between.
    function loop(now) {
      requestAnimationFrame(loop);
      const raw = (now - last) / 1000;
      last = now;
      if (UI.paused || raw <= 0) return;
      acc += raw;
      if (!UI.busy && acc < 1 / 15) return;
      const dt = Math.min(acc, UI.busy ? 0.05 : 0.25);
      acc = 0;
      UI.update(dt);
      if (system.breathGuide) {                // only for the words; native breathes the picture
        breathT = (breathT + dt) % 10;
        const inh = breathT < 4;
        if (inh !== breathIn) { breathIn = inh; UI.breathCue(inh, breathN); if (!inh) breathN++; }
      }
      UI.afterRender();
      UI.afterFrame(dt);
      flush();
    }

    function start() {
      canvas = document.getElementById('c');
      if (mirror.width) window.__fx.state({});
      UI.init(canvas);
      handsOptions();
      cameraDevice();
      post({ type: 'ready' });
      flush();
      requestAnimationFrame((t) => { last = t; requestAnimationFrame(loop); });
    }

    return {
      start,
      step() {}, resize() {},
      breathe(on) { breathT = 0; breathIn = false; breathN = 0; post({ type: 'breathe', on }); },
      get refreshHz() { return mirror.refreshHz; },
      set filming(v) {},
      snapshot(file) { post({ type: 'snapshot', file }); },
      exportStill(width, height, file) { return request({ type: 'export', width, height, file }).then((r) => r.ok); },
      copy(text) { post({ type: 'copy', text }); return Promise.resolve(); },
    };
  })();

  // ---------------- stand-in for the app, in a plain browser ----------------
  // Logs every message (window.__fxLog), answers like the app would, and paints
  // the page loud so a transparent interface is easy to see. Set
  // window.__fxLog.fail = { hands: 'denied', record: true, ... } to rehearse failures.
  const stub = {
    receive(batch) {
      const log = window.__fxLog;
      for (const m of batch) {
        log.push(m);
        if (m.type !== 'set' || log.verbose) console.log('fx →', JSON.stringify(m));
        const fail = log.fail[m.type];
        const later = (fn, ms = 300) => setTimeout(fn, ms);
        if (m.id) later(() => __fx.event('reply', fail ? { id: m.id, ok: false, error: fail } : { id: m.id, ok: true }));
        if (m.type === 'sound' && m.on) later(() => __fx.event('soundStarted'));
        if (m.type === 'hands' && m.on && !fail) later(() => __fx.event('handsReady'), 900);
        if (m.type === 'ready') later(() => __fx.event('cameras', { list: [{ id: 'a', label: 'Built-in Camera' }, { id: 'b', label: 'USB <Capture>' }] }));
        if (m.type === 'fullscreen') later(() => __fx.event('fullscreen', { on: m.on }), 100);
        if (m.type === 'record' && m.on) later(() => (fail ? __fx.event('recordFailed') : __fx.event('recordStarted', { sound: Sound.state.synth })));
        if (m.type === 'record' && !m.on) later(() => __fx.event('recordSaved', { file: stub.file }), 600);
        if (m.type === 'record' && m.on) stub.file = m.file;
      }
    },
  };
  if (!native) {
    window.__fxLog = Object.assign([], { fail: {}, verbose: false, errors: [] });
    addEventListener('error', (e) => __fxLog.errors.push(e.message));
    addEventListener('unhandledrejection', (e) => __fxLog.errors.push(String(e.reason)));
    document.documentElement.style.background = '#ff00c8';
    const fake = () => __fx.state({
      fps: 59 + Math.random(), quality: 1, refreshHz: 60, maxSize: 16384,
      width: Math.round(innerWidth * devicePixelRatio), height: Math.round(innerHeight * devicePixelRatio),
    });
    fake();
    setInterval(fake, 250);
  }
})();
