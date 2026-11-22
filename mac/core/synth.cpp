// Sound, node for node with the web graph (index.html, `const Sound`):
//
//   drone: 2 triangles + 1 sine -> gains .6/.8/.9 -> filt (lowpass 260, Q .8) -> droneGain -> bus
//   notes: sine -> envelope -> stereo panner -> bus
//   bus -> out;  bus -> send (.5) -> [delay -> damp lowpass -> loop gain -> delay] x2 -> ret -> out
//   out (0.7) -> highpass 48 Q .7 -> pre (.5) -> wave shaper (CEILING) -> speakers
//   film: (shaper output + microphone) -> film fader
//
// Parameter changes follow AudioParam.setTargetAtTime: v(t) = target + (v0 - target) e^(-(t - T)/tau).
#include "synth.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr float LEVEL = 0.7f;
constexpr float DRONE = 0.13f;
constexpr int SCALE[5] = {0, 2, 4, 7, 9};
constexpr double RATIO[3] = {1, 1.498, 2.004};
constexpr float MIX[3] = {0.6f, 0.8f, 0.9f};
constexpr struct { double time, back, tone, wet; } LOOPS[2] = {{0.38, 0.4, 2200, 1}, {0.53, 0.32, 1500, 0.7}};
constexpr int VOICES = 12;
constexpr int TRI = 4096;
constexpr int MIC_RING = 1 << 15;
constexpr int FFT_MAX = 2048;

double rootOf(int chamber) { return 65.41 * std::pow(2.0, SCALE[((chamber % 5) + 5) % 5] / 12.0); }

struct Approach {
  double v = 0, target = 0, c = 0;
  void to(double t, double tau, double sr) { target = t; c = 1 - std::exp(-1 / (tau * sr)); }
  void hold(double x) { v = target = x; c = 0; }
  double next() { double y = v; v += (target - v) * c; return y; }
};

struct Biquad {
  BiquadCoefs k{1, 0, 0, 0, 0};
  double x1[2]{}, x2[2]{}, y1[2]{}, y2[2]{};
  void clear() { *this = Biquad{k}; }
  double run(int ch, double x) {
    double y = k.b0 * x + k.b1 * x1[ch] + k.b2 * x2[ch] - k.a1 * y1[ch] - k.a2 * y2[ch];
    x2[ch] = x1[ch]; x1[ch] = x; y2[ch] = y1[ch]; y1[ch] = y;
    return y;
  }
};

struct Delay {
  std::vector<float> buf[2];
  uint32_t mask = 0, w = 0;
  double d = 0;
  void make(double seconds, double sr) {
    uint32_t n = 1;
    while (n < sr * 1.0 + 4) n <<= 1;    // createDelay(1): up to one second
    for (auto& b : buf) b.assign(n, 0.f);
    mask = n - 1; w = 0; d = seconds * sr;
  }
  void clear() { for (auto& b : buf) std::fill(b.begin(), b.end(), 0.f); }
  float read(int ch) const {
    double p = w + (double)(mask + 1) - d;
    uint32_t i = (uint32_t)p; float f = (float)(p - i);
    const float* b = buf[ch].data();
    return b[i & mask] + (b[(i + 1) & mask] - b[i & mask]) * f;
  }
};

struct Voice {
  bool on = false;
  double phase = 0, inc = 0, g = 0, peak = 0;
  int64_t n = 0;
  float gl = 0, gr = 0;
};

// Control side: written from any thread, read by the audio thread at the top of each block.
struct Control {
  std::atomic<bool> on{false};
  std::atomic<int> chamber{0};
  std::atomic<uint64_t> pluck{0};
  std::atomic<uint32_t> pluckSeq{0};
  std::atomic<float> filtTarget{260}, droneTarget{DRONE};
  std::atomic<uint32_t> targetSeq{0};
  std::atomic<uint32_t> hush{0}, hushSeq{0};
  std::atomic<uint32_t> film{0}, filmSeq{0};
  std::atomic<bool> silent{true};
  std::atomic<bool> micOn{false};
  std::atomic<double> micRate{48000};
  std::atomic<uint64_t> micW{0};
  std::atomic<float> mic[MIC_RING];
} C;

