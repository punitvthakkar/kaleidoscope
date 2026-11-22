// VideoToolbox is used directly rather than through AVAssetWriter's encoder because
// only a VTCompressionSession can *require* the hardware encoder (failing at start
// instead of quietly falling back), report that it is in use, and expose the
// encoder's own pixel buffer pool. AVAssetWriter then only muxes (passthrough).
#import "recorder.h"
#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <mach/mach_time.h>
#import <os/lock.h>
#include <algorithm>
#include <atomic>
#include <cmath>

NSString* const RecorderErrorDomain = @"fractonaut.recorder";

namespace {

constexpr int kPoolFrames = 6;       // raw 10-bit frames that may exist at once
constexpr int kMaxInFlight = 10;     // frames admitted but not yet in the file (raw + compressed)
constexpr int kSlots = 64, kSlotFrames = 4096;  // audio ring: about 5 s at 48 kHz
constexpr int kMaxAudioPending = 256;

// Reference white: Apple decodes HLG per BT.2408, with HLG signal 0.75 (203 cd/m2 on a
// 1000 cd/m2 display, system gamma 1.2) at extended-linear 1.0. Encoding is the exact
// inverse, so SDR white lands at 75% and 4.93 is the brightest value a film can hold.
// Measured: Apple's OOTF takes as luminance the mean of BT.2020 Y and max(R,G,B), not Y
// alone as BT.2100 does; inverting Apple's keeps saturated colours right in Photos.
NSString* const kShader = @R"(
#include <metal_stdlib>
using namespace metal;
struct P { float2 scale; uint2 half_size; };
constant float3x3 toBT2020 = float3x3(float3(0.627404, 0.069097, 0.016391),
                                      float3(0.329283, 0.919540, 0.088013),
                                      float3(0.043313, 0.011362, 0.895595));
constant float3 kY = float3(0.2627, 0.6780, 0.0593);

static float3 hlg(float3 srgb) {
  float3 d = max(toBT2020 * srgb, 0.0) * (203.0 / 1000.0);   // display light, 1 = 1000 cd/m2
  float l = 0.5 * dot(d, kY) + 0.5 * max3(d.r, d.g, d.b);
  float3 e = l > 1e-9 ? d * pow(l, -1.0 / 6.0) : float3(0);    // inverse OOTF, gamma 1.2
  e = clamp(e, 0.0, 1.0);
  float3 lo = sqrt(3.0 * e);
  float3 hi = 0.17883277 * log(max(12.0 * e - 0.28466892, 1e-6)) + 0.55991073;
  return select(hi, lo, e <= 1.0 / 12.0);
}

// One thread per 2x2 block. Chroma is sited left (co-sited with even columns, between
// rows): [1 2 1] across columns 2x-1..2x+1, averaged over the two rows.
kernel void convert(texture2d<float> src [[texture(0)]],
                    texture2d<float, access::write> luma [[texture(1)]],
                    texture2d<float, access::write> chroma [[texture(2)]],
                    constant P& p [[buffer(0)]],
                    uint2 g [[thread_position_in_grid]]) {
  if (any(g >= p.half_size)) return;
  constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);
  float2 c = 0;
  for (int dy = 0; dy < 2; dy++) {
    for (int dx = -1; dx < 2; dx++) {
      int x = max(int(g.x) * 2 + dx, 0), y = int(g.y) * 2 + dy;
      float3 e = hlg(src.sample(s, (float2(x, y) + 0.5) * p.scale).rgb);
      float yp = dot(e, kY);
      if (dx >= 0) luma.write(float4(round(64.0 + 876.0 * yp) * 64.0 / 65535.0), uint2(x, y));
      c += (dx == 0 ? 2.0 : 1.0) * float2((e.b - yp) / 1.8814, (e.r - yp) / 1.4746);
    }
  }
  chroma.write(float4(round(512.0 + 896.0 * c / 8.0) * 64.0 / 65535.0, 0, 0), g);
}
)";

