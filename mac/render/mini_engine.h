// The renderer tests' stand-in engine (Fold.update/apply, Main.step's frame values,
// auto pens): it stands in for core/ so a renderer can be tested alone. Used by
// mac/render/test_render.mm and Android's debug chambers, so both renderers get
// identical frames. The includer defines RENDERER_MAX_SEG (its renderer.h).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include "../core/state.h"
#include "../core/rmflight.h"
#include <cstdlib>

namespace mini { constexpr double TAU = M_PI * 2; }

struct MiniEngine {
  RmCam rmCam;
  Params P; System S; Frame f; Fold fold;
  double time = 0, kAnglePhase = 0, autoT = 0, lastT = 0;
  int frame = 0;
  struct Pen { double x = 0, y = 0, px = 0, py = 0, hue = 0, offset = 0, v = 0; bool has = false; };
  std::vector<Pen> autos; Pen hand;
  std::vector<float> ink = std::vector<float>(RENDERER_MAX_SEG * 8);
  int n = 0;
  float rip[16] = {}, ripAge[4] = {}; int ripN = 0;
  float col[3];
  bool script = true;          // scripted hand stroke and tap
  bool starsSeeded = false;
  bool stress = false;         // the sky places with every finger slot in use (GPU budget)
  double hx = 0, hy = 0;

  static double hashv(uint32_t x, uint32_t y, double& hy) {
    x = x * 1664525u + 1013904223u; y = y * 1664525u + 1013904223u;
    x += y * 1664525u; y += x * 1664525u; x ^= x >> 16; y ^= y >> 16;
    x += y * 1664525u; y += x * 1664525u;
    hy = (y ^ (y >> 16)) * (2 / 4294967295.0) - 1;
    return (x ^ (x >> 16)) * (2 / 4294967295.0) - 1;
  }
  static double grad(int ix, int iy, double fx, double fy) { double hy, hx = hashv((uint32_t)ix, (uint32_t)iy, hy); return hx * fx + hy * fy; }
  static double gnoise(double x, double y) {
    int ix = (int)floor(x), iy = (int)floor(y); double fx = x - ix, fy = y - iy;
    double ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    double a = grad(ix, iy, fx, fy), b = grad(ix + 1, iy, fx - 1, fy), c = grad(ix, iy + 1, fx, fy - 1), d = grad(ix + 1, iy + 1, fx - 1, fy - 1);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
  }
  static double sabs(double x, double k) { return hypot(x, k) - k; }

  void foldUpdate(double dt) {
    fold.mode = P.mode; fold.sym = fmax(2, round(P.symmetry)); fold.mirror = P.mirror;
    fold.spin += P.spinSpeed * M_PI / 180 * dt;
    fold.vz = P.viewZoom; fold.warpAmt = P.warpAmount; fold.warpScale = P.warpScale; fold.warpT += P.warpSpeed * dt;
    fold.kIters = P.kifsIters; fold.kScale = P.kifsScale;
    kAnglePhase += P.kifsAngleSpeed * dt;
    double swing = P.kifsAngleSpeed != 0 ? 14 * sin(kAnglePhase * 0.14) : 0;
    fold.kAngle = fmin(82, fmax(6, P.kifsAngle + swing)) * M_PI / 180;
    fold.kOffX = P.kifsOffX; fold.kOffY = P.kifsOffY;
    int hp = (int)fmax(3, fold.sym), q = (int)round(P.hypQ);
    while ((hp - 2) * (q - 2) <= 4) q++;
    double sp = sin(M_PI / hp), cq = cos(M_PI / q), r = sp / sqrt(fmax(1e-5, cq * cq - sp * sp));
    fold.hypC = sqrt(1 + r * r); fold.hypR = r;
    fold.hypAx = P.hypDrift * 0.52 * sin(time * 0.13); fold.hypAy = P.hypDrift * 0.52 * sin(time * 0.17 + 1);
    fold.drL = log(P.drosteScale); fold.drPhase += P.drosteSpeed * dt * fold.drL * 0.25;
    fold.tile = P.tileScale;
    if (fold.mode == 6 || fold.mode == 7) fold.drL = P.lensRing;
    if (fold.mode == 8) fold.tile = P.pearlScale;
    if (fold.mode >= 10) { fold.hypAx = 0.5 * sin(time * 0.21); fold.hypAy = 0.32 * sin(time * 0.29 + 2); fold.tile = P.placeAmount; }
  }

