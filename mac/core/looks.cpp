// Hand looks: the tracked hands drawn in light (index.html, Looks; keep the two in step).
// Each frame this builds batches the renderers draw on top of the picture and, where the
// chamber mirrors, a little into its echoes. Picture space: y up, height spans -1..1.
#include "engine_impl.h"

namespace {

const float HUES[5] = {0.62f, 0.92f, 0.05f, 0.42f, 0.78f};
const float FEED[6] = {0, 0.006f, 0.006f, 0.0035f, 0.004f, 0};   // the echoes keep light for seconds: a little goes a long way
const int CHAINS[6][5] = {{0, 1, 2, 3, 4}, {0, 5, 6, 7, 8}, {9, 10, 11, 12, -1}, {13, 14, 15, 16, -1}, {0, 17, 18, 19, 20}, {5, 9, 13, 17, -1}};
const int BONES[42] = {0, 1, 1, 2, 2, 3, 3, 4, 0, 5, 5, 6, 6, 7, 7, 8, 5, 9, 9, 10, 10, 11, 11, 12, 9, 13, 13, 14, 14, 15, 15, 16, 13, 17, 17, 18, 18, 19, 19, 20, 0, 17};
const int TIPS[5] = {4, 8, 12, 16, 20};

struct RGB { float r, g, b; };
RGB rgb(double h) { return {(float)(0.5 + 0.5 * std::cos(TAU * h)), (float)(0.5 + 0.5 * std::cos(TAU * (h + 0.33))), (float)(0.5 + 0.5 * std::cos(TAU * (h + 0.67)))}; }
double cr(double p0, double p1, double p2, double p3, double u) {
  const double u2 = u * u, u3 = u2 * u;
  return 0.5 * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
}

}  // namespace

void Looks::addSeg(float ax, float ay, float bx, float by, float w, RGBf c, float alpha, float sharp, float glow) {
  LookData& d = out;
  if (d.nSeg >= LOOK_MAX_SEG || alpha <= 0) return;
  float* s = d.seg + d.nSeg++ * 12;
  s[0] = ax; s[1] = ay; s[2] = bx; s[3] = by; s[4] = c.r; s[5] = c.g; s[6] = c.b; s[7] = w;
  s[8] = alpha; s[9] = sharp; s[10] = glow; s[11] = 0;
}

void Looks::addV(float x, float y, float a, float b, float c, float e) {
  LookData& d = out;
  if (d.nVert >= LOOK_MAX_VERT) return;
  float* v = d.vert + d.nVert++ * 6;
  v[0] = x; v[1] = y; v[2] = a; v[3] = b; v[4] = c; v[5] = e;
}

void Looks::strip(int first, int kind) {
  LookData& d = out;
  if (d.nStrip >= LOOK_MAX_STRIP || d.nVert - first < 4) return;
  d.stripFirst[d.nStrip] = first; d.stripCount[d.nStrip] = d.nVert - first; d.stripKind[d.nStrip] = kind; d.nStrip++;
}

void Looks::ring(const Hand& H, RGBf col, float A) {
  if (H.pinch < 0.25) return;
  const float* p = H.p;
  const double cx = (p[8] + p[16]) / 2, cy = (p[9] + p[17]) / 2, r = H.S * 0.16;
  for (int i = 0; i < 24; i++) {
    const double a0 = i / 24.0 * TAU, a1 = (i + 1) / 24.0 * TAU;
    addSeg((float)(cx + std::cos(a0) * r), (float)(cy + std::sin(a0) * r), (float)(cx + std::cos(a1) * r), (float)(cy + std::sin(a1) * r),
           (float)(H.S * 0.014), col, (float)(1.1 * H.pinch * A), 30, 0.3f);
  }
}

void Looks::constellation(Hand& H, int i, double t) {
  const float* p = H.p; const float S = (float)H.S, A = (float)H.a;
  for (int b = 0; b < 42; b += 2) addSeg(p[BONES[b] * 2], p[BONES[b] * 2 + 1], p[BONES[b + 1] * 2], p[BONES[b + 1] * 2 + 1], S * 0.04f, {1, 0.93f, 0.82f}, 0.55f * A, 60, 0.12f);
  for (int j = 0; j < 21; j++) {
    const float tw = (float)std::pow(0.5 + 0.5 * std::sin(t * 3.1 + j * 2.3 + i * 5), 3);
    addSeg(p[j * 2], p[j * 2 + 1], p[j * 2], p[j * 2 + 1], S * (0.024f + 0.03f * tw), {1, 0.95f, 0.85f}, (0.8f + 2.2f * tw) * A, 22, 0.35f);
  }
  for (int f = 0; f < 5; f++) { const int k = TIPS[f]; const RGB c = rgb(HUES[f]); addSeg(p[k * 2], p[k * 2 + 1], p[k * 2], p[k * 2 + 1], S * 0.083f, {c.r, c.g, c.b}, 1.9f * A, 22, 0.35f); }
  const float px = (p[0] + p[10] + p[18] + p[26] + p[34]) / 5, py = (p[1] + p[11] + p[19] + p[27] + p[35]) / 5;
  addSeg(px, py, px, py, S * 0.9f, {1, 0.85f, 0.6f}, 0.05f * A, 2, 0);
  ring(H, {1, 0.95f, 0.85f}, A);
}

