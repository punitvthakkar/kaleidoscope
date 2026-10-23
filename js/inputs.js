// External inputs: webcam, MediaPipe hand tracking (in a worker, so it never
// costs the render loop a frame), Web MIDI with learn, and device tilt.

const Inputs = (() => {
  const MP = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14';
  const MODEL = 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task';
  const TIPS_ALL = [4, 8, 12, 16, 20];
  const TIPS_INDEX = [8];

  const state = {
    cam: false,          // webcam feeds the visuals
    hands: false,
    allFingers: false,
    midi: false,
    tilt: false,
    tiltX: 0, tiltY: 0,
    learnKey: null,
  };

  let video = null, stream = null, freshFrame = false;
  let worker = null, workerReady = false, workerBusy = false;
  let pinched = [false, false];

  // ---------------- webcam ----------------
  async function openStream() {
    if (stream) return;
    stream = await navigator.mediaDevices.getUserMedia({
      video: { width: { ideal: 1280 }, height: { ideal: 720 }, frameRate: { ideal: 60 }, facingMode: 'user' },
      audio: false,
    });
    video = document.createElement('video');
    video.muted = true;
    video.playsInline = true;
    video.srcObject = stream;
    await video.play();
    if ('requestVideoFrameCallback' in video) {
      const onFrame = () => {
        if (!video) return;
        freshFrame = true;
        sendHandFrame();
        video.requestVideoFrameCallback(onFrame);
      };
      video.requestVideoFrameCallback(onFrame);
    } else {
      const poll = () => { if (!video) return; freshFrame = true; sendHandFrame(); setTimeout(poll, 33); };
      poll();
    }
  }

  function closeStreamIfUnused() {
    if (state.cam || state.hands || !stream) return;
    stream.getTracks().forEach((t) => t.stop());
    stream = null;
    video = null;
  }

  async function setCam(on) {
    if (on) {
      await openStream();
      state.cam = true;
    } else {
      state.cam = false;
      closeStreamIfUnused();
    }
  }

  function takeFrame() {
    const f = freshFrame;
    freshFrame = false;
    return f;
  }

  // ---------------- hand tracking ----------------
  // The CDN serves the CommonJS bundle as application/node (nosniff), which
  // importScripts rejects, so the worker fetches it and evaluates it instead.
  const WORKER_SRC = `
var exports = {};
var lm = null;
async function create(delegate) {
  if (!exports.FilesetResolver) {
    const src = await (await fetch('${MP}/vision_bundle.cjs')).text();
    new Function('exports', src)(exports);
  }
  const fs = await exports.FilesetResolver.forVisionTasks('${MP}/wasm');
  return exports.HandLandmarker.createFromOptions(fs, {
    baseOptions: { modelAssetPath: '${MODEL}', delegate: delegate },
    runningMode: 'VIDEO', numHands: 2,
  });
}
onmessage = async (e) => {
  const m = e.data;
  if (m.type === 'init') {
    try { lm = await create('GPU'); }
    catch (err) {
      try { lm = await create('CPU'); }
      catch (err2) { postMessage({ type: 'error', message: String(err2) }); return; }
    }
    postMessage({ type: 'ready' });
  } else if (m.type === 'frame') {
    let hands = [];
    if (lm) {
      try {
        const r = lm.detectForVideo(m.bitmap, m.t);
        hands = r.landmarks.map((h) => h.map((p) => [p.x, p.y]));
      } catch (err) {}
    }
    m.bitmap.close();
    postMessage({ type: 'hands', hands: hands });
  }
};`;

  async function setHands(on) {
    if (!on) {
      state.hands = false;
      if (worker) { worker.terminate(); worker = null; }
      workerReady = false; workerBusy = false;
      Pens.setHands([]);
      closeStreamIfUnused();
      return;
    }
    await openStream();
    state.hands = true;
    UI.toast('Loading fingertip tracking…');
    const url = URL.createObjectURL(new Blob([WORKER_SRC], { type: 'text/javascript' }));
    worker = new Worker(url);
    worker.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'ready') { workerReady = true; UI.toast('Fingertips draw · pinch to add a mirror'); }
      else if (m.type === 'error') { UI.toast("Fingertip tracking couldn't start · check your connection"); console.warn(m.message); setHands(false); UI.refresh(); }
      else if (m.type === 'hands') { workerBusy = false; onHands(m.hands); }
    };
    worker.onerror = (e) => { UI.toast("Fingertip tracking couldn't start · check your connection"); console.warn(e.message); setHands(false); UI.refresh(); };
    worker.postMessage({ type: 'init' });
  }

  async function sendHandFrame() {
    if (!worker || !workerReady || workerBusy || !video || video.readyState < 2) return;
    workerBusy = true;
    try {
      const w = 640, h = Math.round(640 * video.videoHeight / Math.max(1, video.videoWidth));
      const bitmap = await createImageBitmap(video, { resizeWidth: w, resizeHeight: h });
      if (!worker) { bitmap.close(); return; }
      worker.postMessage({ type: 'frame', bitmap, t: performance.now() }, [bitmap]);
    } catch (e) {
      workerBusy = false;
    }
  }

  function onHands(list) {
    const tips = state.allFingers ? TIPS_ALL : TIPS_INDEX;
    Pens.setHands(list.map((h) => tips.map((i) => h[i])));
    list.forEach((h, i) => {
      if (i > 1) return;
      const d = Math.hypot(h[4][0] - h[8][0], h[4][1] - h[8][1]);
      if (!pinched[i] && d < 0.045) { pinched[i] = true; UI.pinch(); }
      else if (pinched[i] && d > 0.08) pinched[i] = false;
    });
  }

  // ---------------- MIDI ----------------
  const DEFAULT_CC = ['fbZoom', 'fbRotate', 'symmetry', 'fbDecay', 'warpAmount', 'fbHueShift', 'chroma', 'bloom'];
  let bindings = {};
  try { bindings = JSON.parse(localStorage.getItem('ks-midi') || '{}'); } catch (e) { bindings = {}; }
  function bindingFor(cc) {
    if (bindings[cc]) return bindings[cc];
    if (cc >= 1 && cc <= 8) return DEFAULT_CC[cc - 1];
    if (cc >= 16 && cc <= 23) return DEFAULT_CC[cc - 16];
    return null;
  }

  function onMidi(e) {
    const [st, d1, d2] = e.data;
    const type = st & 0xf0;
    if (type === 0xb0) {
      if (state.learnKey) {
        bindings[d1] = state.learnKey;
        try { localStorage.setItem('ks-midi', JSON.stringify(bindings)); } catch (err) {}
        UI.toast(`Knob ${d1} now controls ${SCHEMA[state.learnKey][5].toLowerCase()}`);
        state.learnKey = null;
      }
      const key = bindingFor(d1);
      if (key) UI.setNormalized(key, d2 / 127);
    } else if (type === 0x90 && d2 > 0) {
      UI.note(d1);
    }
  }

  async function setMidi(on) {
    if (!on) { state.midi = false; return; }
    if (!navigator.requestMIDIAccess) throw new Error('This browser has no MIDI support · try Chrome or Edge');
    const access = await navigator.requestMIDIAccess();
    const attach = (inp) => { inp.onmidimessage = (e) => state.midi && onMidi(e); };
    access.inputs.forEach(attach);
    access.onstatechange = (e) => { if (e.port.type === 'input' && e.port.state === 'connected') attach(e.port); };
    state.midi = true;
    UI.toast(`MIDI connected · ${access.inputs.size} device${access.inputs.size === 1 ? '' : 's'} · keys switch chambers`);
  }

  function learn(key) {
    state.learnKey = key;
    UI.toast(`Turn a knob to control ${SCHEMA[key][5].toLowerCase()}`);
  }

  // ---------------- tilt ----------------
  let tiltBase = null, tiltListening = false;
  function onTilt(e) {
    if (!state.tilt || e.beta == null) return;
    if (tiltBase === null) tiltBase = e.beta;
    const c = (v) => Math.max(-1, Math.min(1, v));
    state.tiltX = c(e.gamma / 40) * 0.6;
    state.tiltY = c(-(e.beta - tiltBase) / 40) * 0.6;
  }

  async function setTilt(on) {
    if (!on) { state.tilt = false; state.tiltX = state.tiltY = 0; return; }
    if (typeof DeviceOrientationEvent === 'undefined') throw new Error('This device has no tilt sensor');
    if (typeof DeviceOrientationEvent.requestPermission === 'function') {
      const r = await DeviceOrientationEvent.requestPermission();
      if (r !== 'granted') throw new Error('Tilt needs motion access · allow it in settings');
    }
    tiltBase = null;
    if (!tiltListening) { window.addEventListener('deviceorientation', onTilt); tiltListening = true; }
    state.tilt = true;
  }

  return {
    state, setCam, setHands, setMidi, setTilt, learn, takeFrame,
    get video() { return video; },
  };
})();
