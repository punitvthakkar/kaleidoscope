// Web: Pens. Pointers (mouse, touch), autopilot pens, hand-tracked fingertips and
// spark particles. Each frame they emit glowing capsule segments into `data`.
#include "engine_impl.h"
#include <cstring>

static const int LISSAJOUS[8][2] = {{3, 2}, {5, 4}, {3, 4}, {5, 6}, {7, 4}, {2, 5}, {7, 6}, {4, 9}};
static const int ROSE[8] = {3, 5, 2, 7, 4, 6, 3, 5};

static void initPen(Pen& p, int kind, double offset) { p = Pen(); p.kind = kind; p.hue = p.offset = offset; }

// ---------------- input ----------------
void Pens::toP(double clientX, double clientY, double& ox, double& oy) const {
  const double w = viewW, h = viewH;
  const double x = (clientX - w / 2) / h * 2, y = -(clientY - h / 2) / h * 2;
  const double k = 1 + fs->f.fish * (x * x + y * y) * 0.3;
  ox = x * k; oy = y * k;
}

Pen* Pens::get(int id, bool mouse) {
  for (int i = 0; i < nPtr; i++) if (pointers[i].id == id) return &pointers[i].p;
  if (nPtr == MAX_PTR) return nullptr;
  Ptr& r = pointers[nPtr];
  r.id = id; initPen(r.p, mouse ? Pen::MOUSE : Pen::TOUCH, nPtr * 0.17);
  nPtr++;
  return &r.p;
}

Pens::Touch* Pens::touch(int id) {
  for (int i = 0; i < nTouch; i++) if (touches[i].id == id) return &touches[i];
  return nullptr;
}

void Pens::measure() {
  const Touch &a = touches[0], &b = touches[1];
  gdN = std::hypot(b.x - a.x, b.y - a.y);
  gaN = std::atan2(b.y - a.y, b.x - a.x);
}

void Pens::pointerDown(int id, bool mouse, double cx, double cy, double t) {
  Pen* p = get(id, mouse);
  if (!p) return;
  double x, y; toP(cx, cy, x, y);
  p->x = p->px = x; p->y = p->py = y;
  p->down = true; p->inside = true; p->has = false; p->qn = 0;
  p->t0 = t; p->sx = cx; p->sy = cy;
  if (!mouse) {
    Touch* tc = touch(id);
    if (tc) { tc->x = cx; tc->y = cy; }
    else if (nTouch < MAX_PTR) touches[nTouch++] = {id, cx, cy};
    if (nTouch == 2) { measure(); gd = gdN; ga = gaN; gt = t; gAng = gLog = 0; motion->drop(Motion::tA, Motion::tD); motion->held = true; motion->spinVel = 0; }
  }
}

void Pens::pointerMove(int id, bool mouse, double cx, double cy, double t) {
  Pen* p = get(id, mouse);
  if (!p) return;
  // Every sample since the last frame is queued, so fast strokes stay curved.
  const bool fresh = !p->inside;
  if (fresh) p->qn = 0;
  double x, y; toP(cx, cy, x, y);
  if (p->qn < 96) { p->q[p->qn++] = (float)x; p->q[p->qn++] = (float)y; }
  if (fresh) { p->px = p->q[0]; p->py = p->q[1]; p->has = false; }
  p->x = x; p->y = y; p->inside = true;

  Touch* tc = touch(id);
  if (!tc) return;
  tc->x = cx; tc->y = cy;
  if (nTouch != 2) return;
  // Two fingers hold the glass itself: twist turns it, pinch dives through it.
  measure();
  double da = gaN - ga;
  if (da > PI) da -= TAU; else if (da < -PI) da += TAU;
  const double secs = std::max(4.0, t - gt) / 1000;
  gAng += da;
  const double turn = motion->take(Motion::tA, gAng, 0.02);
  if (turn) {
    motion->used++;
    motion->feed(-turn);
    motion->spinVel += (clampd(-turn / secs, -3, 3) - motion->spinVel) * 0.2;
  }
  if (gd > 8 && gdN > 8) {
    gLog += std::log(gdN / gd);
    const double push = motion->take(Motion::tD, gLog, 0.015);
    if (push) motion->dive(push * 7);
  }
  gd = gdN; ga = gaN; gt = t;
}

