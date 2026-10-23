// Space folding shared by GPU and CPU. The GLSL in shaders.js mirrors these
// functions exactly; the CPU copy lets pens draw at fold(cursor), so a stroke
// appears under the cursor *and* in every mirrored/tiled copy, in every mode.
// Noise uses an integer hash so both sides compute the same values.

const TAU = Math.PI * 2;

// Per-frame fold state, written by Fold.update(), read by the renderer and pens.
const fold = {
  mode: 1, sym: 6, mirror: true, spin: 0, cx: 0, cy: 0, vz: 1,
  warpAmt: 0, warpScale: 1.5, warpT: 0,
  kIters: 5, kScale: 1.5, kAngle: 0, kOffX: 0.6, kOffY: 0.3,
  hypC: 2, hypR: 1.7, hypAx: 0, hypAy: 0,
  drL: 1, drPhase: 0, tile: 2,
};

const Fold = (() => {
  let kAnglePhase = 0;
  const out = [0, 0];

  function pcgX(x, y) {
    x = (Math.imul(x, 1664525) + 1013904223) >>> 0;
    y = (Math.imul(y, 1664525) + 1013904223) >>> 0;
    x = (x + Math.imul(y, 1664525)) >>> 0;
    y = (y + Math.imul(x, 1664525)) >>> 0;
    x = (x ^ (x >>> 16)) >>> 0;
    y = (y ^ (y >>> 16)) >>> 0;
    x = (x + Math.imul(y, 1664525)) >>> 0;
    y = (y + Math.imul(x, 1664525)) >>> 0;
    hx = ((x ^ (x >>> 16)) >>> 0) * (2 / 4294967295) - 1;
    hy = ((y ^ (y >>> 16)) >>> 0) * (2 / 4294967295) - 1;
  }
  let hx = 0, hy = 0;

  function grad(ix, iy, fx, fy) {
    pcgX(ix >>> 0, iy >>> 0);
    return hx * fx + hy * fy;
  }

  function gnoise(x, y) {
    const ix = Math.floor(x), iy = Math.floor(y);
    const fx = x - ix, fy = y - iy;
    const ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const a = grad(ix, iy, fx, fy), b = grad(ix + 1, iy, fx - 1, fy);
    const c = grad(ix, iy + 1, fx, fy - 1), d = grad(ix + 1, iy + 1, fx - 1, fy - 1);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
  }

  function hypCircle(p, q) {
    const sp = Math.sin(Math.PI / p), cq = Math.cos(Math.PI / q);
    const r = sp / Math.sqrt(cq * cq - sp * sp);
    return [Math.sqrt(1 + r * r), r];
  }

  function validQ(p, q) {
    while ((p - 2) * (q - 2) <= 4) q++;
    return q;
  }

  function update(dt, time, tiltX, tiltY) {
    const P = params;
    fold.mode = MODES.indexOf(P.mode);
    fold.sym = Math.max(2, Math.round(P.symmetry));
    fold.mirror = P.mirror;
    // Moving a hyperbolic fold inside the feedback loop re-tiles the edge of the
    // disk every frame, which makes the picture boil; hold it still there.
    const still = fold.mode === 3 && P.foldInLoop;
    if (!still) fold.spin += P.spinSpeed * Math.PI / 180 * dt;
    fold.cx += (tiltX - fold.cx) * Math.min(1, dt * 6);
    fold.cy += (tiltY - fold.cy) * Math.min(1, dt * 6);
    fold.vz = P.viewZoom;
    fold.warpAmt = P.warpAmount;
    fold.warpScale = P.warpScale;
    fold.warpT += P.warpSpeed * dt;
    fold.kIters = Math.round(P.kifsIters);
    fold.kScale = P.kifsScale;
    kAnglePhase += P.kifsAngleSpeed * dt;
    fold.kAngle = (P.kifsAngle + kAnglePhase) * Math.PI / 180;
    fold.kOffX = P.kifsOffX;
    fold.kOffY = P.kifsOffY;
    const hp = Math.max(3, fold.sym);
    const [c, r] = hypCircle(hp, validQ(hp, Math.round(P.hypQ)));
    fold.hypC = c; fold.hypR = r;
    if (!still) {
      fold.hypAx = P.hypDrift * 0.6 * Math.sin(time * 0.13);
      fold.hypAy = P.hypDrift * 0.6 * Math.sin(time * 0.17 + 1);
    }
    fold.drL = Math.log(P.drosteScale);
    fold.drPhase += P.drosteSpeed * dt * fold.drL * 0.25;
    fold.tile = P.tileScale;
  }

  // Fold point (x, y) in p-space; returns the shared `out` array.
  function apply(x, y) {
    const f = fold;
    if (f.warpAmt > 0) {
      const s = f.warpScale, t = f.warpT;
      const ox = gnoise(x * s + t, y * s);
      const oy = gnoise(x * s + 5.2, y * s + 1.3 - t);
      const wx = (x + ox) * s * 2, wy = (y + oy) * s * 2;
      const o2x = gnoise(wx + 1.7, wy + 9.2 + t);
      const o2y = gnoise(wx + 8.3 - t, wy + 2.8);
      x += f.warpAmt * (ox + 0.5 * o2x);
      y += f.warpAmt * (oy + 0.5 * o2y);
    }
    if (f.mode === 0) { out[0] = x; out[1] = y; return out; }

    let cs = Math.cos(-f.spin), sn = Math.sin(-f.spin);
    let dx = (x - f.cx) * f.vz, dy = (y - f.cy) * f.vz;
    let zx = cs * dx - sn * dy, zy = sn * dx + cs * dy;

    if (f.mode === 1) {
      const seg = TAU / f.sym;
      let a = Math.atan2(zy, zx);
      a -= seg * Math.floor(a / seg);
      if (f.mirror && a > seg * 0.5) a = seg - a;
      const l = Math.hypot(zx, zy);
      zx = l * Math.cos(a); zy = l * Math.sin(a);
    } else if (f.mode === 2) {
      const s = f.kScale, rc = Math.cos(f.kAngle), rs = Math.sin(f.kAngle);
      for (let i = 0; i < f.kIters; i++) {
        zx = Math.abs(zx); zy = Math.abs(zy);
        if (zx < zy) { const t = zx; zx = zy; zy = t; }
        zx = zx * s - f.kOffX * (s - 1);
        zy = zy * s - f.kOffY * (s - 1);
        const nx = rc * zx - rs * zy; zy = rs * zx + rc * zy; zx = nx;
      }
      const k = Math.pow(s, -f.kIters);
      zx *= k; zy *= k;
    } else if (f.mode === 3) {
      const d2 = zx * zx + zy * zy;
      if (d2 > 1) { zx /= d2; zy /= d2; }
      // Möbius translation: (z - a) / (1 - conj(a) z)
      const ax = f.hypAx, ay = f.hypAy;
      const nx = zx - ax, ny = zy - ay;
      const ddx = 1 - (ax * zx + ay * zy), ddy = -(ax * zy - ay * zx);
      const dd = ddx * ddx + ddy * ddy;
      zx = (nx * ddx + ny * ddy) / dd; zy = (ny * ddx - nx * ddy) / dd;
      const seg = TAU / Math.max(3, f.sym), C = f.hypC, r2 = f.hypR * f.hypR;
      for (let i = 0; i < 32; i++) {
        let a = Math.atan2(zy, zx);
        a -= seg * Math.floor(a / seg);
        if (a > seg * 0.5) a = seg - a;
        const l = Math.hypot(zx, zy);
        zx = l * Math.cos(a); zy = l * Math.sin(a);
        const vx = zx - C, vy = zy;
        const vv = vx * vx + vy * vy;
        if (vv >= r2) break;
        zx = C + vx * r2 / vv; zy = vy * r2 / vv;
      }
    } else if (f.mode === 4) {
      const L = f.drL;
      let wx = Math.log(Math.max(Math.hypot(zx, zy), 1e-6)), wy = Math.atan2(zy, zx);
      // w *= (1 - i L/2π): one turn around the center advances one ratio step
      const T = L / TAU;
      const nx = wx + wy * T; wy = wy - wx * T; wx = nx;
      wx = wx - f.drPhase;
      wx = wx - L * Math.floor(wx / L) - L;
      const e = Math.exp(wx) * 0.9;
      zx = e * Math.cos(wy); zy = e * Math.sin(wy);
    } else {
      const t = f.tile;
      zx = zx * t + 1; zy = zy * t + 1;
      zx = Math.abs(zx - 2 * Math.floor(zx / 2) - 1);
      zy = Math.abs(zy - 2 * Math.floor(zy / 2) - 1);
      if (zx < zy) { const s = zx; zx = zy; zy = s; }
      zx /= t; zy /= t;
    }

    cs = Math.cos(f.spin); sn = Math.sin(f.spin);
    out[0] = f.cx + (cs * zx - sn * zy) / f.vz;
    out[1] = f.cy + (sn * zx + cs * zy) / f.vz;
    return out;
  }

  return { update, apply, gnoise };
})();