// Audio-thread state.
struct Dsp {
  double sr = 0;
  bool ready = false, alive = false, on = false;
  int64_t offFor = 0;
  uint64_t now = 0;
  double lastPluck = -1e9;
  int chamber = 0;
  double root = 65.41;
  uint64_t pluckSeen = 0;
  uint32_t targetSeen = 0, hushSeen = 0, filmSeen = 0;
  Approach out, filtF, droneG, droneF[3];
  double dronePh[3]{}, filtLast = 0;
  Biquad filt, low, damp[2];
  Delay loops[2];
  Voice v[VOICES];
  int64_t attackN = 0, stopN = 0;
  double attackC = 0, decayK = 0;
  float tri[TRI + 1];
  float ceiling[1025];
  bool film = false, fading = false;
  int64_t filmN = 0, fadeN = 0;
  double fadeFrom = 0, filmGain = 0;
  bool micReading = false;
  double micPos = 0;
} D;

// Frame-loop state.
struct Listen {
  int n = 0;
  float win[FFT_MAX];
  double re[FFT_MAX], im[FFT_MAX], smooth[FFT_MAX / 2];
  float peak[3] = {0.15f, 0.15f, 0.15f};
  SynthLevels level{0, 0, 0};
  double since = 0;   // seconds since the drone targets were last set
} L;

void fresh(Dsp& d) {
  d.out.hold(0);
  d.filtF.hold(260);
  d.droneG.hold(DRONE);
  for (int k = 0; k < 3; k++) { d.droneF[k].hold(d.root * RATIO[k]); d.dronePh[k] = 0; }
  d.filt.k = synth_biquad(0, 260, 0.8, d.sr); d.filtLast = 260;
  d.filt.clear(); d.low.clear();
  for (auto& b : d.damp) b.clear();
  for (auto& l : d.loops) l.clear();
  for (auto& v : d.v) v.on = false;
}

void startVoice(Dsp& d, float x, float y, float vel) {
  double t = d.now / d.sr;
  int voices = 0;
  for (auto& v : d.v) voices += v.on;
  if (t - d.lastPluck < 0.09 || voices >= VOICES) return;
  d.lastPluck = t;
  int deg = (int)std::floor((std::atan2(y, x) / (2 * PI) + 0.5) * 5) % 5;
  int oct = 2 + std::min(2, (int)std::floor(std::hypot(x, y) / 0.38));
  for (auto& v : d.v) {
    if (v.on) continue;
    v = Voice{};
    v.on = true;
    v.inc = d.root * std::pow(2.0, oct + SCALE[deg] / 12.0) / d.sr;
    v.peak = vel * 0.22 * (oct == 2 ? 1 : oct == 3 ? 0.8 : 0.6);
    // StereoPannerNode, mono input: equal power over (pan + 1) / 2.
    double p = (std::clamp(x * 0.7, -0.85, 0.85) + 1) / 2;
    v.gl = (float)std::cos(p * PI / 2);
    v.gr = (float)std::sin(p * PI / 2);
    return;
  }
}

float unq(uint64_t q, float lo, float hi) { return lo + (float)(q & 0xffff) / 65535.f * (hi - lo); }
uint64_t q16(float v, float lo, float hi) { return (uint64_t)std::lrint((std::clamp(v, lo, hi) - lo) / (hi - lo) * 65535); }

