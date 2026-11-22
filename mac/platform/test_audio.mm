// test_audio: checks the synth offline, and the Core Audio path live.
//   test_audio           20 s offline render -> build/test_audio.wav (+ film copy), level report, biquad checks
//   test_audio --check   5 s on the hardware, hushed (inaudible): callbacks, film tap, hardware stop
//   test_audio --live    5 s audible: drone and plucks, for listening
// Build: see the command at the end of this file.
#import "audio.h"
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("cannot write %s\n", path.c_str()); fails++; return; }
  uint32_t n = (uint32_t)l.size(), data = n * 8;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(4 + 26 + 12 + 8 + data);
  fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); u32(18); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32); u16(0);
  fwrite("fact", 1, 4, f); u32(4); u32(n);
  fwrite("data", 1, 4, f); u32(data);
  for (uint32_t i = 0; i < n; i++) { fwrite(&l[i], 4, 1, f); fwrite(&r[i], 4, 1, f); }
  fclose(f);
}

static void report(const char* name, const std::vector<float>& l, const std::vector<float>& r, float* peakOut) {
  double peak = 0, sum2[2] = {}, sum[2] = {};
  long bad = 0;
  const std::vector<float>* ch[2] = {&l, &r};
  for (int c = 0; c < 2; c++)
    for (float s : *ch[c]) {
      if (!std::isfinite(s)) { bad++; continue; }
      peak = std::max(peak, (double)std::fabs(s));
      sum2[c] += (double)s * s; sum[c] += s;
    }
  double n = l.size();
  printf("%-6s peak %.4f (%.2f dBFS)  RMS L %.4f R %.4f (%.1f dBFS)  DC L %+.2e R %+.2e  NaN/inf %ld\n", name, peak,
         20 * log10(peak + 1e-12), sqrt(sum2[0] / n), sqrt(sum2[1] / n), 10 * log10((sum2[0] + sum2[1]) / (2 * n) + 1e-20),
         sum[0] / n, sum[1] / n, bad);
  CHECK(bad == 0);
  *peakOut = (float)peak;
}

static void checkListen();

static void checkBiquads() {
  // Web Audio spec formulas (Q in dB for lowpass/highpass), computed independently in double.
  struct { int type; double f, q; double k[5]; } cases[] = {
    {0, 2200, 1, {0.018276923418207736, 0.036553846836415473, 0.018276923418207736, -1.7022012665495689, 0.77530896022240003}},
    {1, 48, 0.7, {0.99710021041175279, -1.9942004208235056, 0.99710021041175279, -1.9941807387247528, 0.99422010292225871}},
  };
  for (auto& c : cases) {
    BiquadCoefs k = synth_biquad(c.type, c.f, c.q, 48000);
    double got[5] = {k.b0, k.b1, k.b2, k.a1, k.a2}, worst = 0;
    for (int i = 0; i < 5; i++) worst = std::max(worst, std::fabs(got[i] - c.k[i]));
    // For these types the gain at the cutoff is exactly Q dB.
    double w = 2 * M_PI * c.f / 48000;
    std::complex<double> z = std::exp(std::complex<double>(0, -w));
    double gain = std::abs((k.b0 + k.b1 * z + k.b2 * z * z) / (1.0 + k.a1 * z + k.a2 * z * z));
    printf("biquad %s %g Hz Q %g dB: max coefficient error %.1e, gain at cutoff %.6f dB\n",
           c.type ? "highpass" : "lowpass", c.f, c.q, worst, 20 * log10(gain));
    CHECK(worst < 1e-12);
    CHECK(std::fabs(20 * log10(gain) - c.q) < 1e-9);
  }
}