double ticksPerSecond() {
  static double v = [] { mach_timebase_info_data_t tb; mach_timebase_info(&tb); return 1e9 * tb.denom / tb.numer; }();
  return v;
}

// Bits follow pixels, as on the web (0.085 bits per pixel for H.264, 4..30 Mb/s). HEVC
// needs about 0.6x the bits of H.264 for the same picture; 10-bit HDR about 1.25x back.
double bitrateFor(int w, int h) { return std::clamp(w * (double)h * 60 * 0.064, 6e6, 50e6); }

NSError* makeError(RecorderError code, OSStatus status = 0, NSError* underlying = nil) {
  NSMutableDictionary* info = [NSMutableDictionary dictionary];
  if (status) info[@"status"] = @(status);
  if (underlying) info[NSUnderlyingErrorKey] = underlying;
  return [NSError errorWithDomain:RecorderErrorDomain code:code userInfo:info];
}

struct AudioSlot { uint64_t host; int frames; float pcm[kSlotFrames * 2]; };
// Single producer (the audio thread, through gRing: no lock, no Objective-C object),
// single consumer (writerQ). Owned by its recorder.
struct AudioRing {
  AudioSlot slots[kSlots];
  std::atomic<uint64_t> head{0}, tail{0};
  std::atomic<int64_t> dropped{0};
  std::atomic<bool> open{true};      // false once the recording stops or fails
  double rate = 48000, ticksPerSec = 1;
};
std::atomic<AudioRing*> gRing{nullptr};

} // namespace

@interface FNRecorder : NSObject {
@public
  int W, H; float scaleX, scaleY;
  double sampleRate; bool hasAudio; bool hardware; double bitrate;
  NSURL* url;
  id<MTLComputePipelineState> pipe;
  CVMetalTextureCacheRef texCache;
  VTCompressionSessionRef vt;
  CVPixelBufferPoolRef pool;
  NSDictionary* poolAux;
  CMAudioFormatDescriptionRef audioFormat;
  dispatch_queue_t encodeQ, writerQ;
  dispatch_source_t timer;
  AVAssetWriter* writer;
  AVAssetWriterInput *vIn, *aIn;
  void (^onFailure)(NSError*);
  double startSec;

  int64_t lastSlot;                  // render thread
  int64_t lastEncodedSlot;           // encodeQ
  std::atomic<int> inFlight, gpuPending;
  std::atomic<int64_t> written, dropped, audioDropped;
  std::atomic<bool> stopping, failed;

  AudioRing* ring;

  // writerQ only
  NSMutableArray* vPending; NSMutableArray* aPending;
  bool writerStarted, finishing, finished, audioAnchored;
  int64_t audioNext; CMTime firstPts, lastPts; double finishDeadline;
  void (^done)(NSURL*, double, int64_t, NSError*);
  FNRecorder* keepAlive;             // from stop until the file is finished
}
@property(atomic, strong) NSError* error;
@end

static os_unfair_lock gLock = OS_UNFAIR_LOCK_INIT;
static FNRecorder* gRec;
#ifdef FX_TEST
static std::atomic<int> gSlowMs{0};
#endif

static FNRecorder* current() {
  os_unfair_lock_lock(&gLock);
  FNRecorder* r = gRec;
  os_unfair_lock_unlock(&gLock);
  return r;
}

static void vtOutput(void* ref, void*, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb);

@implementation FNRecorder

- (void)dealloc {
  if (vt) { VTCompressionSessionInvalidate(vt); CFRelease(vt); }
  if (texCache) CFRelease(texCache);
  if (audioFormat) CFRelease(audioFormat);
  delete ring;
}

- (void)fail:(NSError*)e {
  bool expected = false;
  if (!failed.compare_exchange_strong(expected, true)) return;
  if (ring) ring->open = false;
  self.error = e;
  auto cb = onFailure;
  if (cb) dispatch_async(dispatch_get_main_queue(), ^{ cb(e); });
}