void Pens::pointerUp(int id, bool mouse, double cx, double cy, double t, bool cancel) {
  for (int i = 0; i < nTouch; i++) if (touches[i].id == id) {
    memmove(&touches[i], &touches[i + 1], (nTouch - i - 1) * sizeof(Touch)); nTouch--;
    if (nTouch < 2) motion->held = false;
    break;
  }
  int pi = -1;
  for (int i = 0; i < nPtr; i++) if (pointers[i].id == id) { pi = i; break; }
  if (pi < 0) return;
  Pen& p = pointers[pi].p;
  const bool still = std::hypot(cx - p.sx, cy - p.sy) < 10;
  if (p.down && !cancel && still && t - p.t0 < 320 && nTouch == 0 && tapN <= 93) { tapQ[tapN++] = (float)p.x; tapQ[tapN++] = (float)p.y; tapQ[tapN++] = 1; }
  p.down = false;
  if (!mouse) { memmove(&pointers[pi], &pointers[pi + 1], (nPtr - pi - 1) * sizeof(Ptr)); nPtr--; }
}

void Pens::pointerLeave(int id) {
  for (int i = 0; i < nPtr; i++) if (pointers[i].id == id) { pointers[i].p.inside = false; pointers[i].p.has = false; }
}

void Pens::wheel(double dx, double dy, bool shift) {
  if (shift && !dx) { dx = dy; dy = 0; }
  if (std::fabs(dy) >= std::fabs(dx)) motion->dive(-dy * 0.004); else motion->spin(dx * 0.004);
}

// Trackpad twist and pinch, as if two fingers had just landed on the glass.
void Pens::gestureBegin(double t) {
  gt = t; gAng = gLog = 0;
  motion->drop(Motion::tA, Motion::tD); motion->held = true; motion->spinVel = 0;
}

void Pens::gestureTwist(double da, double t) {
  const double secs = std::max(4.0, t - gt) / 1000;
  gAng += da;
  const double turn = motion->take(Motion::tA, gAng, 0.02);
  if (turn) {
    motion->used++;
    motion->feed(-turn);
    motion->spinVel += (clampd(-turn / secs, -3, 3) - motion->spinVel) * 0.2;
  }
  gt = t;
}

void Pens::gesturePinch(double m, double t) {
  gLog += std::log(std::max(1 + m, 0.01));
  const double push = motion->take(Motion::tD, gLog, 0.015);
  if (push) motion->dive(push * 7);
  gt = t;
}

void Pens::gestureEnd() { if (nTouch < 2) motion->held = false; }

// ---------------- drawing ----------------
void Pens::push(double ax, double ay, double bx, double by, double rad) {
  if (n >= MAX_SEG) return;
  float* o = data + n * 8;
  o[0] = (float)ax; o[1] = (float)ay; o[2] = (float)bx; o[3] = (float)by;
  o[4] = col[0]; o[5] = col[1]; o[6] = col[2];
  o[7] = (float)rad;
  n++;
}

double Pens::foldScale(double x, double y) {
  const double e = 1e-3;
  double f[2];
  fs->apply(x, y, f); const double fx = f[0], fy = f[1];
  fs->apply(x + e, y, f); const double sx = std::hypot(f[0] - fx, f[1] - fy);
  fs->apply(x, y + e, f); const double sy = std::hypot(f[0] - fx, f[1] - fy);
  return clampd(std::min(sx, sy) / e, 0.2, 2.5);
}