static int offline() {
  checkBiquads();
  const int rate = 48000, block = 512, total = rate * 20, perFrame = rate / 60;
  synth_create(rate);
  synth_set_root(0);
  synth_set_on(true);
  std::vector<float> L(total), R(total), FL(total, 0.f), FR(total, 0.f);
  int nextFrame = 0, nextPluck = rate, k = 0;
  bool filming = false, faded = false, rerooted = false, hushed = false, unhushed = false, off = false;
  double lastSilentAt = -1;
  uint64_t t0 = mach_absolute_time();
  for (int at = 0; at < total; at += block) {
    double t = (double)at / rate;
    // Frame loop at 60 Hz: energy rises and falls; the breathing guide from 8 s.
    while (nextFrame <= at) {
      double ft = (double)nextFrame / rate;
      float energy = (float)(ft < 4 ? ft * 1.5 : ft < 12 ? 6 - (ft - 4) * 0.6 : 0.5);
      float lung = ft < 8 ? -1.f : (float)(0.5 - 0.5 * cos(M_PI * fmod(ft, 10) / 5));
      synth_update(1 / 60.f, std::max(0.f, energy), lung);
      nextFrame += perFrame;
    }
    // Plucks walking round the circle and outward, a burst too fast to all sound, and
    // from 12 s a pile-up of the loudest notes the web ever plays and louder.
    while (nextPluck <= at && t < 18) {
      double a = k * 2.4, rad = 0.1 + 0.9 * fmod(k * 0.37, 1.0);
      float v = t < 6 ? 0.3f : t < 12 ? 0.5f : 1.0f;
      synth_pluck((float)(cos(a) * rad), (float)(sin(a) * rad), v);
      k++;
      nextPluck += t > 5 && t < 6 ? rate / 20 : t >= 12 ? rate / 10 : rate / 4;
    }
    if (!filming && t >= 2) { synth_film_begin(); filming = true; }
    if (!rerooted && t >= 10) { synth_set_root(3); rerooted = true; }
    if (!hushed && t >= 15) { synth_hush(false); hushed = true; }
    if (!unhushed && t >= 16) { synth_unhush(); unhushed = true; }
    if (!faded && t >= 17.5) { synth_film_fade(); faded = true; }
    if (filming && t >= 17.95) { synth_film_end(); filming = false; }
    if (!off && t >= 18.2) { synth_set_on(false); off = true; }
    int n = std::min(block, total - at);
    synth_render(&L[at], &R[at], n, &FL[at], &FR[at]);
    if (synth_is_silent() && off && lastSilentAt < 0) lastSilentAt = t;
  }
  double secs = (mach_absolute_time() - t0) * 1e-9 * [] { mach_timebase_info_data_t tb; mach_timebase_info(&tb); return (double)tb.numer / tb.denom; }();
  printf("rendered 20 s in %.1f ms (%.0fx real time)\n", secs * 1e3, 20 / secs);

  float peak, fpeak;
  report("heard", L, R, &peak);
  report("film", FL, FR, &fpeak);
  CHECK(peak < 0.98f);   // the shaper's ceiling: 0.6 + 0.38 tanh(...) < 0.98
  CHECK(peak > 0.05f);
  // Film fader: silent for the first 0.12 s after 2 s, silent again after the fade.
  float early = 0, late = 0;
  for (int i = 2 * rate; i < (int)(2.11 * rate); i++) early = std::max({early, std::fabs(FL[i]), std::fabs(FR[i])});
  for (int i = (int)(17.81 * rate); i < total; i++) late = std::max({late, std::fabs(FL[i]), std::fabs(FR[i])});
  printf("film: max %.2e in its first 0.11 s, %.2e after its fade\n", early, late);
  CHECK(early == 0 && late == 0);
  // Heard: hushed (window 15.6..16 s) should be near silent; off should go silent after 1.6 s.
  float hushPeak = 0;
  for (int i = (int)(15.8 * rate); i < 16 * rate; i++) hushPeak = std::max({hushPeak, std::fabs(L[i]), std::fabs(R[i])});
  printf("hushed level %.2e; silent %.2f s after turning off\n", hushPeak, lastSilentAt - 18.2);
  CHECK(hushPeak < 0.01f);
  CHECK(lastSilentAt > 18.2 + 1.55 && lastSilentAt < 18.2 + 1.7);
  // Drone alone (first second, no plucks yet): stereo bus carries it equally.
  double diff = 0;
  for (int i = 0; i < rate; i++) diff = std::max(diff, (double)std::fabs(L[i] - R[i]));
  CHECK(diff == 0);

  // Drone pitch: root, fifth and octave of chamber 0 stand well above a frequency between them.
  auto level = [&](double hz) {
    double w = 2 * M_PI * hz / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = rate / 2; i < rate; i++) { double s0 = L[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return 20 * log10(sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) + 1e-12);
  };
  double ref = level(80);
  printf("drone vs 80 Hz: %.1f Hz %+.1f dB, %.1f Hz %+.1f dB, %.1f Hz %+.1f dB\n", 65.41, level(65.41) - ref,
         65.41 * 1.498, level(65.41 * 1.498) - ref, 65.41 * 2.004, level(65.41 * 2.004) - ref);
  CHECK(level(65.41) - ref > 20 && level(65.41 * 1.498) - ref > 20 && level(65.41 * 2.004) - ref > 20);

  NSString* dir = [[[NSString stringWithUTF8String:__FILE__] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"../build"];
  std::string base = [NSURL fileURLWithPath:dir].URLByStandardizingPath.path.UTF8String;
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  writeWav(base + "/test_audio.wav", L, R, rate);
  writeWav(base + "/test_audio_film.wav", FL, FR, rate);
  checkListen();
  printf("wrote %s/test_audio.wav and test_audio_film.wav\n", base.c_str());
  return 0;
}

// Listening without a microphone: a 44.1 kHz tone into a 48 kHz synth, as with a
// microphone on its own clock. Checks the band levels and the room's copy in the film.
static void checkListen() {
  const int rate = 48000, block = 512, mrate = 44100;
  synth_create(rate);
  synth_mic_start(mrate);
  synth_film_begin();
  std::vector<float> l(block), r(block), fl, fr, tmp(block * 2);
  std::vector<float> mic(2048);
  double ph = 0, owed = 0, hz = 150;
  SynthLevels lv{};
  for (int b = 0; b < 3 * rate / block; b++) {
    if (b == 3 * rate / block / 2) hz = 5000;   // bass for 1.5 s, then treble
    owed += block * (double)mrate / rate * 1.0001;   // the microphone's clock runs a little fast
    int n = (int)owed; owed -= n;
    for (int i = 0; i < n; i++) { mic[i] = 0.3f * (float)sin(ph); ph += 2 * M_PI * hz / mrate; }
    synth_mic_push(mic.data(), n);
    synth_render(l.data(), r.data(), block, &tmp[0], &tmp[block]);
    fl.insert(fl.end(), tmp.begin(), tmp.begin() + block);
    fr.insert(fr.end(), tmp.begin() + block, tmp.end());
    if (b % 2 == 0) {   // ~94 frames a second
      synth_update(1 / 94.f, 0, -1);
      lv = synth_levels();
      if (b == 3 * rate / block / 2 - 2) printf("listen, 150 Hz: bass %.2f mid %.2f high %.2f\n", lv.bass, lv.mid, lv.high);
      if (b == 3 * rate / block / 2 - 2) CHECK(lv.bass > 0.6f && lv.high < 0.1f);
    }
  }
  printf("listen, 5 kHz:  bass %.2f mid %.2f high %.2f\n", lv.bass, lv.mid, lv.high);
  CHECK(lv.high > 0.3f && lv.bass < 0.1f);   // one tone fills few of the 72 high bins
  // The film carries the tone continuously once the fader is up (no dropouts from the clock mismatch).
  int gaps = 0, run = 0;
  for (size_t i = rate; i < fl.size(); i++) { run = fabs(fl[i]) < 1e-6f ? run + 1 : 0; gaps += run == 64; }
  float pk = 0;
  for (size_t i = rate; i < fl.size(); i++) pk = std::max(pk, fabs(fl[i]));
  printf("listen, film: peak %.3f, %d dropouts\n", pk, gaps);
  CHECK(gaps == 0 && fabs(pk - 0.3f) < 0.01f);
  synth_film_end();
  synth_mic_stop();
  synth_update(1 / 60.f, 0, -1);
  CHECK(synth_levels().bass == 0);
}

static std::atomic<long> gTapFrames{0};
static std::atomic<uint64_t> gTapFirst{0}, gTapLast{0};
static std::atomic<int> gTapBackwards{0};
static void tap(const float*, const float*, int frames, uint64_t host) {
  if (!gTapFirst.load()) gTapFirst = host;
  if (host < gTapLast.load()) gTapBackwards++;
  gTapLast = host;
  gTapFrames += frames;
}

// Core Audio's own view of whether this process is driving an output device.
static int runningOutput() {
  AudioObjectPropertyAddress a{kAudioHardwarePropertyTranslatePIDToProcessObject, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  pid_t pid = getpid();
  AudioObjectID proc = 0; UInt32 size = sizeof proc, running = 0;
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, sizeof pid, &pid, &size, &proc) != noErr || !proc) return -1;
  a.mSelector = kAudioProcessPropertyIsRunningOutput; size = sizeof running;
  if (AudioObjectGetPropertyData(proc, &a, 0, nullptr, &size, &running) != noErr) return -1;
  return (int)running;
}