void Looks::neon(Hand& H, int i, double t) {
  const float* p = H.p; const float S = (float)H.S, A = (float)H.a;
  for (int f = 0; f < 6; f++) {
    const RGB c0 = rgb(f == 5 ? 0.82 : HUES[f]);
    const RGBf col{0.15f + 0.85f * c0.r, 0.15f + 0.85f * c0.g, 0.15f + 0.85f * c0.b};
    const RGBf white{0.75f + 0.25f * col.r, 0.75f + 0.25f * col.g, 0.75f + 0.25f * col.b};
    const float flick = (float)(0.88 + 0.12 * std::sin(t * 41 + f * 7 + i * 3) * std::sin(t * 13 + f)) * A;
    for (int j = 1; j < 5 && CHAINS[f][j] >= 0; j++) {
      const int a = CHAINS[f][j - 1] * 2, b = CHAINS[f][j] * 2;
      addSeg(p[a], p[a + 1], p[b], p[b + 1], S * 0.12f, col, 0.32f * flick, 9, 0.6f);
      addSeg(p[a], p[a + 1], p[b], p[b + 1], S * 0.03f, white, 0.55f * flick, 7, 0);
    }
  }
  ring(H, {0.4f, 1.0f, 0.9f}, A);
}

void Looks::aurora(Hand& H, int i, double t) {
  const float* p = H.p; const double S = H.S; const float A = (float)H.a;
  for (int f = 0; f < 5; f++) {
    Trail& q = H.tr[f];
    const int k = TIPS[f];
    q.push(p[k * 2], p[k * 2 + 1], t);
    while (q.n && t - q.at(0)[2] > 1.3) q.drop();
    const int n = q.n;
    if (n < 2) continue;
    const int M = std::min(48, n), first = out.nVert;
    const double step = (n - 1) / (double)(M - 1);
    for (int j = 0; j < M; j++) {
      const double* e = q.at((int)std::lround(j * step));
      const double u = j / (double)(M - 1), age = (t - e[2]) / 1.3;
      // Behind the tip the curtain always spreads a little, so a still hand still wears one.
      const double x = e[0] + S * 0.3 * (u - 1), y = e[1] + std::sin(u * 6 + t * 1.5 + f) * S * 0.03;
      const double L = S * (0.9 + 0.5 * std::sin(t * 0.7 + f * 1.9 + u * 3)) * (1 - age * 0.5);
      const float al = (float)((1 - age) * 0.9) * A;
      addV((float)x, (float)y, (float)u, 0, f / 4.f, al);
      addV((float)(x + L * 0.12), (float)(y - L), (float)u, 1, f / 4.f, al);
    }
    strip(first, 1);
  }
  for (int f = 0; f < 5; f++) { const int k = TIPS[f]; addSeg(p[k * 2], p[k * 2 + 1], p[k * 2], p[k * 2 + 1], (float)S * 0.05f, {0.5f, 1.0f, 0.7f}, 1.9f * A, 22, 0.35f); }
  ring(H, {0.5f, 1.0f, 0.7f}, A);
}