- (NSError*)setup:(id<MTLDevice>)device width:(int)width height:(int)height {
  W = width & ~1; H = height & ~1;
  if (W < 64 || H < 64) return makeError(RecorderErrorSize);
  scaleX = 1.f / width; scaleY = 1.f / height;
  bitrate = bitrateFor(W, H);

  NSError* e = nil;
  static id<MTLDevice> pipeDevice; static id<MTLComputePipelineState> pipeCache;
  if (pipeDevice != device) {
    id<MTLLibrary> lib = [device newLibraryWithSource:kShader options:nil error:&e];
    id<MTLFunction> fn = [lib newFunctionWithName:@"convert"];
    pipeCache = fn ? [device newComputePipelineStateWithFunction:fn error:&e] : nil;
    if (!pipeCache) return makeError(RecorderErrorSetup, 0, e);
    pipeDevice = device;
  }
  pipe = pipeCache;
  if (CVMetalTextureCacheCreate(NULL, NULL, device,
        (__bridge CFDictionaryRef)@{(id)kCVMetalTextureUsage: @(MTLTextureUsageShaderWrite)}, &texCache))
    return makeError(RecorderErrorSetup);

  NSDictionary* spec = @{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
  NSDictionary* source = @{
    (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange),
    (id)kCVPixelBufferWidthKey: @(W), (id)kCVPixelBufferHeightKey: @(H),
    (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
    (id)kCVPixelBufferMetalCompatibilityKey: @YES,
  };
  OSStatus st = VTCompressionSessionCreate(NULL, W, H, kCMVideoCodecType_HEVC,
      (__bridge CFDictionaryRef)spec, (__bridge CFDictionaryRef)source, NULL,
      vtOutput, (__bridge void*)self, &vt);
  if (st) return makeError(RecorderErrorNoHardware, st);   // without the requirement this would be software

  NSDictionary* props = @{
    (id)kVTCompressionPropertyKey_ProfileLevel: (id)kVTProfileLevel_HEVC_Main10_AutoLevel,
    (id)kVTCompressionPropertyKey_RealTime: @YES,
    (id)kVTCompressionPropertyKey_AllowFrameReordering: @NO,
    (id)kVTCompressionPropertyKey_ExpectedFrameRate: @60,
    (id)kVTCompressionPropertyKey_MaxKeyFrameInterval: @60,
    (id)kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration: @1,
    (id)kVTCompressionPropertyKey_AverageBitRate: @(bitrate),
    (id)kVTCompressionPropertyKey_DataRateLimits: @[@(bitrate * 1.5 / 8), @1],
    (id)kVTCompressionPropertyKey_ColorPrimaries: (id)kCVImageBufferColorPrimaries_ITU_R_2020,
    (id)kVTCompressionPropertyKey_TransferFunction: (id)kCVImageBufferTransferFunction_ITU_R_2100_HLG,
    (id)kVTCompressionPropertyKey_YCbCrMatrix: (id)kCVImageBufferYCbCrMatrix_ITU_R_2020,
  };
  for (NSString* k in props) {
    st = VTSessionSetProperty(vt, (__bridge CFStringRef)k, (__bridge CFTypeRef)props[k]);
    if (st) return makeError(k == (id)kVTCompressionPropertyKey_ProfileLevel ? RecorderErrorNoHardware : RecorderErrorSetup, st);
  }
  if ((st = VTCompressionSessionPrepareToEncodeFrames(vt))) return makeError(RecorderErrorSetup, st);
  CFBooleanRef hw = NULL;
  VTSessionCopyProperty(vt, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, NULL, &hw);
  hardware = hw == kCFBooleanTrue;
  if (hw) CFRelease(hw);
  if (!hardware) return makeError(RecorderErrorNoHardware);
  pool = VTCompressionSessionGetPixelBufferPool(vt);
  if (!pool) return makeError(RecorderErrorSetup);
  poolAux = @{(id)kCVPixelBufferPoolAllocationThresholdKey: @(kPoolFrames)};

  writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&e];
  if (!writer) return makeError(RecorderErrorSetup, 0, e);
  writer.shouldOptimizeForNetworkUse = YES;   // moov before mdat

  if (hasAudio) {
    AudioStreamBasicDescription asbd = {
      .mSampleRate = sampleRate, .mFormatID = kAudioFormatLinearPCM,
      .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
      .mBytesPerPacket = 8, .mFramesPerPacket = 1, .mBytesPerFrame = 8,
      .mChannelsPerFrame = 2, .mBitsPerChannel = 32};
    if ((st = CMAudioFormatDescriptionCreate(NULL, &asbd, 0, NULL, 0, NULL, NULL, &audioFormat)))
      return makeError(RecorderErrorSetup, st);
    aIn = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio outputSettings:@{
      AVFormatIDKey: @(kAudioFormatMPEG4AAC), AVNumberOfChannelsKey: @2,
      AVSampleRateKey: @(sampleRate), AVEncoderBitRateKey: @160000}];
    aIn.expectsMediaDataInRealTime = YES;
    ring = new AudioRing();
    ring->rate = sampleRate; ring->ticksPerSec = ticksPerSecond();
  }

  encodeQ = dispatch_queue_create("recorder.encode", DISPATCH_QUEUE_SERIAL);
  writerQ = dispatch_queue_create("recorder.writer", DISPATCH_QUEUE_SERIAL);
  vPending = [NSMutableArray array]; aPending = [NSMutableArray array];
  lastSlot = lastEncodedSlot = -1;
  timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, writerQ);
  dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, 25 * NSEC_PER_MSEC, NSEC_PER_MSEC);
  __weak FNRecorder* weak = self;
  dispatch_source_set_event_handler(timer, ^{ [weak pump]; });
  dispatch_resume(timer);
  startSec = mach_absolute_time() / ticksPerSecond();
  return nil;
}