void controls(Dsp& d) {
  bool want = C.on.load(std::memory_order_acquire);
  int chamber = C.chamber.load(std::memory_order_relaxed);
  if (chamber != d.chamber) {
    d.chamber = chamber;
    d.root = rootOf(chamber);
    if (d.alive) for (int k = 0; k < 3; k++) d.droneF[k].to(d.root * RATIO[k], 1.2, d.sr);
  }
  if (want && !d.on) {
    // A fresh graph, unless the last one is still fading out: then it simply comes back.
    if (!d.alive) fresh(d);
    d.out.to(LEVEL, 0.9, d.sr);
    d.on = d.alive = true;
  } else if (!want && d.on) {
    d.out.to(0, 0.25, d.sr);
    d.on = false;
    d.offFor = 0;
  }
  uint32_t ts = C.targetSeq.load(std::memory_order_acquire);
  if (ts != d.targetSeen) {
    d.targetSeen = ts;
    if (d.on) {
      d.filtF.to(C.filtTarget.load(std::memory_order_relaxed), 0.3, d.sr);
      d.droneG.to(C.droneTarget.load(std::memory_order_relaxed), 0.35, d.sr);
    }
  }
  uint32_t h = C.hush.load(std::memory_order_acquire);
  if (h != d.hushSeen) {
    d.hushSeen = h;
    if (d.on) {
      int mode = h & 3;
      if (mode == 0) d.out.to(LEVEL, 0.5, d.sr);
      else d.out.to(0, mode == 2 ? 0.012 : 0.1, d.sr);
    }
  }
  uint64_t p = C.pluck.load(std::memory_order_acquire);
  if (p != d.pluckSeen) {
    d.pluckSeen = p;
    if (d.on && d.alive) startVoice(d, unq(p >> 32, -4, 4), unq(p >> 16, -4, 4), unq(p, 0, 2));
  }
  uint32_t f = C.film.load(std::memory_order_acquire);
  if (f != d.filmSeen) {
    d.filmSeen = f;
    switch (f & 3) {
      case 1: d.film = true; d.fading = false; d.filmN = 0; d.filmGain = 0; break;
      case 2: if (d.film && !d.fading) { d.fading = true; d.fadeN = 0; d.fadeFrom = d.filmGain; } break;
      case 3: d.film = false; break;
    }
  }
}

inline float shape(const float* c, float x) {
  // WaveShaperNode, no oversampling: v = (N - 1) / 2 * (x + 1), linear between points.
  float v = 512.f * (x + 1);
  if (!(v > 0)) return c[0];
  if (v >= 1024) return c[1024];
  int k = (int)v; float f = v - k;
  return (1 - f) * c[k] + f * c[k + 1];
}

void renderSound(Dsp& d, float* outL, float* outR, int frames) {
  const double sr = d.sr;
  for (int i = 0; i < frames; i++) {
    double drone = 0;
    for (int k = 0; k < 3; k++) {
      double ph = d.dronePh[k], s;
      if (k == 2) s = std::sin(2 * PI * ph);
      else {
        double x = ph * TRI; int j = (int)x; double fr = x - j;
        s = d.tri[j] + (d.tri[j + 1] - d.tri[j]) * fr;
      }
      drone += s * MIX[k];
      ph += d.droneF[k].next() / sr;
      d.dronePh[k] = ph - std::floor(ph);
    }
    double ff = d.filtF.next();
    if (std::fabs(ff - d.filtLast) > 1e-6 * ff) { d.filt.k = synth_biquad(0, ff, 0.8, sr); d.filtLast = ff; }
    float dr = (float)(d.filt.run(0, drone) * d.droneG.next());
    float bl = dr, br = dr;   // mono into a stereo bus: both sides alike

    for (auto& v : d.v) {
      if (!v.on) continue;
      float s = (float)(std::sin(2 * PI * v.phase) * v.g);
      bl += s * v.gl; br += s * v.gr;
      v.phase += v.inc; v.phase -= std::floor(v.phase);
      // setTargetAtTime(peak, t, 0.012) until t + 0.05, then setTargetAtTime(0, t + 0.05, 0.36).
      if (v.n < d.attackN) v.g += (v.peak - v.g) * d.attackC;
      else v.g *= d.decayK;
      if (++v.n >= d.stopN) v.on = false;
    }

    float sendL = bl * 0.5f, sendR = br * 0.5f, retL = 0, retR = 0;
    for (int l = 0; l < 2; l++) {
      Delay& dl = d.loops[l];
      float yl = (float)d.damp[l].run(0, dl.read(0));
      float yr = (float)d.damp[l].run(1, dl.read(1));
      dl.buf[0][dl.w] = sendL + (float)LOOPS[l].back * yl;
      dl.buf[1][dl.w] = sendR + (float)LOOPS[l].back * yr;
      dl.w = (dl.w + 1) & dl.mask;
      retL += (float)LOOPS[l].wet * yl; retR += (float)LOOPS[l].wet * yr;
    }

    float g = (float)d.out.next();
    float ol = (float)d.low.run(0, (bl + retL) * g);
    float orr = (float)d.low.run(1, (br + retR) * g);
    outL[i] = shape(d.ceiling, ol * 0.5f);
    outR[i] = shape(d.ceiling, orr * 0.5f);
  }
}

