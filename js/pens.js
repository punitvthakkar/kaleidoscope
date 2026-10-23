// Pens: pointers (mouse, multi-touch, stylus), autopilot pens, hand-tracked
// fingertips and spark particles. Each frame they emit glowing capsule
// segments into Renderer.inkData, pre-folded so they land where you see them.

const Pens = (() => {
  const pointers = new Map();
  const autos = [];
  const hands = [];
  const PMAX = 4096;
  const parts = new Float32Array(PMAX * 8); // x, y, vx, vy, life, hue, fx, fy
  let pCount = 0, emitAcc = 0, autoT = 0;
  let n = 0;                    // segments written this frame
  let lastT = 0;                // palette coordinate of the pen being drawn
  let dt = 0, palPhase = 0;
  let fr = null;                // per-frame feedback values from Main
  let ux = 0, uy = 0;
  const data = Renderer.inkData;
  let canvasEl;

  const LISSAJOUS = [[3, 2], [5, 4], [3, 4], [5, 6], [7, 4], [2, 5], [7, 6], [4, 9]];
  const ROSE = [3, 5, 2, 7, 4, 6, 3, 5];

  function pen(kind, offset) {
    return { kind, x: 0, y: 0, px: 0, py: 0, has: false, down: false, inside: false, hue: offset, offset, v: 0, vx: 0, vy: 0 };
  }

  // Screen pixel → p-space (y up, screen height spans [-1, 1]), with the
  // display fisheye applied so ink lands under the cursor.
  function toP(clientX, clientY, out) {
    const w = canvasEl.clientWidth, h = canvasEl.clientHeight;
    let x = (clientX - w / 2) / h * 2, y = -(clientY - h / 2) / h * 2;
    const k = 1 + params.fisheye * (x * x + y * y) * 0.3;
    out.x = x * k; out.y = y * k;
  }

  function attach(canvas) {
    canvasEl = canvas;
    const tmp = { x: 0, y: 0 };
    const get = (e) => {
      let p = pointers.get(e.pointerId);
      if (!p) { p = pen(e.pointerType, pointers.size * 0.17); pointers.set(e.pointerId, p); }
      return p;
    };
    canvas.addEventListener('pointerdown', (e) => {
      const p = get(e);
      toP(e.clientX, e.clientY, tmp);
      p.x = p.px = tmp.x; p.y = p.py = tmp.y;
      p.down = true; p.inside = true; p.has = false;
      canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener('pointermove', (e) => {
      const p = get(e);
      // coalesced events give smoother strokes on high-rate mice and pens
      const evs = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
      const last = evs.length ? evs[evs.length - 1] : e;
      toP(last.clientX, last.clientY, tmp);
      if (!p.inside) { p.px = tmp.x; p.py = tmp.y; p.has = false; }
      p.x = tmp.x; p.y = tmp.y; p.inside = true;
    });
    const up = (e) => {
      const p = pointers.get(e.pointerId);
      if (!p) return;
      p.down = false;
      if (e.pointerType !== 'mouse') pointers.delete(e.pointerId);
    };
    canvas.addEventListener('pointerup', up);
    canvas.addEventListener('pointercancel', up);
    canvas.addEventListener('pointerleave', (e) => {
      const p = pointers.get(e.pointerId);
      if (p) { p.inside = false; p.has = false; }
    });
  }

  function push(ax, ay, bx, by, rad, src) {
    if (n >= Renderer.MAX_SEG) return;
    const o = n * 8;
    data[o] = ax; data[o + 1] = ay; data[o + 2] = bx; data[o + 3] = by;
    data[o + 4] = src[0]; data[o + 5] = src[1]; data[o + 6] = src[2];
    data[o + 7] = rad;
    n++;
  }

  // Local scale of the fold map at (x, y): keeps stroke width constant on screen.
  function foldScale(x, y) {
    const e = 1e-3;
    let f = Fold.apply(x, y); const fx = f[0], fy = f[1];
    f = Fold.apply(x + e, y); const sx = Math.hypot(f[0] - fx, f[1] - fy);
    f = Fold.apply(x, y + e); const sy = Math.hypot(f[0] - fx, f[1] - fy);
    return Math.min(Math.max(Math.min(sx, sy) / e, 0.01), 50);
  }

  // Where a buffer point drawn last frame has been carried by the feedback
  // transform: the inverse of fbTransform in shaders.js (ignoring melt noise).
  // Starting each segment there keeps trails continuous instead of dashed.
  function carried(x, y) {
    const dx = x - fold.cx, dy = y - fold.cy, z = fr.fbZoom;
    const a = -(fr.fbRot + fr.fbSwirl * Math.hypot(dx, dy) * z);
    const c = Math.cos(a) * z, s = Math.sin(a) * z;
    ux = fold.cx + c * dx - s * dy;
    uy = fold.cy + s * dx + c * dy;
  }

  const col = new Float32Array(3);
  // Emit a stroke segment in screen p-space, folding it into buffer space.
  function stroke(ax, ay, bx, by, rad) {
    const len = Math.hypot(bx - ax, by - ay);
    if (fold.mode === 0) {
      if (len < 1e-5) { push(ax, ay, ax, ay, rad, col); return; }
      carried(ax, ay);
      push(ux, uy, bx, by, rad, col);
      return;
    }
    // Hyperbolic chambers show one small triangle everywhere, so strokes are
    // drawn finer there or a single sweep would flood the whole screen.
    const js = foldScale((ax + bx) / 2, (ay + by) / 2);
    const s = js * (fold.mode === 3 ? 0.4 : 1);
    let f = Fold.apply(ax, ay);
    let x0 = f[0], y0 = f[1];
    if (len < 1e-5) { push(x0, y0, x0, y0, rad * s, col); return; }
    carried(x0, y0);
    x0 = ux; y0 = uy;
    const steps = Math.min(16, Math.ceil(len / 0.02));
    for (let i = 1; i <= steps; i++) {
      const t = i / steps;
      f = Fold.apply(ax + (bx - ax) * t, ay + (by - ay) * t);
      const x1 = f[0], y1 = f[1];
      // skip pieces that jump across a seam: a continuous piece is about as
      // long as the local fold scale predicts
      if (Math.hypot(x1 - x0, y1 - y0) < (len / steps) * js * 3 + 0.004) push(x0, y0, x1, y1, rad * s, col);
      x0 = x1; y0 = y1;
    }
  }

  function emit(p, d) {
    emitAcc += dt * 60 + d * 300;
    while (emitAcc >= 1) {
      emitAcc -= 1;
      if (pCount >= PMAX) { emitAcc = 0; break; }
      const o = pCount * 8, a = Math.random() * TAU, sp = 0.15 + Math.random() * 0.6;
      parts[o] = p.x; parts[o + 1] = p.y;
      parts[o + 2] = p.vx * 0.25 + Math.cos(a) * sp;
      parts[o + 3] = p.vy * 0.25 + Math.sin(a) * sp;
      parts[o + 4] = 0.6 + Math.random();
      parts[o + 5] = lastT + Math.random() * 0.15;
      const f = Fold.apply(p.x, p.y);
      parts[o + 6] = f[0]; parts[o + 7] = f[1];
      pCount++;
    }
  }

  function updateParticles(time) {
    const P = params, rad = 0.0025 * P.inkSize, gain = P.inkBright * 1.2;
    const drag = Math.exp(-1.6 * dt);
    for (let i = 0; i < pCount; i++) {
      const o = i * 8;
      parts[o + 4] -= dt;
      if (parts[o + 4] <= 0) {
        pCount--;
        parts.copyWithin(o, pCount * 8, pCount * 8 + 8);
        i--;
        continue;
      }
      const x = parts[o], y = parts[o + 1];
      const cx = Fold.gnoise(x * 2 + time * 0.3, y * 2), cy = Fold.gnoise(x * 2 + 4.1, y * 2 - time * 0.3);
      parts[o + 2] = parts[o + 2] * drag + cy * dt * 2;
      parts[o + 3] = parts[o + 3] * drag - cx * dt * 2;
      const nx = x + parts[o + 2] * dt, ny = y + parts[o + 3] * dt;
      parts[o] = nx; parts[o + 1] = ny;
      const life = Math.min(1, parts[o + 4]);
      Palette.write(parts[o + 5] + palPhase, col, 0, gain * life);
      if (fold.mode === 0) {
        carried(x, y);
        push(ux, uy, nx, ny, rad, col);
      } else {
        const f = Fold.apply(nx, ny);
        carried(parts[o + 6], parts[o + 7]);
        const fx = ux, fy = uy;
        const dd = Math.hypot(f[0] - fx, f[1] - fy), ds = Math.hypot(nx - x, ny - y);
        const s = ds > 1e-6 ? Math.min(Math.max(dd / ds, 0.01), 50) : 1;
        if (dd < (fold.mode === 3 ? 0.02 : 0.15)) push(fx, fy, f[0], f[1], rad * s * (fold.mode === 3 ? 0.4 : 1), col);
        parts[o + 6] = f[0]; parts[o + 7] = f[1];
      }
    }
  }

  function autoPos(p, i, count) {
    const t = autoT, P = params;
    switch (P.autoPath) {
      case 'lissajous': {
        const [a, b] = LISSAJOUS[i % 8];
        p.x = 0.8 * Math.sin(a * t * 0.3 + i); p.y = 0.8 * Math.sin(b * t * 0.3 + i * 2.1);
        break;
      }
      case 'rose': {
        const th = t * 0.5 + i * TAU / count, r = 0.85 * Math.cos(ROSE[i % 8] * th);
        p.x = r * Math.cos(th); p.y = r * Math.sin(th);
        break;
      }
      case 'noise':
        p.x = 1.6 * Fold.gnoise(t * 0.35, i * 7.13 + 0.5);
        p.y = 1.6 * Fold.gnoise(i * 3.31 + 0.5, t * 0.35);
        break;
      default: {
        const th = t * 0.8 + i * TAU / count, r = 0.15 + 0.65 * (0.5 + 0.5 * Math.sin(t * 0.23 + i));
        p.x = r * Math.cos(th); p.y = r * Math.sin(th);
      }
    }
  }

  // Palette coordinate for a pen this frame.
  function colorT(p, v) {
    switch (params.inkColor) {
      case 'cycle': p.hue += dt * (0.03 * fr.pace + v * 0.06); return p.hue;
      case 'speed': return Math.min(v * 0.2, 1.2) + p.offset * 0.2;
      case 'angle': return Math.atan2(p.y, p.x) / TAU + p.offset * 0.1;
      default: return p.offset;
    }
  }

  // Video-normalized fingertip coordinates → p-space (mirrored, fills screen).
  function setHands(list) {
    const asp = canvasEl.clientWidth / canvasEl.clientHeight;
    let k = 0;
    for (const tips of list) {
      for (const [x, y] of tips) {
        if (!hands[k]) hands[k] = pen('hand', k * 0.11);
        const p = hands[k];
        const tx = (0.5 - x) * 2 * asp, ty = (0.5 - y) * 2;
        if (!p.down) { p.x = tx; p.y = ty; }
        p.x += (tx - p.x) * 0.6; p.y += (ty - p.y) * 0.6;
        p.down = true;
        k++;
      }
    }
    for (; k < hands.length; k++) hands[k].down = false;
  }

  function build(_dt, time, frame) {
    dt = _dt; fr = frame; palPhase = frame.palPhase; n = 0;
    const P = params;
    autoT += dt * P.autoSpeed * frame.pace;
    const count = Math.round(P.autoPens);
    while (autos.length < count) autos.push(pen('auto', autos.length * 0.13));
    for (let i = 0; i < autos.length; i++) {
      if (i < count) autoPos(autos[i], i, count);
      run(autos[i], i < count);
    }
    for (const p of pointers.values()) run(p, p.kind === 'mouse' ? p.inside && (p.down || P.inkHover) : p.down);
    for (const p of hands) run(p, p.down);
    updateParticles(time);
    return n;
  }

  function run(p, active) {
    if (active && p.has) lastT = colorT(p, p.v);
    drawPen(p, active);
  }

  function drawPen(p, active) {
    if (!active) { p.has = false; return; }
    if (!p.has) { p.px = p.x; p.py = p.y; p.has = true; p.v = 0; return; }
    const dx = p.x - p.px, dy = p.y - p.py;
    const d = Math.hypot(dx, dy);
    const inv = 1 / Math.max(dt, 1e-3);
    p.v += (d * inv - p.v) * Math.min(1, dt * 20);
    p.vx = dx * inv; p.vy = dy * inv;
    if (d > 0.6) { p.px = p.x; p.py = p.y; return; }
    const P = params;
    const rad = 0.006 * P.inkSize * (1 + P.inkSpeed * Math.min(p.v, 8) * 0.25);
    Palette.write(lastT + palPhase, col, 0, P.inkBright);
    if (d > 1e-4) {
      if (P.brush === 'neon') stroke(p.px, p.py, p.x, p.y, rad);
      else if (P.brush === 'dots') stroke(p.x, p.y, p.x, p.y, rad * 1.8);
      else { stroke(p.px, p.py, p.x, p.y, rad * 0.35); emit(p, d); }
    }
    p.px = p.x; p.py = p.y;
  }

  function clear() { pCount = 0; }

  return { attach, build, setHands, clear, get particles() { return pCount; } };
})();