void Pens::carried(double x, double y) {
  const Fold& fd = fs->f;
  const double dx = x - fd.cx, dy = y - fd.cy, z = fr->fbZoom;
  const double a = -(fr->fbRot + fr->fbSwirl * std::hypot(dx, dy) * z);
  const double c = std::cos(a) * z, s = std::sin(a) * z;
  ux = fd.cx + c * dx - s * dy;
  uy = fd.cy + s * dx + c * dy;
}

void Pens::stroke(double ax, double ay, double bx, double by, double rad) {
  const Fold& fd = fs->f;
  const double len = std::hypot(bx - ax, by - ay);
  // In unwarped Mandala/None modes, the ink shader instances all dihedral reflections on the GPU.
  if (fd.mode == 0 || (fd.mode == 1 && fd.warpAmt <= 0)) {
    if (len < 1e-5) { push(ax, ay, ax, ay, rad); return; }
    carried(ax, ay);
    push(ux, uy, bx, by, rad);
    return;
  }
  const double js = foldScale((ax + bx) / 2, (ay + by) / 2);
  double f[2];
  fs->apply(ax, ay, f);
  double x0 = f[0], y0 = f[1];
  const double w = rad * js * (fd.mode == 3 ? 0.65 : 0.85);
  if (len < 1e-5) { push(x0, y0, x0, y0, w); return; }
  carried(x0, y0);
  x0 = ux; y0 = uy;
  const int steps = (int)std::min(24.0, std::ceil(len / 0.012));
  for (int i = 1; i <= steps; i++) {
    const double t = (double)i / steps;
    fs->apply(ax + (bx - ax) * t, ay + (by - ay) * t, f);
    const double x1 = f[0], y1 = f[1];
    if (std::hypot(x1 - x0, y1 - y0) < (len / steps) * js * 3.5 + 0.04) push(x0, y0, x1, y1, w);
    x0 = x1; y0 = y1;
  }
}

bool Pens::spawn(double x, double y, double vx, double vy, double life) {
  if (pCount >= PMAX) return false;
  float* o = parts + pCount * 8;
  o[0] = (float)x; o[1] = (float)y; o[2] = (float)vx; o[3] = (float)vy;
  o[4] = (float)life;
  o[5] = (float)(lastT + rng->next() * 0.15);
  double f[2]; fs->apply(x, y, f);
  o[6] = (float)f[0]; o[7] = (float)f[1];
  pCount++;
  return true;
}

void Pens::emit(Pen& p, double d) {
  emitAcc += dt * 60 + d * 300;
  while (emitAcc >= 1) {
    emitAcc -= 1;
    const double a = rng->next() * TAU, sp = 0.15 + rng->next() * 0.6;
    if (!spawn(p.x, p.y, p.vx * 0.25 + std::cos(a) * sp, p.vy * 0.25 + std::sin(a) * sp, 0.6 + rng->next())) { emitAcc = 0; break; }
  }
}

void Pens::pluck(double x, double y, double v) {
  events->push({EV_PLUCK, (float)x, (float)y, (float)v, 0, 0});
}