void renderFilm(Dsp& d, const float* l, const float* r, float* fl, float* fr, int frames) {
  const double sr = d.sr;
  bool mic = C.micOn.load(std::memory_order_acquire);
  uint64_t w = C.micW.load(std::memory_order_acquire);
  double rate = C.micRate.load(std::memory_order_relaxed), ratio = 0;
  if (!mic) d.micReading = false;
  else {
    // The microphone runs on its own clock: read it about 30 ms behind, nudging the
    // read speed to hold that distance.
    double target = std::max(1024.0, 0.03 * rate);
    if (d.micReading && (w - d.micPos > MIC_RING / 2 || w - d.micPos < 2)) d.micReading = false;
    if (!d.micReading && w >= target) { d.micPos = w - target; d.micReading = true; }
    double err = std::clamp((w - d.micPos - target) / target, -1.0, 1.0);
    ratio = rate / sr * (1 + 0.002 * err);
  }
  for (int i = 0; i < frames; i++) {
    double t = d.filmN++ / sr;
    double g = t < 0.12 ? 0 : t < 0.5 ? (t - 0.12) / 0.38 : 1;
    if (d.fading) g = std::max(0.0, d.fadeFrom * (1 - d.fadeN++ / (0.3 * sr)));
    d.filmGain = g;
    float m = 0;
    if (d.micReading) {
      if (d.micPos + 1 >= (double)w) d.micReading = false;
      else {
        uint64_t j = (uint64_t)d.micPos; float f = (float)(d.micPos - j);
        float a = C.mic[j & (MIC_RING - 1)].load(std::memory_order_relaxed);
        float b = C.mic[(j + 1) & (MIC_RING - 1)].load(std::memory_order_relaxed);
        m = a + (b - a) * f;
        d.micPos += ratio;
      }
    }
    fl[i] = (l[i] + m) * (float)g;
    fr[i] = (r[i] + m) * (float)g;
  }
}

void fft(double* re, double* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double a = -2 * PI / len, wr = std::cos(a), wi = std::sin(a);
    for (int i = 0; i < n; i += len) {
      double cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int p = i + k, q = p + len / 2;
        double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
        re[q] = re[p] - tr; im[q] = im[p] - ti;
        re[p] += tr; im[p] += ti;
        double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}

// AnalyserNode.getByteFrequencyData (fftSize 512, smoothing 0.45, -100..-30 dB), then
// the web's band() averages. fftSize grows with the rate so bins keep their width.
void analyse(float out[3], float dt) {
  double rate = C.micRate.load(std::memory_order_relaxed);
  int s = rate >= 176400 ? 4 : rate >= 88200 ? 2 : 1, n = 512 * s;
  if (n != L.n) {
    L.n = n;
    for (int i = 0; i < n; i++)  // Blackman, alpha 0.16
      L.win[i] = (float)(0.42 - 0.5 * std::cos(2 * PI * i / n) + 0.08 * std::cos(4 * PI * i / n));
    std::fill(L.smooth, L.smooth + n / 2, 0.0);
  }
  uint64_t w = C.micW.load(std::memory_order_acquire);
  for (int i = 0; i < n; i++) {
    uint64_t j = w + MIC_RING - n + i;
    L.re[i] = C.mic[j & (MIC_RING - 1)].load(std::memory_order_relaxed) * L.win[i];
    L.im[i] = 0;
  }
  fft(L.re, L.im, n);
  const double keep = std::pow(0.45, dt * 60);   // the web's 0.45 per 60 Hz frame
  const int edges[4] = {1, 4, 24, 96};
  for (int b = 0; b < 3; b++) {
    int lo = edges[b] * s, hi = edges[b + 1] * s;
    double sum = 0;
    for (int k = lo; k < hi; k++) {
      double mag = std::hypot(L.re[k], L.im[k]) / n;
      double v = keep * L.smooth[k] + (1 - keep) * mag;
      if (!std::isfinite(v)) v = 0;
      L.smooth[k] = v;
      double db = 20 * std::log10(v);
      sum += std::clamp(std::floor(255 / 70.0 * (db + 100)), 0.0, 255.0);
    }
    out[b] = (float)(sum / ((hi - lo) * 255));
  }
}

void follow(float& level, int i, float v, float dt) {
  // Slow automatic gain so a quiet room and a loud one both use the full range.
  L.peak[i] = std::max({L.peak[i] * std::exp(-dt * 0.25f), v, 0.12f});
  float x = std::max(0.f, v / L.peak[i] - 0.25f) / 0.75f;
  level += (x - level) * std::min(1.f, dt * (x > level ? 30 : 5));
}

uint32_t next(std::atomic<uint32_t>& seq) { return seq.fetch_add(1, std::memory_order_acq_rel) + 1; }

}  // namespace