  void spun(double zx, double zy, double* o) {
    double c = cos(fold.spin), s = sin(fold.spin);
    o[0] = fold.cx + c * zx - s * zy; o[1] = fold.cy + s * zx + c * zy;
  }
  void apply(double x, double y, double* o) {
    const Fold& F = fold;
    if (F.warpAmt > 0) {
      double s = F.warpScale, t = F.warpT;
      double ox = gnoise(x * s + t, y * s), oy = gnoise(x * s + 5.2, y * s + 1.3 - t);
      double wx = (x + ox) * s * 2, wy = (y + oy) * s * 2;
      double o2x = gnoise(wx + 1.7, wy + 9.2 + t), o2y = gnoise(wx + 8.3 - t, wy + 2.8);
      x += F.warpAmt * (ox + 0.5 * o2x); y += F.warpAmt * (oy + 0.5 * o2y);
    }
    if (F.mode == 0) { o[0] = x; o[1] = y; return; }
    double cs = cos(-F.spin), sn = sin(-F.spin), dx = (x - F.cx) * F.vz, dy = (y - F.cy) * F.vz;
    double zx = cs * dx - sn * dy, zy = sn * dx + cs * dy;
    if (F.mode == 1) {
      double seg = mini::TAU / F.sym, half = seg * 0.5, a = atan2(zy, zx);
      a -= seg * floor(a / seg);
      if (F.mirror) a = half - sabs(a - half, 0.002);
      double l = hypot(zx, zy); zx = l * cos(a); zy = l * sin(a);
    } else if (F.mode == 2) {
      double s = F.kScale, iters = F.kIters; int maxI = (int)ceil(iters);
      zx = sabs(zx, 0.012); zy = sabs(zy, 0.012);
      double d0 = zx - zy, sd0 = sabs(d0, 0.012), tx0 = 0.5 * (zx + zy + sd0), ty0 = 0.5 * (zx + zy - sd0);
      zx = tx0; zy = ty0;
      for (int i = 0; i < maxI; i++) {
        double w = fmin(1, fmax(0, iters - i)); w = w * w * (3 - 2 * w);
        double k = 0.018 / (1 + 0.3 * i), fx = sabs(zx, k), fy = sabs(zy, k), d = fx - fy, sd = sabs(d, k);
        double sx = 0.5 * (fx + fy + sd), sy = 0.5 * (fx + fy - sd);
        fx = sx * s - F.kOffX * (s - 1); fy = sy * s - F.kOffY * (s - 1);
        double ang = F.kAngle / (1 + 0.45 * i), rc = cos(ang), rs = sin(ang);
        double rx = rc * fx - rs * fy, ry = rs * fx + rc * fy;
        zx += (rx - zx) * w; zy += (ry - zy) * w;
      }
      double norm = 1 / fmax(hypot(F.kOffX, F.kOffY) * 1.25, 0.55);
      return spun(zx * norm, zy * norm, o);
    } else if (F.mode == 3) {
      double r = hypot(zx, zy);
      if (r > 1e-6) { double rh = fmin(0.985, r / sqrt(1 + pow(fmax(r - 0.82, 0) * 3.5, 2))); zx *= rh / r; zy *= rh / r; }
      double ax = F.hypAx, ay = F.hypAy, nx = zx - ax, ny = zy - ay;
      double ddx = 1 - (ax * zx + ay * zy), ddy = -(ax * zy - ay * zx), dd = fmax(1e-6, ddx * ddx + ddy * ddy);
      zx = (nx * ddx + ny * ddy) / dd; zy = (ny * ddx - nx * ddy) / dd;
      double seg = mini::TAU / fmax(3, F.sym), half = seg * 0.5, C = F.hypC, r2 = F.hypR * F.hypR;
      for (int i = 0; i < 28; i++) {
        double a = atan2(zy, zx); a -= seg * floor(a / seg); a = half - sabs(a - half, 0.003);
        double l = hypot(zx, zy); zx = l * cos(a); zy = l * sin(a);
        double vx = zx - C, vy = zy, vv = vx * vx + vy * vy;
        if (vv >= r2) break;
        double inv = r2 / fmax(vv, 1e-6); zx = C + vx * inv; zy = vy * inv;
      }
      double dr = fmax(F.hypC - F.hypR, 0.15);
      return spun(zx * 0.72 / dr, zy * 0.72 / dr, o);
    } else if (F.mode == 4) {
      double L = F.drL, wx = log(fmax(hypot(zx, zy), 1e-5)), wy = atan2(zy, zx), T = L / mini::TAU;
      double nx = wx + wy * T; wy = wy - wx * T; wx = nx - F.drPhase;
      double ph = wx / L, fr = ph - floor(ph), sf = 0.5 - sabs(fr - 0.5, 0.04);
      wx = (sf * 2 - 1) * L * 0.55;
      double e = exp(wx) * 0.85;
      return spun(e * cos(wy), e * sin(wy), o);
    } else if (F.mode == 6) {
      double E = F.drL, r = fmax(hypot(zx, zy), 1e-5), b = r - E * E / r;
      double wind = 0.9 * log(1 + 0.4 * E / fmax(r - 0.5 * E, 1e-3 * E)), wc = cos(wind) * b / r, ws = sin(wind) * b / r;
      return spun((wc * zx - ws * zy) / F.vz, (ws * zx + wc * zy) / F.vz, o);
    } else {
      double t = F.tile; zx = zx * t + 1; zy = zy * t + 1;
      double fx = zx - 2 * floor(zx * 0.5) - 1, fy = zy - 2 * floor(zy * 0.5) - 1;
      zx = sabs(fx, 0.02); zy = sabs(fy, 0.02);
      double d = zx - zy, sd = sabs(d, 0.02), sx = 0.5 * (zx + zy + sd), sy = 0.5 * (zx + zy - sd);
      return spun(sx * 1.4 - 0.7, sy * 1.4 - 0.7, o);
    }
    spun(zx / F.vz, zy / F.vz, o);
  }