// A tap drops a stone in the glass: a ring of light, a scatter of sparks and
// a wave that pushes the echo outward through every mirror. `power` scales it (a hand's
// punch or a snapped bridge is several taps strong, and makes a wave even in places
// whose own ripple is off); negative power pulls the wave and the sparks inward.
void Pens::burst(double x, double y, double power) {
  const double mag = std::fabs(power);
  const double rp = P->ripple > 0 ? P->ripple : (mag > 1.01 || power < 0 ? 0.6 : 0);
  if (rp > 0) {
    double f[2]; fs->apply(x, y, f);
    if (ripN == 4) { memmove(rip, rip + 4, 12 * sizeof(float)); memmove(ripAge, ripAge + 1, 3 * sizeof(float)); memmove(ripPow, ripPow + 1, 3 * sizeof(float)); ripN = 3; }
    rip[ripN * 4] = (float)f[0]; rip[ripN * 4 + 1] = (float)f[1]; ripAge[ripN] = 0; ripPow[ripN] = (float)(rp * power); ripN++;
  }
  taps++; tapX = x; tapY = y;
  lastT += 0.07;
  palette_write(*pal, lastT + palPhase, col, P->inkBright);
  const double R = 0.03 * P->inkSize * std::sqrt(mag); const int N = 14;
  double ax = x + R, ay = y;
  for (int i = 1; i <= N; i++) {
    const double a = (double)i / N * TAU, bx = x + R * std::cos(a), by = y + R * std::sin(a);
    stroke(ax, ay, bx, by, 0.004 * P->inkSize * std::min(2.0, std::sqrt(mag)));
    ax = bx; ay = by;
  }
  const int sparks = (int)(18 * std::min(3.0, mag));
  for (int i = 0; i < sparks; i++) {
    const double a = rng->next() * TAU, sp = (0.35 + rng->next() * 0.7) * std::min(2.0, std::sqrt(mag));
    if (power < 0) spawn(x + std::cos(a) * R * 3, y + std::sin(a) * R * 3, -std::cos(a) * sp, -std::sin(a) * sp, 0.3 + rng->next() * 0.4);
    else spawn(x, y, std::cos(a) * sp, std::sin(a) * sp, 0.4 + rng->next() * 0.7);
  }
  pluck(x, y, std::min(1.0, 0.5 * std::sqrt(mag)));
  events->push({EV_HAPTIC, 0, 0, 0, mag >= 2 ? HAPTIC_CONFIRM : HAPTIC_TICK, 0});
}

void Pens::toView(double u, double v, double& x, double& y) const {
  x = (u - 0.5) * 2 * (viewW / viewH); y = (0.5 - v) * 2;
  const double fk = 1 + fs->f.fish * (x * x + y * y) * 0.3;
  x *= fk; y *= fk;
}

void Pens::airTap(double u, double v, double power) {
  if (tapN > 93) return;
  double x, y; toView(u, v, x, y);
  tapQ[tapN++] = (float)x; tapQ[tapN++] = (float)y; tapQ[tapN++] = (float)power;
}

void Pens::setStir(int hand, bool on, double u, double v, double vu, double vv) {
  Stir& s = stirs[hand & 1];
  if (!on) { s.on = false; return; }
  if (s.on) { s.vu += (vu - s.vu) * 0.5; s.vv += (vv - s.vv) * 0.5; } else { s.vu = vu; s.vv = vv; }
  s.on = true; s.u = u; s.v = v; s.at = clock;
}

// The hand stirs the echoes like a hand drawn through water: a drag (as wet paint's)
// at its palm, every frame, carried on between the tracker's samples and fading if they stop.
void Pens::stir(Stir& s) {
  if (!s.on) return;
  const double age = clock - s.at;
  if (age > 0.25) { s.on = false; return; }
  const double fade = age > 0.1 ? 1 - (age - 0.1) / 0.15 : 1;
  const double u = s.u + s.vu * std::min(age, 0.1), v = s.v + s.vv * std::min(age, 0.1);
  double x0, y0, x1, y1;
  toView(u, v, x0, y0); toView(u + s.vu * dt, v + s.vv * dt, x1, y1);
  if (ripN + combN >= 4) return;
  double a[2], b[2]; fs->apply(x0, y0, a); fs->apply(x1, y1, b);
  const double dx = b[0] - a[0], dy = b[1] - a[1], d = std::hypot(dx, dy);
  if (d < 1e-5 || d > 0.3) return;
  float* o = rip + (ripN + combN++) * 4;
  o[0] = (float)b[0]; o[1] = (float)b[1]; o[2] = (float)-(std::atan2(dy, dx) + 4); o[3] = (float)(d * 0.85 * fade);
}