- (void)encode:(CVPixelBufferRef)pb slot:(int64_t)slot ok:(bool)ok {
#ifdef FX_TEST
  if (int ms = gSlowMs.load()) usleep(ms * 1000);
#endif
  if (!ok || failed || slot <= lastEncodedSlot) {
    if (!failed) dropped++;
    inFlight--; CVPixelBufferRelease(pb); return;
  }
  lastEncodedSlot = slot;
  CVBufferSetAttachment(pb, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_2100_HLG, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferChromaLocationTopFieldKey, kCVImageBufferChromaLocation_Left, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferChromaLocationBottomFieldKey, kCVImageBufferChromaLocation_Left, kCVAttachmentMode_ShouldPropagate);
  OSStatus st = VTCompressionSessionEncodeFrame(vt, pb, CMTimeMake(slot, 60), CMTimeMake(1, 60), NULL, NULL, NULL);
  CVPixelBufferRelease(pb);
  if (st) { inFlight--; [self fail:makeError(RecorderErrorEncode, st)]; }
}

- (void)drainAudio {
  if (!ring) return;
  uint64_t tail = ring->tail.load(std::memory_order_relaxed), head = ring->head.load(std::memory_order_acquire);
  for (; tail != head; tail++) {
    AudioSlot& s = ring->slots[tail % kSlots];
    // Keep samples contiguous; re-anchor to host time only after a real gap.
    int64_t at = llround((s.host / ticksPerSecond() - startSec) * sampleRate);
    if (!audioAnchored || at > audioNext + sampleRate * 0.03) { audioNext = at; audioAnchored = true; }
    int64_t pos = audioNext; audioNext += s.frames;
    if (pos < 0) continue;
    CMBlockBufferRef block = NULL; CMSampleBufferRef sb = NULL;
    size_t bytes = s.frames * 8;
    if (!CMBlockBufferCreateWithMemoryBlock(NULL, NULL, bytes, NULL, NULL, 0, bytes, kCMBlockBufferAssureMemoryNowFlag, &block)) {
      CMBlockBufferReplaceDataBytes(s.pcm, block, 0, bytes);
      CMAudioSampleBufferCreateReadyWithPacketDescriptions(NULL, block, audioFormat, s.frames,
          CMTimeMake(pos, (int32_t)sampleRate), NULL, &sb);
      CFRelease(block);
    }
    if (sb) [aPending addObject:(__bridge_transfer id)sb];
  }
  ring->tail.store(tail, std::memory_order_release);
  while (aPending.count > kMaxAudioPending) {
    audioDropped += CMSampleBufferGetNumSamples((__bridge CMSampleBufferRef)aPending[0]);
    [aPending removeObjectAtIndex:0];
  }
}