static void runFor(double seconds, bool plucks) {
  NSDate* end = [NSDate dateWithTimeIntervalSinceNow:seconds];
  int i = 0;
  while (end.timeIntervalSinceNow > 0) {
    [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1 / 60.0]];
    audio_update(1 / 60.f, 1.5f, -1);
    if (plucks && ++i % 24 == 0) audio_pluck((float)cos(i * 0.7) * 0.6f, (float)sin(i * 0.7) * 0.6f, 0.4f);
  }
}

static int live(bool audible) {
  audio_set_root(0);
  audio_set_sound(true);
  if (!audible) synth_hush(true);
  double rate = audio_sample_rate();
  printf("output at %.0f Hz%s\n", rate, audible ? "" : " (hushed)");
  CHECK(audio_film_has_sound());
  audio_film_begin(tap);
  runFor(0.2, true);
  printf("this process running output while playing: %d\n", runningOutput());
  CHECK(runningOutput() == 1);
  runFor(4.3, true);
  __block bool silent = false;
  audio_film_fade(^{ silent = true; });
  runFor(0.5, false);
  CHECK(silent);
  audio_film_end();
  double secs = (gTapLast - gTapFirst) * 1e-9 * [] { mach_timebase_info_data_t tb; mach_timebase_info(&tb); return (double)tb.numer / tb.denom; }();
  printf("film tap: %ld frames (%.2f s of sound), host times span %.2f s, %d out of order\n",
         gTapFrames.load(), gTapFrames / rate, secs, gTapBackwards.load());
  CHECK(gTapFrames > rate * 4.5 && gTapFrames < rate * 5.5);
  CHECK(fabs(secs - gTapFrames / rate) < 0.05);
  CHECK(gTapBackwards == 0);
  audio_set_sound(false);
  runFor(2.2, false);
  CHECK(synth_is_silent());
  printf("sound off: silent %d, this process running output: %d\n", synth_is_silent(), runningOutput());
  CHECK(runningOutput() == 0);
  return 0;
}

int main(int argc, char** argv) {
  @autoreleasepool {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--check") live(false);
    else if (mode == "--live") live(true);
    else offline();
    printf(fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
  }
}

// clang++ -std=c++20 -O2 -fobjc-arc -Wall -Imac/core mac/platform/test_audio.mm mac/platform/audio.mm mac/core/synth.cpp
//   -framework AudioToolbox -framework CoreAudio -framework AVFoundation -framework AppKit -o mac/build/test_audio