BiquadCoefs synth_biquad(int type, double frequency, double qDb, double sampleRate) {
  double f = std::clamp(frequency / (sampleRate / 2), 0.0, 1.0);
  if (f >= 1) return type == 0 ? BiquadCoefs{1, 0, 0, 0, 0} : BiquadCoefs{0, 0, 0, 0, 0};
  if (f <= 0) return type == 0 ? BiquadCoefs{0, 0, 0, 0, 0} : BiquadCoefs{1, 0, 0, 0, 0};
  double w0 = PI * f, c = std::cos(w0), alpha = std::sin(w0) / (2 * std::pow(10, qDb / 20));
  double a0 = 1 + alpha;
  double b0 = (type == 0 ? 1 - c : 1 + c) / 2, b1 = type == 0 ? 1 - c : -(1 + c);
  return {b0 / a0, b1 / a0, b0 / a0, -2 * c / a0, (1 - alpha) / a0};
}

void synth_create(double sr) {
  Dsp& d = D;
  d.sr = sr;
  // Band-limited triangle (the spec's Fourier series, normalized to peak 1), with
  // harmonics up to Nyquist for the highest drone triangle (the fifth over the top root).
  int harmonics = (int)(sr / 2 / (rootOf(4) * RATIO[1] * 1.01));
  float peak = 0;
  for (int i = 0; i < TRI; i++) {
    double x = 2 * PI * i / TRI, s = 0;
    for (int k = 1; k <= harmonics; k += 2) s += 8 * std::sin(k * PI / 2) / (PI * PI * k * k) * std::sin(k * x);
    d.tri[i] = (float)s;
    peak = std::max(peak, std::fabs(d.tri[i]));
  }
  for (int i = 0; i < TRI; i++) d.tri[i] /= peak;
  d.tri[TRI] = d.tri[0];
  // Input is halved before the shaper, so the curve covers levels up to twice full scale.
  for (int i = 0; i < 1025; i++) {
    double x = (i - 512) / 256.0, a = std::fabs(x);
    d.ceiling[i] = (float)((x > 0) - (x < 0)) * (float)(a <= 0.6 ? a : 0.6 + 0.38 * std::tanh((a - 0.6) / 0.38));
  }
  for (int l = 0; l < 2; l++) {
    d.loops[l].make(LOOPS[l].time, sr);
    d.damp[l].k = synth_biquad(0, LOOPS[l].tone, 1, sr);   // Q left at its default, 1
  }
  d.low.k = synth_biquad(1, 48, 0.7, sr);
  d.attackN = (int64_t)std::ceil(0.05 * sr);
  d.stopN = (int64_t)std::ceil(2.8 * sr);
  d.attackC = 1 - std::exp(-1 / (0.012 * sr));
  d.decayK = std::exp(-1 / (0.36 * sr));
  d.alive = d.on = d.film = d.micReading = false;
  d.chamber = C.chamber.load();
  d.root = rootOf(d.chamber);
  d.lastPluck = -1e9;
  d.ready = true;
  C.silent.store(true);
}