void Looks::ribbons(Hand& H, int i, double t, double dt) {
  const float* p = H.p; const double S = H.S; const float A = (float)H.a;
  const double L = S * 0.055 * 54 / (double)RN;
  if (!H.rbOn) {
    for (int f = 0; f < 5; f++) for (int j = 0; j < RN; j++) {
      Node& q = H.rb[f][j];
      q.x = q.px = p[TIPS[f] * 2]; q.y = q.py = p[TIPS[f] * 2 + 1] - j * L; q.tw = q.tv = 0;
    }
    H.rbOn = true;
  }
  const int steps = 2;
  const double sdt = std::min(dt, 1 / 30.0) / steps, drag = std::exp(-sdt * 1.6);
  for (int s = 0; s < steps; s++) for (int f = 0; f < 5; f++) {
    Node* ch = H.rb[f];
    ch[0].x = p[TIPS[f] * 2]; ch[0].y = p[TIPS[f] * 2 + 1];
    for (int j = 1; j < RN; j++) {
      Node& q = ch[j];
      const double vx = (q.x - q.px) * drag, vy = (q.y - q.py) * drag;
      q.px = q.x; q.py = q.y;
      const double wind = (std::sin(t * 0.8 + j * 0.16 + f * 1.3) * 2.4 + std::sin(t * 2.1 + j * 0.4) * 1.1) * S;
      q.x += vx + wind * sdt * sdt; q.y += vy - 1.54 * S * sdt * sdt;
    }
    for (int it = 0; it < 3; it++) for (int j = 1; j < RN; j++) {
      Node &a = ch[j - 1], &b = ch[j];
      const double dx = b.x - a.x, dy = b.y - a.y, d = std::max(1e-9, std::hypot(dx, dy)), k = (d - L) / d;
      if (j == 1) { b.x -= dx * k; b.y -= dy * k; } else { a.x += dx * k * 0.5; a.y += dy * k * 0.5; b.x -= dx * k * 0.5; b.y -= dy * k * 0.5; }
    }
    for (int j = 1; j < RN - 1; j++) {
      const Node &a = ch[j - 1], &c = ch[j + 1]; Node& b = ch[j];
      const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
      const double target = cross / (L * L) * 2.2 + std::sin(t * 1.3 + j * 0.34 + f) * 0.6;
      b.tv += ((target - b.tw) * 30 - b.tv * 5) * sdt; b.tw += b.tv * sdt;
    }
  }
  for (int f = 0; f < 5; f++) {
    const Node* ch = H.rb[f];
    const int first = out.nVert, M = (RN - 1) * 3;
    double lx = ch[0].x, ly = ch[0].y;
    for (int j = 0; j < M; j++) {
      const int jj = j / 3; const double u = (j % 3) / 3.0;
      const Node &a = ch[std::max(0, jj - 1)], &b = ch[jj], &c = ch[std::min(RN - 1, jj + 1)], &d = ch[std::min(RN - 1, jj + 2)];
      const double x = cr(a.x, b.x, c.x, d.x, u), y = cr(a.y, b.y, c.y, d.y, u), tw = b.tw + (c.tw - b.tw) * u;
      const double u2 = std::min(1.0, u + 1 / 3.0), nx = cr(a.x, b.x, c.x, d.x, u2), ny = cr(a.y, b.y, c.y, d.y, u2);
      double tx = nx - lx, ty = ny - ly; const double tl = std::max(1e-9, std::hypot(tx, ty)); tx /= tl; ty /= tl;
      lx = x; ly = y;
      const double along = j / (double)(M - 1), cs = std::cos(tw), w = S * 0.075 * (1 - along * 0.55) * (0.18 + 0.82 * std::fabs(cs));
      // The hue (fraction) and how present the hand is (whole 255ths), packed together.
      const double hue = HUES[f] + along * 0.15 + t * 0.02;
      const float hw = (float)(hue - std::floor(hue) + std::lround(A * 255));
      addV((float)(x - ty * w), (float)(y + tx * w), -1, (float)along, (float)cs, hw);
      addV((float)(x + ty * w), (float)(y - tx * w), 1, (float)along, (float)cs, hw);
    }
    strip(first, 0);
  }
  for (int f = 0; f < 5; f++) { const int k = TIPS[f]; const RGB c = rgb(HUES[f]); addSeg(p[k * 2], p[k * 2 + 1], p[k * 2], p[k * 2 + 1], (float)S * 0.04f, {c.r, c.g, c.b}, 0.8f * A, 22, 0.35f); }
}

void Looks::aura(Hand& H) {
  LookData& d = out;
  const int n = d.auraN++;
  const float* p = H.p;
  memcpy(d.auraPts + n * 42, p, sizeof(float) * 42);
  d.auraHand[n * 4] = (p[0] + p[10] + p[18] + p[26] + p[34]) / 5;
  d.auraHand[n * 4 + 1] = (p[1] + p[11] + p[19] + p[27] + p[35]) / 5;
  d.auraHand[n * 4 + 2] = (float)H.S; d.auraHand[n * 4 + 3] = (float)H.a;
  const float m = (float)H.S * 1.4f;
  for (int j = 0; j < 21; j++) {
    d.box[0] = std::min(d.box[0], p[j * 2] - m); d.box[1] = std::min(d.box[1], p[j * 2 + 1] - m);
    d.box[2] = std::max(d.box[2], p[j * 2] + m); d.box[3] = std::max(d.box[3], p[j * 2 + 1] + m);
  }
}