- (void)startWriter:(CMSampleBufferRef)first {
  writerStarted = true;
  vIn = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:nil
                                     sourceFormatHint:CMSampleBufferGetFormatDescription(first)];
  vIn.expectsMediaDataInRealTime = YES;
  if (![writer canAddInput:vIn] || (aIn && ![writer canAddInput:aIn])) { [self fail:makeError(RecorderErrorWrite)]; return; }
  [writer addInput:vIn];
  if (aIn) [writer addInput:aIn];
  if (![writer startWriting]) { [self fail:makeError(RecorderErrorWrite, 0, writer.error)]; return; }
  firstPts = CMSampleBufferGetPresentationTimeStamp(first);
  [writer startSessionAtSourceTime:firstPts];
}

// writerQ: move whatever the inputs will take, never waiting for them.
- (void)pump {
  if (finished) return;
  [self drainAudio];
  if (!writerStarted && vPending.count && !failed) [self startWriter:(__bridge CMSampleBufferRef)vPending[0]];
  if (writerStarted && !failed) {
    while (vPending.count && vIn.readyForMoreMediaData) {
      CMSampleBufferRef sb = (__bridge CMSampleBufferRef)vPending[0];
      if (![vIn appendSampleBuffer:sb]) { [self fail:makeError(RecorderErrorWrite, 0, writer.error)]; break; }
      lastPts = CMSampleBufferGetPresentationTimeStamp(sb);
      [vPending removeObjectAtIndex:0]; written++; inFlight--;
    }
    while (aIn && aPending.count && aIn.readyForMoreMediaData && !failed) {
      CMSampleBufferRef sb = (__bridge CMSampleBufferRef)aPending[0];
      CMTime end = CMTimeAdd(CMSampleBufferGetPresentationTimeStamp(sb), CMSampleBufferGetDuration(sb));
      if (CMTimeCompare(end, firstPts) > 0 && ![aIn appendSampleBuffer:sb]) {
        [self fail:makeError(RecorderErrorWrite, 0, writer.error)]; break;
      }
      [aPending removeObjectAtIndex:0];
    }
  }
  if (finishing && (failed || !writerStarted || (!vPending.count && !aPending.count) ||
                    CACurrentMediaTime() > finishDeadline)) [self finish];
}

