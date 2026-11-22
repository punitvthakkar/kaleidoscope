// AAudio output for the synthesiser. Mirrors mac/platform/audio.mm: the stream runs
// exactly while the synth is on, fading out, or a film is being made. Control state is
// under one lock that the audio thread never takes; the audio thread sees only atomics.
#include "audio_out.h"
#include "host.h"
#include "core/synth.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "fx", __VA_ARGS__)

namespace {

std::recursive_mutex gLock;            // everything below except the atomics; never on the audio thread
AAudioStream* gStream = nullptr;
double gRate = 0;
bool gFilm = false, gForeground = true, gRecheck = false;
std::atomic<bool> gListening{false};

std::atomic<AudioFilmTap> gTap{nullptr};
std::atomic<bool> gInTap{false};

std::atomic<int> gXruns{0}, gBufFrames{0};   // for the stats line
int32_t gSeenXruns = 0;                      // audio thread only

constexpr int CHUNK = 1024;
float gL[CHUNK], gR[CHUNK], gFilmL[CHUNK], gFilmR[CHUNK];   // audio thread only

int64_t monoNanos() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

aaudio_data_callback_result_t render(AAudioStream* s, void*, void* data, int32_t frames) {
  float* out = (float*)data;
  // An underrun is heard as a click: give the stream one more burst of headroom each
  // time, up to 8 bursts (17 ms at 48 kHz, 96-frame bursts).
  const int32_t x = AAudioStream_getXRunCount(s);
  if (x > gSeenXruns) {
    gXruns.fetch_add(x - gSeenXruns, std::memory_order_relaxed);
    gSeenXruns = x;
    const int32_t burst = AAudioStream_getFramesPerBurst(s), cur = AAudioStream_getBufferSizeInFrames(s);
    if (burst > 0 && cur < 8 * burst) gBufFrames.store(AAudioStream_setBufferSizeInFrames(s, cur + burst), std::memory_order_relaxed);
  }
  gInTap.store(true);
  AudioFilmTap tap = gTap.load();
  int64_t heard = 0;
  if (tap) {   // when the first frame of this block leaves the speaker
    int64_t pos = 0, t = 0;
    const int64_t written = AAudioStream_getFramesWritten(s);
    if (AAudioStream_getTimestamp(s, CLOCK_MONOTONIC, &pos, &t) == AAUDIO_OK)
      heard = t + (int64_t)((written - pos) * 1e9 / gRate);
    else
      heard = monoNanos() + (int64_t)(AAudioStream_getBufferSizeInFrames(s) * 1e9 / gRate);
  }
  for (int32_t o = 0; o < frames; o += CHUNK) {
    const int n = std::min<int32_t>(CHUNK, frames - o);
    const bool film = synth_render(gL, gR, n, tap ? gFilmL : nullptr, tap ? gFilmR : nullptr);
    float* d = out + 2 * o;
    for (int i = 0; i < n; i++) { d[2 * i] = gL[i]; d[2 * i + 1] = gR[i]; }
    if (film) tap(gFilmL, gFilmR, n, heard + (int64_t)(o * 1e9 / gRate));
  }
  gInTap.store(false);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void reconcile();

void closeStream() {
  if (!gStream) return;
  AAudioStream_requestStop(gStream);
  AAudioStream_close(gStream);
  gStream = nullptr;
  gBufFrames.store(0, std::memory_order_relaxed);
}

// Disconnected (headphones out, route change): reopen on the new device, off the
// audio thread as AAudio requires.
void onError(AAudioStream* s, void*, aaudio_result_t err) {
  std::thread([s, err] {
    std::lock_guard<std::recursive_mutex> g(gLock);
    if (s != gStream) return;
    LOGW("audio: stream error %s, reopening", AAudio_convertResultToText(err));
    closeStream();
    reconcile();
  }).detach();
}

bool openStream() {
  if (gStream) return true;
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(b, 2);
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
  AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
  AAudioStreamBuilder_setDataCallback(b, render, nullptr);
  AAudioStreamBuilder_setErrorCallback(b, onError, nullptr);
  aaudio_result_t r = AAudioStreamBuilder_openStream(b, &gStream);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK) { LOGW("audio: open failed: %s", AAudio_convertResultToText(r)); gStream = nullptr; return false; }
  if (AAudioStream_getFormat(gStream) != AAUDIO_FORMAT_PCM_FLOAT || AAudioStream_getChannelCount(gStream) != 2) {
    LOGW("audio: stream is not stereo float");
    AAudioStream_close(gStream);
    gStream = nullptr;
    return false;
  }
  // Four bursts to start with (8 ms at 48 kHz): the default two leave no room for a
  // render thread and a web view busy on the same cores.
  gSeenXruns = 0;
  if (const int32_t burst = AAudioStream_getFramesPerBurst(gStream); burst > 0)
    gBufFrames.store(AAudioStream_setBufferSizeInFrames(gStream, 4 * burst), std::memory_order_relaxed);
  const double rate = AAudioStream_getSampleRate(gStream);
  if (rate != gRate) { gRate = rate; synth_create(rate); }   // nothing renders yet
  return true;
}

// Run the hardware exactly while there is something to play or film.
void reconcile() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  const bool fading = !synth_is_on() && !synth_is_silent();
  const bool need = gFilm || (gForeground && (synth_is_on() || fading));
  if (need && !gStream && openStream()) {
    aaudio_result_t r = AAudioStream_requestStart(gStream);
    if (r != AAUDIO_OK) { LOGW("audio: start failed: %s", AAudio_convertResultToText(r)); closeStream(); }
  } else if (!need && gStream) {
    closeStream();
  }
  if (gStream && (fading || !gForeground) && !gFilm && !gRecheck) {
    gRecheck = true;
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      { std::lock_guard<std::recursive_mutex> g2(gLock); gRecheck = false; }
      reconcile();
    }).detach();
  }
}

}  // namespace

void audio_set_sound(bool on) {
  synth_set_on(on);
  reconcile();
}

void audio_set_foreground(bool on) {
  std::lock_guard<std::recursive_mutex> g(gLock);
  if (on == gForeground) return;
  gForeground = on;
  if (on) { synth_unhush(); reconcile(); return; }
  synth_hush(false);
  if (gStream && !gRecheck) {   // let the hush play out, then close
    gRecheck = true;
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      { std::lock_guard<std::recursive_mutex> g2(gLock); gRecheck = false; }
      reconcile();
    }).detach();
  }
}

void audio_set_listening(bool on) { gListening.store(on); }

int audio_xruns() { return gXruns.load(std::memory_order_relaxed); }
int audio_buffer_frames() { return gBufFrames.load(std::memory_order_relaxed); }

bool audio_film_has_sound() { return synth_is_on() || gListening.load(); }

double audio_sample_rate() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  if (gRate <= 0 && openStream()) reconcile();   // learn the device rate; closes again if unneeded
  return gRate > 0 ? gRate : 48000;
}

void audio_film_begin(AudioFilmTap tap) {
  std::lock_guard<std::recursive_mutex> g(gLock);
  synth_film_begin();
  gTap.store(tap);
  gFilm = true;
  reconcile();
}

void audio_film_fade(std::function<void()> silent) {
  synth_film_fade();
  std::thread([silent = std::move(silent)] {
    std::this_thread::sleep_for(std::chrono::milliseconds(420));
    if (silent) host_post_render(silent);
  }).detach();
}

void audio_film_end() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  synth_film_end();
  gTap.store(nullptr);
  while (gInTap.load()) usleep(200);
  gFilm = false;
  reconcile();
}