  void push(double ax, double ay, double bx, double by, double rad) {
    if (n >= RENDERER_MAX_SEG) return;
    float* d = &ink[n * 8];
    d[0] = ax; d[1] = ay; d[2] = bx; d[3] = by; d[4] = col[0]; d[5] = col[1]; d[6] = col[2]; d[7] = rad;
    n++;
  }
  void carried(double x, double y, double& ux, double& uy) {
    double dx = x - fold.cx, dy = y - fold.cy, z = f.fbZoom, a = -(f.fbRot + f.fbSwirl * hypot(dx, dy) * z);
    double c = cos(a) * z, s = sin(a) * z;
    ux = fold.cx + c * dx - s * dy; uy = fold.cy + s * dx + c * dy;
  }
  double foldScale(double x, double y) {
    const double e = 1e-3; double a[2], b[2], c[2];
    apply(x, y, a); apply(x + e, y, b); apply(x, y + e, c);
    return fmin(fmax(fmin(hypot(b[0] - a[0], b[1] - a[1]), hypot(c[0] - a[0], c[1] - a[1])) / e, 0.2), 2.5);
  }
  void stroke(double ax, double ay, double bx, double by, double rad) {
    double len = hypot(bx - ax, by - ay), ux, uy;
    if (fold.mode == 0 || (fold.mode == 1 && fold.warpAmt <= 0)) {
      if (len < 1e-5) { push(ax, ay, ax, ay, rad); return; }
      carried(ax, ay, ux, uy); push(ux, uy, bx, by, rad); return;
    }
    double js = foldScale((ax + bx) / 2, (ay + by) / 2), p[2];
    apply(ax, ay, p);
    double x0 = p[0], y0 = p[1], w = rad * js * (fold.mode == 3 ? 0.65 : 0.85);
    if (len < 1e-5) { push(x0, y0, x0, y0, w); return; }
    carried(x0, y0, ux, uy); x0 = ux; y0 = uy;
    int steps = (int)fmin(24, ceil(len / 0.012));
    for (int i = 1; i <= steps; i++) {
      double t = (double)i / steps;
      apply(ax + (bx - ax) * t, ay + (by - ay) * t, p);
      if (hypot(p[0] - x0, p[1] - y0) < (len / steps) * js * 3.5 + 0.04) push(x0, y0, p[0], p[1], w);
      x0 = p[0]; y0 = p[1];
    }
  }
  void palWrite(double t, double gain) {
    for (int j = 0; j < 3; j++) col[j] = (f.pal.a[j] + f.pal.b[j] * cos(6.283185307 * (f.pal.c[j] * t + f.pal.d[j]))) * gain;
  }
  void drawPen(Pen& p, double dt, bool active) {
    if (!active) { p.has = false; return; }
    if (p.has) { p.hue += dt * (0.03 * f.pace + p.v * 0.06); lastT = p.hue; }
    if (!p.has) { p.px = p.x; p.py = p.y; p.has = true; p.v = 0; return; }
    double dx = p.x - p.px, dy = p.y - p.py, d = hypot(dx, dy), inv = 1 / fmax(dt, 1e-3);
    p.v += (d * inv - p.v) * fmin(1, dt * 20);
    if (d > 0.6) { p.px = p.x; p.py = p.y; return; }
    double rad = 0.006 * P.inkSize * (1 + P.inkSpeed * fmin(p.v, 8) * 0.25);
    palWrite(lastT + f.palPhase, P.inkBright);
    if (d > 1e-4) stroke(p.px, p.py, p.x, p.y, rad);
    p.px = p.x; p.py = p.y;
  }
  void burst(double x, double y) {
    double o[2]; apply(x, y, o);
    rip[ripN * 4] = o[0]; rip[ripN * 4 + 1] = o[1]; ripAge[ripN] = 0; ripN++;
    lastT += 0.07; palWrite(lastT + f.palPhase, P.inkBright);
    double R = 0.03 * P.inkSize, ax = x + R, ay = y;
    for (int i = 1; i <= 14; i++) { double a = i / 14.0 * mini::TAU, bx = x + R * cos(a), by = y + R * sin(a); stroke(ax, ay, bx, by, 0.004 * P.inkSize); ax = bx; ay = by; }
  }

