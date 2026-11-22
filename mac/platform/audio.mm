// Output: the default-output Audio Unit rather than AVAudioEngine. It follows the
// system's default output device by itself (as a browser does), its render callback
// carries the hardware's own timestamp for the film, and there is no graph or mixer in
// between. Input: a HAL unit with input only, created when listening starts and
// disposed when it stops, so the microphone is released.
#import "audio.h"
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace {

std::recursive_mutex gLock;            // control state; never taken on an audio thread
AudioUnit gOut = nullptr;
bool gOutRunning = false, gRecheck = false, gFilm = false;
double gRate = 0;
std::atomic<uint64_t> gLatencyTicks{0};
mach_timebase_info_data_t gTB;

AudioUnit gMic = nullptr;
bool gListen = false;
uint32_t gListenGen = 0;

std::atomic<AudioFilmTap> gTap{nullptr};
std::atomic<bool> gInTap{false};

constexpr int CHUNK = 1024;
float gFilmL[CHUNK], gFilmR[CHUNK];
constexpr int MIC_MAX = 4096;
float gMicData[2][MIC_MAX], gMono[MIC_MAX];
struct { AudioBufferList list; AudioBuffer second; } gMicBufs;

uint64_t ticks(double seconds) { return (uint64_t)(seconds * 1e9 * gTB.denom / gTB.numer); }

AudioDeviceID defaultDevice(AudioObjectPropertySelector which) {
  AudioObjectPropertyAddress a{which, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioDeviceID dev = kAudioObjectUnknown;
  UInt32 size = sizeof dev;
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, &dev);
  return dev;
}

UInt32 deviceUInt(AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope) {
  AudioObjectPropertyAddress a{sel, scope, kAudioObjectPropertyElementMain};
  UInt32 v = 0, size = sizeof v;
  return AudioObjectGetPropertyData(obj, &a, 0, nullptr, &size, &v) == noErr ? v : 0;
}

double nominalRate(AudioDeviceID dev) {
  AudioObjectPropertyAddress a{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  Float64 r = 0;
  UInt32 size = sizeof r;
  if (dev == kAudioObjectUnknown || AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &r) != noErr) return 0;
  return r;
}

AudioStreamBasicDescription floatFormat(double rate, UInt32 channels) {
  AudioStreamBasicDescription f{};
  f.mSampleRate = rate;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
  f.mBytesPerPacket = f.mBytesPerFrame = 4;
  f.mFramesPerPacket = 1;
  f.mChannelsPerFrame = channels;
  f.mBitsPerChannel = 32;
  return f;
}

// From the output timestamp to when the sound leaves the device.
void measureLatency() {
  AudioDeviceID dev = kAudioObjectUnknown;
  UInt32 size = sizeof dev;
  if (!gOut || AudioUnitGetProperty(gOut, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, &size) != noErr) return;
  double rate = nominalRate(dev);
  if (rate <= 0) return;
  UInt32 frames = deviceUInt(dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) +
                  deviceUInt(dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput);
  AudioObjectPropertyAddress a{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
  AudioStreamID stream = 0;
  size = sizeof stream;
  if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &stream) == noErr && size)
    frames += deviceUInt(stream, kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
  gLatencyTicks.store(ticks(frames / rate));
}

OSStatus render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts, UInt32, UInt32 frames, AudioBufferList* io) {
  float* l = (float*)io->mBuffers[0].mData;
  float* r = io->mNumberBuffers > 1 ? (float*)io->mBuffers[1].mData : nullptr;
  gInTap.store(true);
  AudioFilmTap tap = gTap.load();
  uint64_t host = (ts->mFlags & kAudioTimeStampHostTimeValid) ? ts->mHostTime : mach_absolute_time();
  host += gLatencyTicks.load(std::memory_order_relaxed);
  for (UInt32 o = 0; o < frames; o += CHUNK) {
    int n = (int)std::min<UInt32>(CHUNK, frames - o);
    float scratch[CHUNK];
    bool film = synth_render(l + o, r ? r + o : scratch, n, tap ? gFilmL : nullptr, tap ? gFilmR : nullptr);
    if (film) tap(gFilmL, gFilmR, n, host + ticks(o / gRate));
  }
  gInTap.store(false);
  return noErr;
}

