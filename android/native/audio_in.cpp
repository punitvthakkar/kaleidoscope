// Listen (host.h): the microphone on AAudio, mono float, feeding the synth's analyser
// (synth_mic_start / push / stop), which yields the three band levels exactly as on the
// Mac (mac/platform/audio.mm, micInput). The stream exists only while Listen is on.
// Calls: main thread; the error callback reopens from its own short-lived thread.
// State: one struct under one lock that the audio callback never takes.
#include "host.h"
#include "core/synth.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <algorithm>
#include <mutex>
#include <thread>

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "fx", __VA_ARGS__)

namespace {

constexpr int CHUNK = 1024;

struct Mic {
  std::mutex lock;
  AAudioStream* stream = nullptr;
  bool on = false;               // Listen is on (the stream may be reopening)
  int channels = 1;              // audio thread reads; fixed while the stream is open
  float mono[CHUNK];             // audio thread only
} M;

aaudio_data_callback_result_t input(AAudioStream*, void*, void* data, int32_t frames) {
  const float* in = (const float*)data;
  if (M.channels == 1) { synth_mic_push(in, frames); return AAUDIO_CALLBACK_RESULT_CONTINUE; }
  for (int32_t o = 0; o < frames; o += CHUNK) {
    const int n = std::min<int32_t>(CHUNK, frames - o);
    for (int i = 0; i < n; i++) {
      float s = 0;
      for (int c = 0; c < M.channels; c++) s += in[(o + i) * M.channels + c];
      M.mono[i] = s / M.channels;
    }
    synth_mic_push(M.mono, n);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void closeLocked() {
  if (!M.stream) return;
  AAudioStream_requestStop(M.stream);
  AAudioStream_close(M.stream);
  M.stream = nullptr;
  synth_mic_stop();
}

bool openLocked();

// Disconnected (headset in or out): reopen on the new device, off the audio thread.
void onError(AAudioStream* s, void*, aaudio_result_t err) {
  std::thread([s, err] {
    std::lock_guard<std::mutex> g(M.lock);
    if (s != M.stream) return;
    LOGW("mic: stream error %s, reopening", AAudio_convertResultToText(err));
    closeLocked();
    if (M.on && !openLocked()) M.on = false;
  }).detach();
}

bool openLocked() {
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_INPUT);
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(b, 1);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setInputPreset(b, AAUDIO_INPUT_PRESET_UNPROCESSED);   // the room as it is, as on the Mac
  AAudioStreamBuilder_setDataCallback(b, input, nullptr);
  AAudioStreamBuilder_setErrorCallback(b, onError, nullptr);
  aaudio_result_t r = AAudioStreamBuilder_openStream(b, &M.stream);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK) { LOGW("mic: open failed: %s", AAudio_convertResultToText(r)); M.stream = nullptr; return false; }
  M.channels = AAudioStream_getChannelCount(M.stream);
  if (AAudioStream_getFormat(M.stream) != AAUDIO_FORMAT_PCM_FLOAT || M.channels < 1) {
    LOGW("mic: stream is not float");
    AAudioStream_close(M.stream);
    M.stream = nullptr;
    return false;
  }
  synth_mic_start(AAudioStream_getSampleRate(M.stream));
  r = AAudioStream_requestStart(M.stream);
  if (r != AAUDIO_OK) { LOGW("mic: start failed: %s", AAudio_convertResultToText(r)); closeLocked(); return false; }
  return true;
}

}  // namespace

bool audio_in_start() {
  std::lock_guard<std::mutex> g(M.lock);
  if (M.stream) return true;
  M.on = openLocked();
  return M.on;
}

void audio_in_stop() {
  std::lock_guard<std::mutex> g(M.lock);
  M.on = false;
  closeLocked();
}