- (void)finish {
  finished = true;
  dispatch_source_cancel(timer);
  FNRecorder* hold = keepAlive; keepAlive = nil; (void)hold;
  VTCompressionSessionInvalidate(vt);
  auto cb = done;
  int64_t drops = dropped.load();
  auto reply = ^(NSURL* u, double sec, NSError* e) {
    dispatch_async(dispatch_get_main_queue(), ^{ cb(u, sec, drops, e); });
  };
  if (!failed && !writerStarted) { self.error = makeError(RecorderErrorNoFrames); failed = true; }
  if (failed) {
    if (writerStarted && writer.status == AVAssetWriterStatusWriting) [writer cancelWriting];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    reply(nil, 0, self.error);
    return;
  }
  CMTime end = CMTimeAdd(lastPts, CMTimeMake(1, 60));
  [writer endSessionAtSourceTime:end];
  double seconds = CMTimeGetSeconds(CMTimeSubtract(end, firstPts));
  AVAssetWriter* w = writer; NSURL* u = url;
  [w finishWritingWithCompletionHandler:^{
    if (w.status == AVAssetWriterStatusCompleted) reply(u, seconds, nil);
    else reply(nil, 0, makeError(RecorderErrorWrite, 0, w.error));
  }];
}
@end

static void vtOutput(void* ref, void*, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sb) {
  FNRecorder* r = (__bridge FNRecorder*)ref;
  if (status) { r->inFlight--; [r fail:makeError(RecorderErrorEncode, status)]; return; }
  if (!sb || (flags & kVTEncodeInfo_FrameDropped)) { r->inFlight--; r->dropped++; return; }
  CFRetain(sb);
  dispatch_async(r->writerQ, ^{
    [r->vPending addObject:(__bridge_transfer id)sb];
    [r pump];
  });
}

bool recorder_start(id<MTLDevice> device, int width, int height, NSURL* url, bool hasAudio,
                    double sampleRate, void (^onFailure)(NSError*), NSError** error) {
  NSError* e = nil;
  if (current()) e = makeError(RecorderErrorBusy);
  FNRecorder* r = [FNRecorder new];
  if (!e) {
    r->url = url; r->hasAudio = hasAudio; r->sampleRate = sampleRate; r->onFailure = onFailure;
    e = [r setup:device width:width height:height];
  }
  if (e) { if (error) *error = e; return false; }
  os_unfair_lock_lock(&gLock);
  gRec = r;
  os_unfair_lock_unlock(&gLock);
  gRing.store(r->ring, std::memory_order_release);
  return true;
}

static int64_t slotAt(FNRecorder* r, uint64_t hostTime) {
  const double t = hostTime / ticksPerSecond() - r->startSec;
  return (int64_t)floor(t * 60 + 0.25);   // nearest 60 fps slot, tolerant of jitter
}

bool recorder_wants(uint64_t hostTime) {
  FNRecorder* r = current();
  if (!r || r->stopping || r->failed) return false;
  const int64_t slot = slotAt(r, hostTime);
  return slot >= 0 && slot > r->lastSlot;
}