bool openOutput() {
  if (gOut) return true;
  double rate = nominalRate(defaultDevice(kAudioHardwarePropertyDefaultOutputDevice));
  if (rate <= 0) rate = 48000;
  AudioComponentDescription d{kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c || AudioComponentInstanceNew(c, &gOut) != noErr) { gOut = nullptr; return false; }
  AudioStreamBasicDescription f = floatFormat(rate, 2);
  AURenderCallbackStruct cb{render, nullptr};
  if (AudioUnitSetProperty(gOut, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr ||
      AudioUnitSetProperty(gOut, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr ||
      AudioUnitInitialize(gOut) != noErr) {
    AudioComponentInstanceDispose(gOut);
    gOut = nullptr;
    return false;
  }
  gRate = rate;
  synth_create(rate);
  measureLatency();
  return true;
}

void closeOutput() {
  if (!gOut) return;
  if (gOutRunning) AudioOutputUnitStop(gOut);
  AudioUnitUninitialize(gOut);
  AudioComponentInstanceDispose(gOut);
  gOut = nullptr;
  gOutRunning = false;
}

// Run the hardware exactly while there is something to play or film.
void reconcile() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  bool fading = !synth_is_on() && !synth_is_silent();
  bool need = synth_is_on() || fading || gFilm;
  if (need && !gOutRunning && openOutput()) gOutRunning = AudioOutputUnitStart(gOut) == noErr;
  else if (!need && gOutRunning) { AudioOutputUnitStop(gOut); gOutRunning = false; }
  if (gOutRunning && fading && !gFilm && !gRecheck) {
    gRecheck = true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      { std::lock_guard<std::recursive_mutex> g2(gLock); gRecheck = false; }
      reconcile();
    });
  }
}

OSStatus micInput(void* unit, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32, UInt32 frames, AudioBufferList*) {
  if (frames > MIC_MAX) return noErr;
  AudioBufferList* list = &gMicBufs.list;
  for (UInt32 c = 0; c < list->mNumberBuffers; c++) {
    list->mBuffers[c].mData = gMicData[c];
    list->mBuffers[c].mDataByteSize = frames * 4;
  }
  OSStatus e = AudioUnitRender((AudioUnit)unit, flags, ts, 1, frames, list);
  if (e != noErr) return e;
  if (list->mNumberBuffers == 2)   // stereo to mono as Web Audio down-mixes: (L + R) / 2
    for (UInt32 i = 0; i < frames; i++) gMono[i] = 0.5f * (gMicData[0][i] + gMicData[1][i]);
  else std::copy(gMicData[0], gMicData[0] + frames, gMono);
  synth_mic_push(gMono, (int)frames);
  return noErr;
}

void closeMic() {
  if (!gMic) return;
  synth_mic_stop();
  AudioOutputUnitStop(gMic);
  AudioUnitUninitialize(gMic);
  AudioComponentInstanceDispose(gMic);
  gMic = nullptr;
}

bool openMic() {
  AudioDeviceID dev = defaultDevice(kAudioHardwarePropertyDefaultInputDevice);
  if (dev == kAudioObjectUnknown) return false;
  AudioComponentDescription d{kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c || AudioComponentInstanceNew(c, &gMic) != noErr) { gMic = nullptr; return false; }
  UInt32 one = 1, zero = 0, maxFrames = MIC_MAX;
  AudioStreamBasicDescription hw{};
  UInt32 size = sizeof hw;
  bool ok = AudioUnitSetProperty(gMic, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, sizeof one) == noErr &&
            AudioUnitSetProperty(gMic, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero, sizeof zero) == noErr &&
            AudioUnitSetProperty(gMic, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof dev) == noErr &&
            AudioUnitSetProperty(gMic, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames) == noErr &&
            AudioUnitGetProperty(gMic, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hw, &size) == noErr &&
            hw.mChannelsPerFrame > 0 && hw.mSampleRate > 0;
  if (ok) {
    UInt32 channels = std::min<UInt32>(hw.mChannelsPerFrame, 2);
    AudioStreamBasicDescription f = floatFormat(hw.mSampleRate, channels);
    gMicBufs.list.mNumberBuffers = channels;
    for (UInt32 i = 0; i < channels; i++) gMicBufs.list.mBuffers[i].mNumberChannels = 1;
    AURenderCallbackStruct cb{micInput, gMic};
    ok = AudioUnitSetProperty(gMic, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f) == noErr &&
         AudioUnitSetProperty(gMic, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb) == noErr &&
         AudioUnitInitialize(gMic) == noErr;
    if (ok) {
      synth_mic_start(hw.mSampleRate);
      ok = AudioOutputUnitStart(gMic) == noErr;
    }
  }
  if (!ok) closeMic();
  return ok;
}