// Wet paint (fbComb): a moving finger drags the picture with it, for good. Each drag
// takes a free ripple slot as (x, y, -(direction + 4), distance): the negative third
// value is what tells the feedback shader it is a drag and not a ripple.
void Pens::comb(Pen& p, bool active) {
  if (active && p.cb && P->fbComb > 0 && ripN + combN < 4) {
    double a[2], b[2];
    fs->apply(p.cbx, p.cby, a);
    fs->apply(p.x, p.y, b);
    const double dx = b[0] - a[0], dy = b[1] - a[1], d = std::hypot(dx, dy);
    if (d > 1e-5 && d < 0.3) {
      float* o = rip + (ripN + combN++) * 4;
      o[0] = (float)b[0]; o[1] = (float)b[1];
      o[2] = (float)-(std::atan2(dy, dx) + 4); o[3] = (float)(d * P->fbComb);
    }
  }
  p.cbx = p.x; p.cby = p.y; p.cb = active;
}

void Pens::updateRipples() {
  const double push0 = 0.011 * dt * 60;
  for (int i = 0; i < ripN; i++) {
    ripAge[i] += (float)dt;
    if (ripAge[i] > 3) {
      memmove(rip + i * 4, rip + (i + 1) * 4, (16 - (i + 1) * 4) * sizeof(float));
      memmove(ripAge + i, ripAge + i + 1, (3 - i) * sizeof(float));
      memmove(ripPow + i, ripPow + i + 1, (3 - i) * sizeof(float));
      ripN--; i--;
      continue;
    }
    rip[i * 4 + 2] = 0.02f + ripAge[i] * 0.8f;
    rip[i * 4 + 3] = (float)(push0 * ripPow[i] * std::exp(-ripAge[i] * 1.2));
  }
}

void Pens::updateParticles(double time) {
  const Fold& fd = fs->f;
  const double rad = 0.0025 * P->inkSize, gain = P->inkBright * 1.2;
  const double drag = std::exp(-1.6 * dt);
  const bool gpuMirrors = fd.mode == 0 || (fd.mode == 1 && fd.warpAmt <= 0);
  for (int i = 0; i < pCount; i++) {
    float* o = parts + i * 8;
    o[4] -= (float)dt;
    if (o[4] <= 0) {
      pCount--;
      memcpy(o, parts + pCount * 8, 8 * sizeof(float));
      i--;
      continue;
    }
    const double x = o[0], y = o[1];
    const double cx = gnoise(x * 2 + time * 0.3, y * 2), cy = gnoise(x * 2 + 4.1, y * 2 - time * 0.3);
    o[2] = (float)(o[2] * drag + cy * dt * 2);
    o[3] = (float)(o[3] * drag - cx * dt * 2);
    const double nx = x + o[2] * dt, ny = y + o[3] * dt;
    o[0] = (float)nx; o[1] = (float)ny;
    const double life = std::min(1.0f, o[4]);
    palette_write(*pal, o[5] + palPhase, col, gain * life);
    if (gpuMirrors) {
      carried(x, y);
      push(ux, uy, nx, ny, rad);
    } else {
      double f[2]; fs->apply(nx, ny, f);
      carried(o[6], o[7]);
      const double fx = ux, fy = uy;
      const double dd = std::hypot(f[0] - fx, f[1] - fy), ds = std::hypot(nx - x, ny - y);
      const double s = ds > 1e-6 ? clampd(dd / ds, 0.2, 2.5) : 1;
      if (dd < 0.14) push(fx, fy, f[0], f[1], rad * s * 0.75);
      o[6] = (float)f[0]; o[7] = (float)f[1];
    }
  }
}

