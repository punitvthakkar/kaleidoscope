// Web: Palette.update/write, Fold (update, apply, gnoise) and Motion.
#include "engine_impl.h"

// ---------------- noise ----------------
static void pcgX(uint32_t x, uint32_t y, double& hx, double& hy) {
  x = x * 1664525u + 1013904223u;
  y = y * 1664525u + 1013904223u;
  x += y * 1664525u;
  y += x * 1664525u;
  x ^= x >> 16;
  y ^= y >> 16;
  x += y * 1664525u;
  y += x * 1664525u;
  hx = (double)(x ^ (x >> 16)) * (2.0 / 4294967295.0) - 1;
  hy = (double)(y ^ (y >> 16)) * (2.0 / 4294967295.0) - 1;
}

static inline double grad(int64_t ix, int64_t iy, double fx, double fy) {
  double hx, hy;
  pcgX((uint32_t)ix, (uint32_t)iy, hx, hy);   // JS `ix >>> 0`
  return hx * fx + hy * fy;
}

double gnoise(double x, double y) {
  const double fix = std::floor(x), fiy = std::floor(y);
  const int64_t ix = (int64_t)fix, iy = (int64_t)fiy;
  const double fx = x - fix, fy = y - fiy;
  const double ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  const double a = grad(ix, iy, fx, fy), b = grad(ix + 1, iy, fx - 1, fy);
  const double c = grad(ix, iy + 1, fx, fy - 1), d = grad(ix + 1, iy + 1, fx - 1, fy - 1);
  return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

static inline double sabs(double x, double k) { return std::hypot(x, k) - k; }

// ---------------- palette ----------------
void palette_update(PaletteState& pal, int palette, double dt) {
  const float (*t)[3] = PALETTES[(palette >= 0 && palette < PALETTE_COUNT) ? palette : 0];
  const double k = std::min(1.0, dt * 1.4);
  for (int j = 0; j < 3; j++) {
    pal.a[j] += (float)((t[0][j] - pal.a[j]) * k); pal.b[j] += (float)((t[1][j] - pal.b[j]) * k);
    pal.c[j] += (float)((t[2][j] - pal.c[j]) * k); pal.d[j] += (float)((t[3][j] - pal.d[j]) * k);
  }
}

void palette_write(const PaletteState& pal, double t, float* arr, double gain) {
  for (int j = 0; j < 3; j++)
    arr[j] = (float)((pal.a[j] + pal.b[j] * std::cos(6.283185307 * (pal.c[j] * t + pal.d[j]))) * gain);
}

// ---------------- fold ----------------
static void hypCircle(int p, int q, double& c, double& r) {
  const double sp = std::sin(PI / p), cq = std::cos(PI / q);
  r = sp / std::sqrt(std::max(1e-5, cq * cq - sp * sp));
  c = std::sqrt(1 + r * r);
}

static int validQ(int p, int q) {
  while ((p - 2) * (q - 2) <= 4) q++;
  return q;
}

void FoldSys::update(const Params& P, double dt, double time, double tiltX, double tiltY) {
  f.mode = P.mode;
  f.sym = (float)std::max(2.0, jsround(P.symmetry));
  f.mirror = P.mirror;
  spin += P.spinSpeed * PI / 180 * dt;
  // A lens leans slowly toward the painting hand (never all the way: it would sit under
  // the finger), and wanders a little when left alone.
  double rate = 6;
  if (f.mode == 6 || f.mode == 7) {
    rate = 0.7;
    tiltX = lean ? 0.6 * leanX : 0.22 * std::sin(time * 0.11);
    tiltY = lean ? 0.6 * leanY : 0.16 * std::sin(time * 0.17 + 1);
  }
  fed += (std::min(feed, 3.0) / 3 - fed) * std::min(1.0, dt * 0.8);
  cx += (tiltX - cx) * std::min(1.0, dt * rate);
  cy += (tiltY - cy) * std::min(1.0, dt * rate);
  f.vz = P.viewZoom;
  f.warpAmt = P.warpAmount;
  f.warpScale = P.warpScale;
  warpT += P.warpSpeed * dt;
  f.kIters = P.kifsIters;
  f.kScale = P.kifsScale;
  kAnglePhase += P.kifsAngleSpeed * dt;
  // Harmonic oscillation around kifsAngle prevents spinning into degenerate flat creases.
  const double swing = P.kifsAngleSpeed != 0 ? 14 * std::sin(kAnglePhase * 0.14) : 0;
  f.kAngle = (float)(clampd(P.kifsAngle + swing, 6, 82) * PI / 180);
  f.kOffX = P.kifsOffX;
  f.kOffY = P.kifsOffY;
  const int hp = std::max(3, (int)f.sym);
  double c, r;
  hypCircle(hp, validQ(hp, (int)jsround(P.hypQ)), c, r);
  f.hypC = (float)c; f.hypR = (float)r;
  f.hypAx = (float)(P.hypDrift * 0.52 * std::sin(time * 0.13));
  f.hypAy = (float)(P.hypDrift * 0.52 * std::sin(time * 0.17 + 1));
  f.drL = (float)std::log(P.drosteScale);
  drPhase += P.drosteSpeed * dt * f.drL * 0.25;
  f.tile = P.tileScale;
  // The lens places borrow uniforms they would otherwise not use: the black hole's ring
  // (which swells while it is fed) and the wormhole's radius ride in uDroste.x, the
  // pearls' inversion radius in uTile.
  if (f.mode == 6) f.drL = (float)(P.lensRing * (1 + 0.35 * fed));
  else if (f.mode == 7) f.drL = P.lensRing;
  else if (f.mode == 8) f.tile = P.pearlScale;
  // The finger point: quick to follow a hand, slow to drift once it lets go.
  const double fr = std::min(1.0, dt * (lean ? 6 : 0.4));
  fx += ((lean ? leanX : 0.5 * std::sin(time * 0.21)) - fx) * fr;
  fy += ((lean ? leanY : 0.32 * std::sin(time * 0.29 + 2)) - fy) * fr;
  if (f.mode >= 10) { f.hypAx = (float)fx; f.hypAy = (float)fy; f.tile = P.placeAmount; }
  f.spin = (float)spin; f.cx = (float)cx; f.cy = (float)cy;
  f.warpT = (float)warpT; f.drPhase = (float)drPhase;
}

// Fold point (x, y) in p-space.
void FoldSys::apply(double x, double y, double out[2]) const {
  auto spun = [&](double zx, double zy) {
    const double cs = std::cos(f.spin), sn = std::sin(f.spin);
    out[0] = f.cx + cs * zx - sn * zy;
    out[1] = f.cy + sn * zx + cs * zy;
  };
  if (f.warpAmt > 0) {
    const double s = f.warpScale, t = f.warpT;
    const double ox = gnoise(x * s + t, y * s);
    const double oy = gnoise(x * s + 5.2, y * s + 1.3 - t);
    const double wx = (x + ox) * s * 2, wy = (y + oy) * s * 2;
    const double o2x = gnoise(wx + 1.7, wy + 9.2 + t);
    const double o2y = gnoise(wx + 8.3 - t, wy + 2.8);
    x += f.warpAmt * (ox + 0.5 * o2x);
    y += f.warpAmt * (oy + 0.5 * o2y);
  }
  if (f.mode == 0) { out[0] = x; out[1] = y; return; }

  const double cs = std::cos(-(double)f.spin), sn = std::sin(-(double)f.spin);
  const double dx = (x - f.cx) * f.vz, dy = (y - f.cy) * f.vz;
  double zx = cs * dx - sn * dy, zy = sn * dx + cs * dy;

  if (f.mode == 1) {
    const double seg = TAU / f.sym, half = seg * 0.5;
    double a = std::atan2(zy, zx);
    a -= seg * std::floor(a / seg);
    if (f.mirror) a = half - sabs(a - half, 0.002);
    const double l = std::hypot(zx, zy);
    zx = l * std::cos(a); zy = l * std::sin(a);
  } else if (f.mode == 2) {
    const double s = f.kScale, iters = f.kIters; const int maxI = (int)std::ceil(iters);
    zx = sabs(zx, 0.012); zy = sabs(zy, 0.012);
    const double d0 = zx - zy, sd0 = sabs(d0, 0.012);
    const double tx0 = 0.5 * (zx + zy + sd0), ty0 = 0.5 * (zx + zy - sd0);
    zx = tx0; zy = ty0;
    for (int i = 0; i < maxI; i++) {
      double w = clampd(iters - i, 0, 1);
      w = w * w * (3 - 2 * w);
      const double k = 0.018 / (1 + 0.3 * i);
      double fx = sabs(zx, k), fy = sabs(zy, k);
      const double d = fx - fy, sd = sabs(d, k);
      const double sx = 0.5 * (fx + fy + sd), sy = 0.5 * (fx + fy - sd);
      fx = sx * s - f.kOffX * (s - 1);
      fy = sy * s - f.kOffY * (s - 1);
      const double ang = f.kAngle / (1 + 0.45 * i);
      const double rc = std::cos(ang), rs = std::sin(ang);
      const double rx = rc * fx - rs * fy, ry = rs * fx + rc * fy;
      zx += (rx - zx) * w;
      zy += (ry - zy) * w;
    }
    const double norm = 1 / std::max(std::hypot((double)f.kOffX, (double)f.kOffY) * 1.25, 0.55);
    spun(zx * norm, zy * norm);
    return;
  } else if (f.mode == 3) {
    const double r = std::hypot(zx, zy);
    if (r > 1e-6) {
      const double e = std::max(r - 0.82, 0.0) * 3.5;
      const double rh = std::min(0.985, r / std::sqrt(1 + e * e));
      zx *= rh / r; zy *= rh / r;
    }
    const double ax = f.hypAx, ay = f.hypAy;
    const double nx = zx - ax, ny = zy - ay;
    const double ddx = 1 - (ax * zx + ay * zy), ddy = -(ax * zy - ay * zx);
    const double dd = std::max(1e-6, ddx * ddx + ddy * ddy);
    zx = (nx * ddx + ny * ddy) / dd; zy = (ny * ddx - nx * ddy) / dd;
    const double seg = TAU / std::max(3.0, (double)f.sym), half = seg * 0.5, C = f.hypC, r2 = (double)f.hypR * f.hypR;
    for (int i = 0; i < 28; i++) {
      double a = std::atan2(zy, zx);
      a -= seg * std::floor(a / seg);
      a = half - sabs(a - half, 0.003);
      const double l = std::hypot(zx, zy);
      zx = l * std::cos(a); zy = l * std::sin(a);
      const double vx = zx - C, vy = zy;
      const double vv = vx * vx + vy * vy;
      if (vv >= r2) break;
      const double inv = r2 / std::max(vv, 1e-6);
      zx = C + vx * inv; zy = vy * inv;
    }
    const double domRadius = std::max((double)f.hypC - f.hypR, 0.15);
    spun(zx * 0.72 / domRadius, zy * 0.72 / domRadius);
    return;
  } else if (f.mode == 4) {
    const double L = f.drL;
    double wx = std::log(std::max(std::hypot(zx, zy), 1e-5)), wy = std::atan2(zy, zx);
    const double T = L / TAU;
    const double nx = wx + wy * T; wy = wy - wx * T; wx = nx;
    wx -= f.drPhase;
    const double phase = wx / L;
    const double frac = phase - std::floor(phase);
    const double sf = 0.5 - sabs(frac - 0.5, 0.04);
    wx = (sf * 2 - 1) * L * 0.55;
    const double e = std::exp(wx) * 0.85;
    spun(e * std::cos(wy), e * std::sin(wy));
    return;
  } else if (f.mode == 6) {
    // foldLens: trace the ray back to the sky, so ink lands under the finger.
    const double E = f.drL, r = std::max(std::hypot(zx, zy), 1e-5);
    const double b = r - E * E / r, wind = 0.9 * std::log(1 + 0.4 * E / std::max(r - 0.5 * E, 1e-3 * E));
    const double wc = std::cos(wind) * b / r, ws = std::sin(wind) * b / r;
    spun((wc * zx - ws * zy) / f.vz, (ws * zx + wc * zy) / f.vz);
    return;
  } else if (f.mode == 7) {   // foldWorm
    const double a = f.drL, r = std::max(std::hypot(zx, zy), 1e-5), u = r / a;
    const double b = u >= 1 ? std::sqrt(r * r - a * a) : -0.9 * a * u / std::sqrt(std::max(1 - u * u, 1e-4));
    spun(zx / r * b / f.vz, zy / r * b / f.vz);
    return;
  } else if (f.mode == 8) {   // foldPearls
    auto tri = [](double v) { v -= 1; return std::fabs(v - 4 * std::floor(v / 4) - 2) - 1; };
    zx += f.hypAx; zy += f.hypAy;
    for (int i = 0; i < 4; i++) {
      zx = tri(zx); zy = tri(zy);
      double k = f.tile / std::max(zx * zx + zy * zy, 1e-4);
      k = 0.5 * (k + 1 + std::sqrt((k - 1) * (k - 1) + 0.02));
      zx *= k; zy *= k;
    }
    spun(tri(zx) * 0.85, tri(zy) * 0.85);
    return;
  } else if (f.mode == 9) {   // foldPoles
    auto tri = [](double v) { v -= 1; return std::fabs(v - 4 * std::floor(v / 4) - 2) - 1; };
    const double t = f.drPhase;
    const double ax = 0.62 * std::cos(t), ay = 0.62 * std::sin(t), bx = 0.5 * std::cos(1 - 0.7 * t), by = 0.5 * std::sin(1 - 0.7 * t);
    const double z2x = zx * zx - zy * zy, z2y = 2 * zx * zy;
    const double nx = z2x - (ax * ax - ay * ay), ny = z2y - 2 * ax * ay;
    const double dx2 = z2x - (bx * bx - by * by), dy2 = z2y - 2 * bx * by, dd = std::max(dx2 * dx2 + dy2 * dy2, 1e-6);
    const double qx = (nx * dx2 + ny * dy2) / dd, qy = (ny * dx2 - nx * dy2) / dd;
    spun(tri((zx * qx - zy * qy) * 0.9) * 0.9, tri((zx * qy + zy * qx) * 0.9) * 0.9);
    return;
  } else if (f.mode >= 10) {   // the steered places: foldPool .. foldJulia
    auto tri = [](double v) { v -= 1; return std::fabs(v - 4 * std::floor(v / 4) - 2) - 1; };
    const double gx = (f.hypAx - f.cx) * f.vz, gy = (f.hypAy - f.cy) * f.vz;
    const double Fx = cs * gx - sn * gy, Fy = sn * gx + cs * gy;
    const double t = f.drPhase, A = f.tile;
    if (f.mode == 10) {
      const double ex = zx - Fx, ey = zy - Fy, r1 = std::hypot(ex, ey) + 1e-4, r0 = std::hypot(zx, zy) + 1e-4;
      const double w1 = std::sin(r1 * 22 - t * 8) * std::exp(-r1 * 1.6), w0 = 0.5 * std::sin(r0 * 16 - t * 6) * std::exp(-r0 * 1.2);
      const double k = 0.07 * A;
      spun((zx + (ex / r1 * w1 + zx / r0 * w0) * k) / f.vz, (zy + (ey / r1 * w1 + zy / r0 * w0) * k) / f.vz);
    } else if (f.mode == 11) {
      const double ex = zx - Fx, ey = zy - Fy;
      const double th = (3 * A + 0.4 * std::sin(t)) * std::exp(-(ex * ex + ey * ey) * 4), c = std::cos(th), s = std::sin(th);
      zx = Fx + c * ex - s * ey; zy = Fy + s * ex + c * ey;
      const double seg = TAU / f.sym, half = seg * 0.5, l = std::hypot(zx, zy);
      double a = std::atan2(zy, zx);
      a -= seg * std::floor(a / seg);
      if (f.mirror) a = half - sabs(a - half, 0.002);
      spun(l * std::cos(a) / f.vz, l * std::sin(a) / f.vz);
    } else if (f.mode == 12) {
      const double r = std::hypot(zx, zy) / 0.8;
      if (r >= 1) { spun(zx, zy); return; }
      double nx = zx / 0.8, ny = zy / 0.8, nz = std::sqrt(1 - r * r);
      const double la = Fy * 1.4, lo = Fx * 2.2 + t;
      const double y1 = std::cos(la) * ny - std::sin(la) * nz, z1 = std::sin(la) * ny + std::cos(la) * nz;
      const double x2 = std::cos(lo) * nx - std::sin(lo) * z1, z2 = std::sin(lo) * nx + std::cos(lo) * z1;
      spun(tri(std::atan2(x2, z2) / PI * 2) * 1.5, std::asin(clampd(y1, -1, 1)) / PI * 1.8);
    } else if (f.mode == 13) {
      const double ex = zx - 0.6 * Fx, ey = zy - 0.6 * Fy, r = std::hypot(ex, ey) + 1e-4;
      spun(tri(std::atan2(ey, ex) / PI * 4) * 1.3, tri(0.35 / r + t * 2) * 0.9);
    } else {
      zx *= 1.25; zy *= 1.25;
      for (int i = 0; i < 4; i++) {
        const double nx = zx * zx - zy * zy + Fx * 1.1;
        zy = 2 * zx * zy + Fy * 1.1; zx = nx;
        if (std::fabs(zx) + std::fabs(zy) > 1e6) break;
      }
      spun(tri(zx * 0.6) * 0.9, tri(zy * 0.6) * 0.9);
    }
    return;
  } else {
    const double t = f.tile;
    zx = zx * t + 1; zy = zy * t + 1;
    const double fx = zx - 2 * std::floor(zx * 0.5) - 1;
    const double fy = zy - 2 * std::floor(zy * 0.5) - 1;
    zx = sabs(fx, 0.02); zy = sabs(fy, 0.02);
    const double d = zx - zy, sd = sabs(d, 0.02);
    const double sx = 0.5 * (zx + zy + sd), sy = 0.5 * (zx + zy - sd);
    spun(sx * 1.4 - 0.7, sy * 1.4 - 0.7);
    return;
  }
  spun(zx / f.vz, zy / f.vz);
}

// ---------------- motion ----------------
// Hands are never perfectly still. `take` gives a grip some play: movement smaller
// than `play` is ignored outright, and only what exceeds it comes through.
double Motion::take(int key, double v, double play) {
  if (!has[key]) { has[key] = true; ref[key] = v; return 0; }
  const double d = v - ref[key];
  if (std::fabs(d) <= play) return 0;
  const double out = d - jssign(d) * play;
  ref[key] = v - jssign(d) * play;
  return out;
}

void Motion::update(double dt, FoldSys& fs) {
  if (!held) fs.addSpin(spinVel * dt);
  const double turn = pend * (1 - std::exp(-dt * 11));
  fs.addSpin(turn); pend -= turn;
  spinVel *= std::exp(-1.6 * dt);
  diveVel *= std::exp(-2.6 * dt);
  if (std::fabs(spinVel) < 1e-4) spinVel = 0;
  if (std::fabs(diveVel) < 1e-4) diveVel = 0;
}