void ensure() {
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    mach_timebase_info(&gTB);
    dispatch_queue_t main = dispatch_get_main_queue();
    // A new output device at a different rate: rebuild at its rate (not mid-film, since
    // the film's rate is fixed; the unit converts until then).
    AudioObjectPropertyAddress out{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &out, main, ^(UInt32, const AudioObjectPropertyAddress*) {
      std::lock_guard<std::recursive_mutex> g(gLock);
      if (!gOut) return;
      double rate = nominalRate(defaultDevice(kAudioHardwarePropertyDefaultOutputDevice));
      if (rate > 0 && rate != gRate && !gFilm) { closeOutput(); reconcile(); }
      else measureLatency();
    });
    AudioObjectPropertyAddress in{kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &in, main, ^(UInt32, const AudioObjectPropertyAddress*) {
      std::lock_guard<std::recursive_mutex> g(gLock);
      if (!gListen) return;
      closeMic();
      gListen = openMic();
    });
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    NSOperationQueue* q = NSOperationQueue.mainQueue;
    [nc addObserverForName:NSApplicationDidHideNotification object:nil queue:q usingBlock:^(NSNotification*) { synth_hush(false); }];
    [nc addObserverForName:NSApplicationDidUnhideNotification object:nil queue:q usingBlock:^(NSNotification*) { synth_unhush(); }];
    [nc addObserverForName:NSApplicationDidChangeOcclusionStateNotification object:nil queue:q usingBlock:^(NSNotification*) {
      if (NSApp.occlusionState & NSApplicationOcclusionStateVisible) synth_unhush();
      else synth_hush(false);
    }];
    [nc addObserverForName:NSApplicationWillTerminateNotification object:nil queue:q usingBlock:^(NSNotification*) {
      if (!synth_is_on()) return;
      synth_hush(true);
      usleep(60000);   // let the fast fade play out before the process ends
    }];
  });
}

}  // namespace

void audio_set_sound(bool on) {
  ensure();
  synth_set_on(on);
  reconcile();
}
void audio_set_root(int chamber) { synth_set_root(chamber); }
void audio_pluck(float x, float y, float v) { synth_pluck(x, y, v); }
void audio_update(float dt, float energy, float lung) { synth_update(dt, energy, lung); }
SynthLevels audio_levels() { return synth_levels(); }

void audio_set_listen(bool on, std::function<void(ListenResult)> done) {
  ensure();
  std::lock_guard<std::recursive_mutex> g(gLock);
  uint32_t gen = ++gListenGen;
  if (!on) {
    closeMic();
    gListen = false;
    return;
  }
  auto finish = [gen, done](bool allowed) {
    std::lock_guard<std::recursive_mutex> g2(gLock);
    if (gen != gListenGen) return;   // turned off or asked again meanwhile
    ListenResult r = ListenResult::denied;
    if (allowed) {
      if (!gListen) gListen = openMic();
      r = gListen ? ListenResult::listening : ListenResult::unavailable;
    }
    if (done) done(r);
  };
  AVAuthorizationStatus s = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (s == AVAuthorizationStatusNotDetermined)
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
      dispatch_async(dispatch_get_main_queue(), ^{ finish(granted); });
    }];
  else {
    bool allowed = s == AVAuthorizationStatusAuthorized;
    dispatch_async(dispatch_get_main_queue(), ^{ finish(allowed); });
  }
}

bool audio_listening() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  return gListen;
}

bool audio_film_has_sound() { return synth_is_on() || audio_listening(); }

double audio_sample_rate() {
  ensure();
  std::lock_guard<std::recursive_mutex> g(gLock);
  openOutput();
  return gRate;
}

void audio_film_begin(AudioFilmTap tap) {
  ensure();
  std::lock_guard<std::recursive_mutex> g(gLock);
  synth_film_begin();
  gTap.store(tap);
  gFilm = true;
  reconcile();
}

void audio_film_fade(std::function<void()> silent) {
  synth_film_fade();
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 420 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    if (silent) silent();
  });
}

void audio_film_end() {
  std::lock_guard<std::recursive_mutex> g(gLock);
  synth_film_end();
  gTap.store(nullptr);
  while (gInTap.load()) usleep(200);
  gFilm = false;
  reconcile();
}