bool synth_render(float* left, float* right, int frames, float* filmL, float* filmR) {
  Dsp& d = D;
  if (!d.ready) {
    std::fill(left, left + frames, 0.f); std::fill(right, right + frames, 0.f);
    return false;
  }
  controls(d);
  if (d.alive) renderSound(d, left, right, frames);
  else { std::fill(left, left + frames, 0.f); std::fill(right, right + frames, 0.f); }
  if (!d.on && d.alive && (d.offFor += frames) >= 1.6 * d.sr) d.alive = false;   // web: stop after 1600 ms
  C.silent.store(!d.alive, std::memory_order_release);
  d.now += frames;
  if (!d.film || !filmL || !filmR) return false;
  renderFilm(d, left, right, filmL, filmR, frames);
  return true;
}

void synth_set_on(bool on) { C.on.store(on, std::memory_order_release); if (on) C.silent.store(false); }
bool synth_is_on() { return C.on.load(); }
bool synth_is_silent() { return C.silent.load(std::memory_order_acquire); }
void synth_set_root(int chamber) { C.chamber.store(chamber, std::memory_order_relaxed); }

void synth_pluck(float x, float y, float v) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(v)) return;
  uint64_t seq = next(C.pluckSeq) & 0xffff;
  C.pluck.store(seq << 48 | q16(x, -4, 4) << 32 | q16(y, -4, 4) << 16 | q16(v, 0, 2), std::memory_order_release);
}

void synth_update(float dt, float energy, float lung) {
  if (C.micOn.load(std::memory_order_acquire)) {
    float b[3];
    analyse(b, dt);
    follow(L.level.bass, 0, b[0], dt);
    follow(L.level.mid, 1, b[1], dt);
    follow(L.level.high, 2, b[2], dt);
  } else L.level = {0, 0, 0};
  // The drone opens up while you paint. Scheduled a few times a second, not per frame.
  L.since += dt;
  if (C.on.load(std::memory_order_relaxed) && L.since >= 0.125) {
    L.since = std::fmod(L.since, 0.125);
    float swell = lung < 0 ? 0 : lung;
    float e = std::min(energy, 5.f);
    C.filtTarget.store(240 + e * 260 + swell * 280, std::memory_order_relaxed);
    C.droneTarget.store((lung < 0 ? DRONE : DRONE * (0.5f + lung)) * (1 - 0.06f * e), std::memory_order_relaxed);
    next(C.targetSeq);
  }
}

SynthLevels synth_levels() { return L.level; }

void synth_hush(bool fast) { C.hush.store(next(C.hushSeq) << 2 | (fast ? 2 : 1), std::memory_order_release); }
void synth_unhush() { C.hush.store(next(C.hushSeq) << 2, std::memory_order_release); }

void synth_film_begin() { C.film.store(next(C.filmSeq) << 2 | 1, std::memory_order_release); }
void synth_film_fade() { C.film.store(next(C.filmSeq) << 2 | 2, std::memory_order_release); }
void synth_film_end() { C.film.store(next(C.filmSeq) << 2 | 3, std::memory_order_release); }

void synth_mic_start(double rate) {
  for (auto& s : C.mic) s.store(0, std::memory_order_relaxed);
  C.micRate.store(rate, std::memory_order_relaxed);
  C.micOn.store(true, std::memory_order_release);
}
void synth_mic_stop() { C.micOn.store(false, std::memory_order_release); }

void synth_mic_push(const float* mono, int frames) {
  uint64_t w = C.micW.load(std::memory_order_relaxed);
  for (int i = 0; i < frames; i++) C.mic[(w + i) & (MIC_RING - 1)].store(mono[i], std::memory_order_relaxed);
  C.micW.store(w + frames, std::memory_order_release);
}