void Pens::autoPos(Pen& p, int i, int count) {
  const double t = autoT;
  switch (P->autoPath) {
    case PENPATH_lissajous: {
      const int a = LISSAJOUS[i % 8][0], b = LISSAJOUS[i % 8][1];
      p.x = 0.76 * std::sin(a * t * 0.3 + i); p.y = 0.76 * std::sin(b * t * 0.3 + i * 2.1);
      break;
    }
    case PENPATH_rose: {
      const double th = t * 0.5 + i * TAU / count, r = 0.22 + 0.62 * std::cos(ROSE[i % 8] * th);
      p.x = r * std::cos(th); p.y = r * std::sin(th);
      break;
    }
    case PENPATH_noise:
      p.x = 1.45 * gnoise(t * 0.35, i * 7.13 + 0.5);
      p.y = 1.45 * gnoise(i * 3.31 + 0.5, t * 0.35);
      break;
    default: {
      const double th = t * 0.8 + i * TAU / count, r = 0.18 + 0.62 * (0.5 + 0.5 * std::sin(t * 0.23 + i));
      p.x = r * std::cos(th); p.y = r * std::sin(th);
    }
  }
}

double Pens::colorT(Pen& p, double v) {
  switch (P->inkColor) {
    case INKCOLOR_cycle: p.hue += dt * (0.03 * fr->pace + v * 0.06); return p.hue;
    case INKCOLOR_speed: return std::min(v * 0.2, 1.2) + p.offset * 0.2;
    case INKCOLOR_angle: return std::atan2(p.y, p.x) / TAU + p.offset * 0.1;
    default: return p.offset;
  }
}

// Ten slots (two hands x five fingers), each off (finger at rest) or a [u, v] screen
// position. A slot keeps its pen, so every finger keeps its color.
void Pens::setHands(const bool on[10], const float tips[10][2]) {
  const double asp = viewW / viewH;
  for (int k = 0; k < 10; k++) {
    Pen& p = hands[k];
    if (!on || !on[k]) { p.down = false; continue; }
    double x = (tips[k][0] - 0.5) * 2 * asp, y = (0.5 - tips[k][1]) * 2;
    const double fk = 1 + fs->f.fish * (x * x + y * y) * 0.3;
    x *= fk; y *= fk;
    if (!p.down) {
      p.tx = p.x = p.rx = x; p.ty = p.y = p.ry = y; p.down = true; p.has = false;
      p.hvx = p.hvy = p.fvx = p.fvy = p.dvx = p.dvy = 0; p.sampleT = clock;
      continue;
    }
    // One Euro filter: steady when the finger is still (the tracker's noise is a few
    // thousandths of the screen), with almost no lag once it moves, because the cutoff
    // rises with its speed. How fast it moves, from sample to sample, smoothed: build()
    // carries the target forward by it between samples.
    const double age = std::max(clock - p.sampleT, 1.0 / 120);
    auto alpha = [&](double fc) { return 1 / (1 + 1 / (TAU * fc * age)); };
    const double ad = alpha(1.0);
    p.dvx += ((x - p.rx) / age - p.dvx) * ad; p.dvy += ((y - p.ry) / age - p.dvy) * ad;
    p.rx = x; p.ry = y;
    const double k1 = alpha(1.6 + 6.0 * std::hypot(p.dvx, p.dvy));
    const double nx = p.tx + (x - p.tx) * k1, ny = p.ty + (y - p.ty) * k1;
    p.hvx += ((nx - p.tx) / age - p.hvx) * 0.5; p.hvy += ((ny - p.ty) / age - p.hvy) * 0.5;
    p.tx = nx; p.ty = ny; p.sampleT = clock;
  }
}