void recorder_append(id<MTLCommandBuffer> cmd, id<MTLTexture> frame, uint64_t hostTime) {
  FNRecorder* r = current();
  if (!r || r->stopping || r->failed) return;
  const int64_t slot = slotAt(r, hostTime);
  if (slot < 0 || slot <= r->lastSlot) return;
  r->lastSlot = slot;
  if (r->inFlight >= kMaxInFlight) { r->dropped++; return; }
  CVPixelBufferRef pb = NULL;
  if (CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(NULL, r->pool, (__bridge CFDictionaryRef)r->poolAux, &pb)) {
    r->dropped++; return;
  }
  CVMetalTextureRef y = NULL, c = NULL;
  CVMetalTextureCacheCreateTextureFromImage(NULL, r->texCache, pb, NULL, MTLPixelFormatR16Unorm, r->W, r->H, 0, &y);
  CVMetalTextureCacheCreateTextureFromImage(NULL, r->texCache, pb, NULL, MTLPixelFormatRG16Unorm, r->W / 2, r->H / 2, 1, &c);
  if (!y || !c) {
    if (y) CFRelease(y);
    if (c) CFRelease(c);
    CVPixelBufferRelease(pb); r->dropped++; return;
  }
  struct { float scale[2]; uint32_t half[2]; } p = {{r->scaleX, r->scaleY}, {(uint32_t)r->W / 2, (uint32_t)r->H / 2}};
  // Its own buffer, queued straight after the frame's: the frame's completion (and its GPU
  // time) does not wait for the conversion, and the capture texture is read before the next frame.
  [cmd enqueue];
  id<MTLCommandBuffer> conv = [cmd.commandQueue commandBuffer];
  [conv enqueue];
  id<MTLComputeCommandEncoder> enc = [conv computeCommandEncoder];
  [enc setComputePipelineState:r->pipe];
  [enc setTexture:frame atIndex:0];
  [enc setTexture:CVMetalTextureGetTexture(y) atIndex:1];
  [enc setTexture:CVMetalTextureGetTexture(c) atIndex:2];
  [enc setBytes:&p length:sizeof p atIndex:0];
  [enc dispatchThreads:MTLSizeMake(r->W / 2, r->H / 2, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
  [enc endEncoding];
  r->inFlight++; r->gpuPending++;
  [conv addCompletedHandler:^(id<MTLCommandBuffer> b) {
    CFRelease(y); CFRelease(c);
    bool ok = b.status == MTLCommandBufferStatusCompleted;
    dispatch_async(r->encodeQ, ^{ [r encode:pb slot:slot ok:ok]; r->gpuPending--; });
  }];
  [conv commit];
  CVMetalTextureCacheFlush(r->texCache, 0);
}

void recorder_append_audio(const float* left, const float* right, int frames, uint64_t hostTime) {
  AudioRing* g = gRing.load(std::memory_order_acquire);
  if (!g || !g->open.load(std::memory_order_relaxed)) return;
  if (!right) right = left;
  for (int off = 0; off < frames; off += kSlotFrames) {
    int n = std::min(kSlotFrames, frames - off);
    uint64_t head = g->head.load(std::memory_order_relaxed);
    if (head - g->tail.load(std::memory_order_acquire) >= kSlots) { g->dropped += n; continue; }
    AudioSlot& s = g->slots[head % kSlots];
    s.host = hostTime + (uint64_t)(off / g->rate * g->ticksPerSec);
    s.frames = n;
    for (int i = 0; i < n; i++) { s.pcm[2 * i] = left[off + i]; s.pcm[2 * i + 1] = right[off + i]; }
    g->head.store(head + 1, std::memory_order_release);
  }
}

void recorder_stop(void (^done)(NSURL*, double, int64_t, NSError*)) {
  os_unfair_lock_lock(&gLock);
  FNRecorder* r = gRec;
  gRec = nil;
  os_unfair_lock_unlock(&gLock);
  if (!r) {
    dispatch_async(dispatch_get_main_queue(), ^{ done(nil, 0, 0, makeError(RecorderErrorNoFrames)); });
    return;
  }
  r->stopping = true;
  // The film's audio tap has ended (audio_film_end) before this: nothing reads gRing now.
  if (r->ring) r->ring->open = false;
  gRing.store(nullptr, std::memory_order_release);
  r->keepAlive = r;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    // Frames still on the GPU reach encodeQ shortly; wait for them, then flush the encoder.
    for (int i = 0; i < 2000 && r->gpuPending > 0; i++) usleep(1000);
    dispatch_async(r->encodeQ, ^{
      VTCompressionSessionCompleteFrames(r->vt, kCMTimeInvalid);
      dispatch_async(r->writerQ, ^{
        r->done = done;
        r->finishing = true;
        r->finishDeadline = CACurrentMediaTime() + 3;
        [r pump];
      });
    });
  });
}

bool recorder_active() { return current() != nil; }

#ifdef FX_TEST
RecorderStats recorder_stats() {
  FNRecorder* r = current();
  if (!r) return {};
  return {r->W, r->H, r->bitrate, r->hardware, r->written.load(), r->dropped.load(), r->audioDropped.load() + (r->ring ? r->ring->dropped.load() : 0)};
}

void recorder_test_slow_consumer(int ms) { gSlowMs = ms; }
#endif