  void init(const Params& p) {
    P = p;
    for (int j = 0; j < 3; j++) {   // palette already settled, as on a thumbnail
      f.pal.a[j] = PALETTES[P.palette][0][j]; f.pal.b[j] = PALETTES[P.palette][1][j];
      f.pal.c[j] = PALETTES[P.palette][2][j]; f.pal.d[j] = PALETTES[P.palette][3][j];
    }
  }

  // Main.step without sound, motion, tilt or breath guide.
  const Frame& step(Renderer* r, double dt) {
    const double nn = dt * 60, rad = M_PI / 180, pdt = dt * P.pace, pn = nn * P.pace;
    time += pdt; frame++;
    foldUpdate(pdt);
    renderer_fade_veil(r, dt);
    double breath = P.breath * sin(time * 0.37);
    fold.fish = P.fisheye + breath * 0.045;
    f.n = nn; f.dt = dt; f.pace = P.pace; f.decay = pow(P.fbDecay, nn); f.hue = P.fbHueShift * rad * pdt;
    f.fbRot = P.fbRotate * rad * pdt; f.fbZoom = pow(P.fbZoom, pn) * exp(breath * 0.1 * pdt);
    f.fbSwirl = P.fbSwirl * pn; f.fbWarp = P.fbWarp * pn; f.fbWarpT += pdt * 0.5;
    f.genT += pdt * P.genSpeed; f.rmZ += pdt * P.rmFly; f.rmT += pdt; f.palPhase += pdt * P.paletteSpeed;
    f.dream = fmin(0.5, P.fbDream * 0.05 * pn); f.dreamX = 0.3 * cos(time * 0.11); f.dreamY = 0.3 * sin(time * 0.143);
    f.sheenT += pdt * 0.21;
    // The maths places: cruise slowly over the surface (FX_RMZOOM: how far the camera has sunk).
    if (P.gen == GEN_fractal3d) {
      static const double rz = getenv("FX_RMZOOM") ? atof(getenv("FX_RMZOOM")) : 1.0;
      rm_frame(P.rmShape, f.rmT, P.rmMorph, P.rmDetail, P.rmSlice, 0.6 + f.rmT * 0.06 * rmCam.gs, 0.35, rz, 0.01, dt, rmCam, f);
    }

    // Pens.build: auto pens (neon brush, cycling colour) plus a scripted hand and tap.
    n = 0;
    autoT += dt * P.autoSpeed * f.pace;
    const int count = (int)round(P.autoPens);
    while ((int)autos.size() < count) { Pen p; p.hue = p.offset = autos.size() * 0.13; autos.push_back(p); }
    static const int LISS[8][2] = {{3, 2}, {5, 4}, {3, 4}, {5, 6}, {7, 4}, {2, 5}, {7, 6}, {4, 9}};
    static const int ROSE[8] = {3, 5, 2, 7, 4, 6, 3, 5};
    for (int i = 0; i < (int)autos.size(); i++) {
      Pen& p = autos[i]; const double t = autoT;
      if (P.autoPath == PENPATH_lissajous) { p.x = 0.76 * sin(LISS[i % 8][0] * t * 0.3 + i); p.y = 0.76 * sin(LISS[i % 8][1] * t * 0.3 + i * 2.1); }
      else if (P.autoPath == PENPATH_rose) { double th = t * 0.5 + i * mini::TAU / count, rr = 0.22 + 0.62 * cos(ROSE[i % 8] * th); p.x = rr * cos(th); p.y = rr * sin(th); }
      else if (P.autoPath == PENPATH_noise) { p.x = 1.45 * gnoise(t * 0.35, i * 7.13 + 0.5); p.y = 1.45 * gnoise(i * 3.31 + 0.5, t * 0.35); }
      else { double th = t * 0.8 + i * mini::TAU / count, rr = 0.18 + 0.62 * (0.5 + 0.5 * sin(t * 0.23 + i)); p.x = rr * cos(th); p.y = rr * sin(th); }
      drawPen(p, dt, i < count);
    }
    if (script) {
      const bool on = frame > 30 && frame < 250;
      const double t = frame / 60.0;
      hand.x = 0.75 * sin(t * 1.3); hand.y = 0.42 * sin(t * 2.6);
      drawPen(hand, dt, on);
      if (frame == 120) burst(0.3, -0.2);
    }
    const double push0 = P.ripple * 0.011 * dt * 60;
    for (int i = 0; i < ripN; i++) {
      ripAge[i] += dt;
      if (ripAge[i] > 3) { for (int k = i; k < 3; k++) { for (int j = 0; j < 4; j++) rip[k * 4 + j] = rip[k * 4 + 4 + j]; ripAge[k] = ripAge[k + 1]; } ripN--; i--; continue; }
      rip[i * 4 + 2] = 0.02 + ripAge[i] * 0.8; rip[i * 4 + 3] = push0 * exp(-ripAge[i] * 1.2);
    }
    f.inkCount = n; f.ink = ink.data(); f.rip = rip; f.ripN = ripN;
    const bool gpuMirrors = fold.mode == 0 || (fold.mode == 1 && fold.warpAmt <= 0);
    f.inkCopies = gpuMirrors ? (fold.mirror ? 2 * (int)fold.sym : (int)fold.sym) : 1;
    f.fold = fold; f.P = P; f.sys = S;
    // The sky's cores, as the engine makes them: the place's own, and the scripted hand,
    // folded into the chamber's wedge.
    f.starCores = f.starAuto = 0; f.starBlast[2] = 0;
    if (P.stars > 0) {
      star_own_cores(f, P.starKind, time);
      f.starView[0] = 1; f.starView[1] = (float)cos(fold.spin); f.starView[2] = (float)sin(fold.spin);
      f.starView[3] = fold.cx; f.starView[4] = fold.cy;
      // A finger sweeping a wide loop (held from frame 60, so its pull has built up by
      // the end), and a tap that bursts. FX_FINGER=0 leaves the sky to itself; FX_HOVER=1
      // makes the finger a cursor that only hovers.
      const char* fe = getenv("FX_FINGER");
      const bool hover = getenv("FX_HOVER") && getenv("FX_HOVER")[0] == '1';
      const double t = frame / 60.0;
      const bool still = P.starKind == 4;   // to gather a star a hand holds still
      const double fx = still ? -0.45 : 0.8 * sin(t * 0.9 + 1.0), fy = still ? 0.15 : 0.45 * sin(t * 1.5);
      const bool nova = P.starKind == 4;   // the supernova's finger lets go near the end, as a hand would
      // Into the sky's open plane: the chamber's spin taken out.
      const double c = cos(fold.spin), sn = sin(fold.spin);
      auto toSky = [&](double x, double y, double& ox, double& oy) { ox = c * x + sn * y; oy = -sn * x + c * y; };
      double sx, sy, svx, svy;
      toSky(fx, fy, sx, sy);
      toSky(still ? 0 : 0.72 * cos(t * 0.9 + 1.0), still ? 0 : 0.675 * cos(t * 1.5), svx, svy);
      if (script && frame > 60 && !(nova && frame > 250) && !(fe && fe[0] == '0'))
        star_add_finger(f, fold.sym, fold.mirror, sx, sy, svx, svy, star_finger_mass(P.starKind) * (float)fmin(1.0, 0.45 + (t - 1) * 0.37), hover);
      if (script && frame > 60 && !(fe && fe[0] == '0')) { f.starLook[0] = (float)fx; f.starLook[1] = (float)fy; }   // parallax: where the hand is
      // Stress: every core slot filled, seven more fingers (pressed and hovering by turns)
      // sweeping circles, for the GPU budget.
      if (stress) for (int k = 0; f.starCores < 8; k++) {
        const double ang = t * (0.7 + 0.1 * k) + k * 0.9, rad = 0.25 + 0.09 * k;
        double kx, ky, kvx, kvy;
        toSky(rad * cos(ang), rad * sin(ang), kx, ky);
        toSky(-rad * sin(ang) * (0.7 + 0.1 * k), rad * cos(ang) * (0.7 + 0.1 * k), kvx, kvy);
        star_add_finger(f, fold.sym, fold.mirror, kx, ky, kvx, kvy, star_finger_mass(P.starKind), k & 1);
      }
      // What the engine does with that finger: it aims the pulsar's beams (and their turning
      // is how fast the finger circles) and the quasar's jets.
      if (script && frame > 60 && (P.starKind == 2 || P.starKind == 3)) {
        const double a1 = atan2(sy, sx), t0 = t - 1 / 60.0, a0 = atan2(0.45 * sin(t0 * 1.5), 0.8 * sin(t0 * 0.9 + 1.0)) - fold.spin;
        double da = a1 - a0; da -= mini::TAU * floor(da / mini::TAU + 0.5);
        f.starCore[3] = (float)a1;
        if (P.starKind == 2) { f.starVel[0] = (float)fmin(1.6, fmax(0.5, hypot(sx, sy) / 0.6)); f.starVel[1] = (float)fmax(-16.0, fmin(16.0, da * 60)); }
      }
      auto blast = [&](double x, double y, float k) { double vx = 0, vy = 0; toSky(x, y, x, y); star_fold(fold.sym, fold.mirror, x, y, vx, vy); f.starBlast[0] = (float)x; f.starBlast[1] = (float)y; f.starBlast[2] = k; };
      if (nova && frame == 251) blast(fx, fy, 3.0f);
      else if (!nova && frame == 255 && !getenv("FX_NOTAP")) blast(-0.5, 0.25, 1.0f);   // FX_NOTAP=1: no tap
      if (getenv("FX_ZOOM")) f.starView[0] = (float)atof(getenv("FX_ZOOM"));
      if (P.starKind == 4) f.starLensN = 0;   // as the engine: the supernova's finger is the star it gathers
      f.starsReset = !starsSeeded; starsSeeded = true;
    }
    return f;
  }
};