int Pens::build(double _dt, double time, const Frame& frame, double phase) {
  dt = _dt; fr = &frame; palPhase = phase; n = 0;
  autoT += dt * P->autoSpeed * frame.pace;
  const int count = std::min((int)MAX_AUTO, (int)jsround(P->autoPens));
  while (nAuto < count) { initPen(autos[nAuto], Pen::AUTO, nAuto * 0.13); nAuto++; }
  for (int i = 0; i < nAuto; i++) {
    if (i < count) autoPos(autos[i], i, count);
    run(autos[i], i < count);
  }
  for (int k = 0; k < nPtr; k++) {
    Pen& p = pointers[k].p;
    const bool active = p.kind == Pen::MOUSE ? p.inside && (p.down || P->inkHover) : p.down;
    const int m = p.qn >> 1;
    if (active && m > 1) {
      const double lx = p.x, ly = p.y;
      dt = _dt / m;
      for (int j = 0; j < m; j++) { p.x = p.q[j * 2]; p.y = p.q[j * 2 + 1]; run(p, true); }
      dt = _dt; p.x = lx; p.y = ly;
    } else run(p, active);
    p.qn = 0;
  }
  // Hands: the camera sees a fingertip 10 to 30 times a second. Between its samples the
  // target is carried on at the fingertip's own speed (for at most a tenth of a second),
  // and the pen follows it on a critically damped spring, so its position and its speed
  // both change smoothly at the display's rate: no steps, no jerks, no overshoot.
  clock += dt;
  for (Pen& p : hands) {
    if (p.down) {
      const double ahead = std::min(clock - p.sampleT, 0.1) * 0.8;
      const double gx = p.tx + p.hvx * ahead, gy = p.ty + p.hvy * ahead, w = 24;
      const int steps = dt > 1.0 / 90 ? 2 : 1;
      const double h = dt / steps;
      for (int i = 0; i < steps; i++) {
        p.fvx += (w * w * (gx - p.x) - 2 * w * p.fvx) * h; p.fvy += (w * w * (gy - p.y) - 2 * w * p.fvy) * h;
        p.x += p.fvx * h; p.y += p.fvy * h;
      }
    }
    run(p, p.down);
  }
  for (int i = 0; i < tapN; i += 3) burst(tapQ[i], tapQ[i + 1], tapQ[i + 2]);
  tapN = 0;
  updateRipples();
  combN = 0;
  for (Stir& s : stirs) stir(s);
  for (int k = 0; k < nPtr; k++) comb(pointers[k].p, pointers[k].p.down);
  for (Pen& p : hands) comb(p, p.down);
  updateParticles(time);
  energy += (moved / std::max(dt, 1e-3) - energy) * std::min(1.0, dt * 5);
  moved = 0;
  return n;
}

void Pens::run(Pen& p, bool active) {
  if (active && p.has) lastT = colorT(p, p.v);
  drawPen(p, active);
}

void Pens::drawPen(Pen& p, bool active) {
  if (!active) { p.has = false; return; }
  if (!p.has) { p.px = p.x; p.py = p.y; p.has = true; p.v = 0; return; }
  const double dx = p.x - p.px, dy = p.y - p.py;
  const double d = std::hypot(dx, dy);
  const double inv = 1 / std::max(dt, 1e-3);
  p.v += (d * inv - p.v) * std::min(1.0, dt * 20);
  p.vx = dx * inv; p.vy = dy * inv;
  if (d > 0.6) { p.px = p.x; p.py = p.y; return; }
  // Movement is what the glass sings: wanderers murmur, hands ring out.
  const bool aut = p.kind == Pen::AUTO;
  if (!aut) { moved += d; painted += d; }
  p.acc += d;
  if (p.acc > (aut ? 0.9 : 0.32)) { p.acc = 0; pluck(p.x, p.y, aut ? 0.13 : 0.3); }
  const double rad = 0.006 * P->inkSize * (1 + P->inkSpeed * std::min(p.v, 8.0) * 0.25);
  palette_write(*pal, lastT + palPhase, col, P->inkBright);
  if (d > 1e-4) {
    if (P->brush == BRUSH_neon) stroke(p.px, p.py, p.x, p.y, rad);
    else if (P->brush == BRUSH_dots) stroke(p.x, p.y, p.x, p.y, rad * 1.8);
    else { stroke(p.px, p.py, p.x, p.y, rad * 0.35); emit(p, d); }
  }
  p.px = p.x; p.py = p.y;
}