// Two pinched hands hold a bridge of light between their grips: a crackling arc that
// grows brighter, wider and wilder as it charges (Hands::bridge), whatever the look.
void Looks::bridge(const Hands& src, double aspect, double t, double dt) {
  const bool on = src.grab.mode == 2 && src.hs[0].seen && src.hs[1].seen;
  bridgeA += ((on ? 1.0 : 0.0) - bridgeA) * std::min(1.0, dt * (on ? 10 : 6));
  if (bridgeA < 0.01 || !on) return;
  const double c = 0.25 + src.bridge.charge, A = bridgeA;   // visible from the first moment, wilder as it charges
  const double ax = (2 * src.hs[0].gx - 1) * aspect, ay = 1 - 2 * src.hs[0].gy, bx = (2 * src.hs[1].gx - 1) * aspect, by = 1 - 2 * src.hs[1].gy;
  const double dx = bx - ax, dy = by - ay, L = std::max(1e-3, std::hypot(dx, dy)), nx = -dy / L, ny = dx / L;
  const int N = 28;
  for (int strand = 0; strand < 3; strand++) {
    const double amp = L * (0.05 + 0.06 * c) * (strand == 0 ? 1 : 0.6), ph = strand * 2.1;
    double px = ax, py = ay;
    for (int i = 1; i <= N; i++) {
      const double u = (double)i / N, env = std::sin(u * PI);
      // Crackle: a few drifting waves plus a flicker that changes every frame or so.
      const double off = env * amp * (std::sin(u * 9 + t * 13 + ph) * 0.5 + std::sin(u * 23 - t * 21 + ph) * 0.3
                                      + std::sin(u * 51 + std::floor(t * 40) * 1.7 + ph) * 0.2);
      const double x = ax + dx * u + nx * off, y = ay + dy * u + ny * off;
      const float a = (float)(A * (strand == 0 ? 1.0 : 0.5));
      addSeg((float)px, (float)py, (float)x, (float)y, (float)(0.03 + 0.03 * c), {0.55f, 0.45f, 1.0f}, a * (float)(0.25 + 0.25 * c), 8, 0.6f);
      addSeg((float)px, (float)py, (float)x, (float)y, (float)(0.006 + 0.004 * c), {0.9f, 0.92f, 1.0f}, a * (float)(0.9 + 0.8 * c), 10, 0);
      px = x; py = y;
    }
  }
  for (int g = 0; g < 2; g++) {
    const double gx = g ? bx : ax, gy = g ? by : ay, pulse = 0.5 + 0.5 * std::sin(t * 9 + g * 3);
    addSeg((float)gx, (float)gy, (float)gx, (float)gy, (float)(0.05 + 0.04 * c + 0.01 * pulse), {0.75f, 0.7f, 1.0f}, (float)(A * (1.2 + c)), 18, 0.4f);
  }
}

const LookData* Looks::build(int look, double dt, double time, double aspect, const Hands& src) {
  LookData& d = out;
  d.look = look; d.nSeg = d.nVert = d.nStrip = d.auraN = 0; d.time = (float)time;
  d.box[0] = d.box[1] = 1e9f; d.box[2] = d.box[3] = -1e9f;
  d.breath = (float)(0.5 - 0.5 * std::cos(time * TAU / 6));
  const double k = 1 - std::exp(-dt * 34);
  bool any = false;
  for (int i = 0; i < 2; i++) {
    Hand& H = hands[i];
    const auto& s = src.hs[i];
    if (s.seen) {
      for (int j = 0; j < 21; j++) {
        const double x = (2.0 * s.lx[j] - 1) * aspect, y = 1 - 2.0 * s.ly[j];
        if (!H.init) { H.p[j * 2] = (float)x; H.p[j * 2 + 1] = (float)y; }
        else { H.p[j * 2] += (float)((x - H.p[j * 2]) * k); H.p[j * 2 + 1] += (float)((y - H.p[j * 2 + 1]) * k); }
      }
      H.init = true;
      H.a = std::min(1.0, H.a + dt * 5);
      H.pinch += ((s.pinch ? 1 : 0) - H.pinch) * std::min(1.0, dt * 10);
    } else if ((H.a = std::max(0.0, H.a - dt * 3)) == 0) {
      H.forget();
    }
    if (H.a <= 0) continue;
    any = true;
    H.S = std::max(0.05, (double)std::hypot(H.p[18] - H.p[0], H.p[19] - H.p[1]));
    if (look == 1) constellation(H, i, time);
    else if (look == 2) neon(H, i, time);
    else if (look == 3) aurora(H, i, time);
    else if (look == 4) ribbons(H, i, time, dt);
    else if (look == 5) aura(H);
  }
  bridge(src, aspect, time, dt);
  // The bridge feeds the echoes too, even with no look shown.
  d.feed = std::max(look >= 0 && look < 6 ? FEED[look] : 0.f, bridgeA > 0.01 ? 0.006f : 0.f) * (float)std::min(1.0, dt * 60);
  return any || d.nSeg > 0 || d.nVert > 0 ? &d : nullptr;
}

void Looks::reset() { for (Hand& H : hands) { H.a = 0; H.forget(); } }
