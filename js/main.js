// Frame loop, frame-rate-independent parameter scaling, and adaptive quality.

const Main = (() => {
  const canvas = document.getElementById('c');
  let time = 0, last = 0;
  // Per-frame values handed to the renderer (reused, no per-frame allocation).
  const f = {
    n: 1, dt: 1 / 60, pace: 1, decay: 1, hue: 0, fbRot: 0, fbZoom: 1, fbSwirl: 0, fbWarp: 0, fbWarpT: 0,
    genT: 0, rmZ: 0, rmT: 0, palPhase: 0, camOn: false, inkCount: 0, inkCopies: 1,
  };

  // Adaptive quality: measure the display refresh first, then scale render
  // resolution so the loop holds that rate.
  let quality = 1, refreshHz = 60, calib = [], fpsAcc = 0, fpsFrames = 0, good = 0;
  let trial = null, cooldown = 0;   // a resolution drop that didn't raise fps is undone

  function resize(force) {
    const dpr = Math.min(window.devicePixelRatio || 1, system.maxDpr);
    const cw = Math.max(1, Math.round(window.innerWidth * dpr));
    const ch = Math.max(1, Math.round(window.innerHeight * dpr));
    if (force || canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
    const s = system.renderScale * quality;
    Renderer.resize(cw * s, ch * s);
  }

  function step(dt) {
    // Pace scales every motion (flow, turning, color drift, wanderers) but not
    // how long echoes last or how pens follow the hand.
    const P = params, n = dt * 60, rad = Math.PI / 180;
    const pdt = dt * P.pace, pn = n * P.pace;
    time += pdt;
    UI.update(dt);
    Palette.update(dt);
    Fold.update(pdt, time, Inputs.state.tiltX, Inputs.state.tiltY);
    Renderer.fadeVeil(dt);

    f.n = n;
    f.dt = dt;
    f.pace = P.pace;
    f.decay = Math.pow(P.fbDecay, n);
    f.hue = P.fbHueShift * rad * pdt;
    f.fbRot = P.fbRotate * rad * pdt;
    f.fbZoom = Math.pow(P.fbZoom, pn);
    f.fbSwirl = P.fbSwirl * pn;
    f.fbWarp = P.fbWarp * pn;
    f.fbWarpT += pdt * 0.5;
    f.genT += pdt * P.genSpeed;
    f.rmZ += pdt * P.rmFly;
    f.rmT += pdt;
    f.palPhase += pdt * P.paletteSpeed;

    f.camOn = Inputs.state.cam;
    if (f.camOn && Inputs.takeFrame()) Renderer.uploadCamera(Inputs.video);

    f.inkCount = Pens.build(dt, time, f);
    f.inkCopies = fold.mode === 0 ? (fold.mirror ? 2 * fold.sym : fold.sym) : 1;
    Renderer.render(f);
  }

  function adapt(dt) {
    fpsAcc += dt; fpsFrames++;
    if (fpsAcc < 1) return;
    const fps = fpsFrames / fpsAcc;
    fpsAcc = 0; fpsFrames = 0;
    UI.stats.fps = fps;
    if (!system.autoQuality) { if (quality !== 1) { quality = 1; UI.stats.quality = 1; resize(); } return; }
    let q = quality;
    if (trial && fps < trial.fps * 1.05 && fps < refreshHz * 0.9) {
      q = trial.q; cooldown = 8;      // not pixel-bound (e.g. a load stall): restore
    } else if (fps < refreshHz * 0.9 && cooldown <= 0) {
      trial = { q, fps };
      q = Math.max(0.3, q * 0.85); good = 0;
    } else if (fps > refreshHz * 0.97 && q < 1 && ++good >= 3) {
      q = Math.min(1, q * 1.1); good = 0;
    }
    if (q >= quality) trial = null;
    cooldown--;
    q = Math.round(q * 20) / 20;
    if (q !== quality) { quality = q; UI.stats.quality = q; resize(); }
  }

  function loop(now) {
    requestAnimationFrame(loop);
    const dt = Math.min((now - last) / 1000, 0.05);
    last = now;
    if (calib.length < 30) {           // ~0.25 s: measure refresh rate on an idle loop
      if (dt > 0) calib.push(dt);
      if (calib.length === 30) {
        calib.sort((a, b) => a - b);
        refreshHz = Math.min(240, 1 / calib[15]);
      }
      return;
    }
    if (UI.paused) return;
    adapt(dt);
    resize();
    step(dt);
    UI.afterRender();
    UI.afterFrame(dt);
  }

  function start() {
    try {
      Renderer.init(canvas);
    } catch (e) {
      document.getElementById('fatal').hidden = false;
      document.getElementById('fatal').textContent = e.message;
      throw e;
    }
    Pens.attach(canvas);
    UI.init(canvas);
    resize(true);
    requestAnimationFrame((t) => { last = t; requestAnimationFrame(loop); });
  }

  return { start, step, resize, get refreshHz() { return refreshHz; } };
})();

Main.start();
