// Recorder harness. Build:
//   clang++ -std=c++20 -O2 -fobjc-arc -DFX_TEST mac/platform/recorder.mm mac/platform/test_recorder.mm \
//     -framework Foundation -framework Metal -framework AVFoundation -framework VideoToolbox \
//     -framework CoreMedia -framework CoreVideo -framework CoreImage -framework CoreGraphics -framework QuartzCore \
//     -o mac/build/test_recorder
// Run: mac/build/test_recorder [film|mem|slow|bench|impossible]   (no argument runs all)
#import "recorder.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <VideoToolbox/VideoToolbox.h>
#import <mach/mach.h>
#import <mach/mach_time.h>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

static const int kW = 3024, kH = 1964, kPatch = 220;
struct Patch { float r, g, b; const char* name; };
static const Patch kPatches[] = {
  {0.18f, 0.18f, 0.18f, "grey 0.18"}, {1, 1, 1, "white 1.0"}, {2, 2, 2, "white 2.0"}, {4, 4, 4, "white 4.0"},
  {1, 0, 0, "red"}, {0, 1, 0, "green"}, {0, 0, 1, "blue"}, {1, 1, 0, "yellow"}, {0, 1, 1, "cyan"},
  {1, 0, 1, "magenta"}, {2, 0.5f, 0.1f, "orange 2x"}, {0.5f, 0.25f, 0.75f, "lilac"},
};
static const int kNP = sizeof kPatches / sizeof *kPatches;
static int patchX(int i) { return 160 + (i % 6) * (kPatch + 240); }
static int patchY(int i) { return 400 + (i / 6) * (kPatch + 500); }

static NSString* const kSynth = @R"(
#include <metal_stdlib>
using namespace metal;
struct Patch { float4 rgb_xy; };
kernel void synth(texture2d<half, access::write> out [[texture(0)]], constant float& t [[buffer(0)]],
                  constant float4* patches [[buffer(1)]], constant int& n [[buffer(2)]],
                  uint2 g [[thread_position_in_grid]]) {
  float2 p = float2(g);
  float a = p.x * 0.004 + t * 0.7, b = p.y * 0.003 - t * 0.5;
  float3 c = 0.5 + 0.45 * cos(float3(0, 2.1, 4.2) + a + sin(b));
  c *= 0.6 + 0.4 * sin(p.x * 0.05 + t * 3) * sin(p.y * 0.043 - t * 2);    // fine moving detail
  c *= 0.8 + 1.6 * smoothstep(0.8, 1.0, sin(length(p - float2(1500, 980)) * 0.01 - t * 4));
  if (t >= 7.0 && t < 7.1) c = 0;                                          // sync marker
  for (int i = 0; i < n; i++) {
    float4 q = patches[2 * i], r = patches[2 * i + 1];
    if (p.x >= r.x && p.x < r.x + r.z && p.y >= r.y && p.y < r.y + r.z) c = q.rgb;
  }
  out.write(half4(half3(c), 1), g);
}
)";

static uint64_t phys() {
  task_vm_info_data_t info; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
  task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n);
  return info.phys_footprint;
}
static double tps() { mach_timebase_info_data_t tb; mach_timebase_info(&tb); return 1e9 * tb.denom / tb.numer; }

struct Run { NSURL* url; double seconds; int64_t dropped, written; NSError* error; double wall; std::vector<double> mem; double appendMax, appendSum; int frames; };

// paced: frames at 120 Hz in real time; otherwise as fast as the GPU allows with a synthetic clock.
static Run record(id<MTLDevice> dev, NSString* name, double seconds, bool audio, bool paced, int slowMs) {
  Run run{};
  id<MTLCommandQueue> q = [dev newCommandQueue];
  NSError* e = nil;
  id<MTLLibrary> lib = [dev newLibraryWithSource:kSynth options:nil error:&e];
  id<MTLComputePipelineState> pipe = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"synth"] error:&e];
  MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:kW height:kH mipmapped:NO];
  td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; td.storageMode = MTLStorageModePrivate;
  id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
  std::vector<float> pd;
  for (int i = 0; i < kNP; i++) { auto& p = kPatches[i]; pd.insert(pd.end(), {p.r, p.g, p.b, 0, (float)patchX(i), (float)patchY(i), (float)kPatch, 0}); }

  NSString* path = [name hasPrefix:@"/"] ? name : [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingFormat:@"/mac/build/%@.mp4", name];
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  run.url = [NSURL fileURLWithPath:path];
  recorder_test_slow_consumer(slowMs);
  if (!recorder_start(dev, kW, kH, run.url, audio, 48000, ^(NSError* err) { NSLog(@"failure: %@", err); }, &e)) {
    run.error = e; return run;
  }
  RecorderStats st = recorder_stats();
  printf("  encoding %dx%d at %.1f Mb/s, hardware=%d\n", st.width, st.height, st.bitsPerSecond / 1e6, st.hardware);

  std::atomic<bool> stop{false};
  double T = tps();
  uint64_t t0 = mach_absolute_time() + (uint64_t)(0.05 * T);
  std::thread tone;
  if (audio) tone = std::thread([&] {
    float l[512], r[512]; int64_t n = 0;
    for (int k = 0; !stop; k++) {
      uint64_t at = t0 + (uint64_t)(k * 512 / 48000.0 * T);
      mach_wait_until(at);
      for (int i = 0; i < 512; i++, n++)   // silent at 7.0..7.1 s, the same moment the picture goes black
        l[i] = r[i] = n >= 336000 && n < 340800 ? 0 : 0.25f * sinf(2 * M_PI * 440 * n / 48000.0);
      recorder_append_audio(l, r, 512, at);
    }
  });
  std::thread memw([&] { while (!stop) { run.mem.push_back(phys() / 1048576.0); usleep(1000000); } });

  double wall0 = CACurrentMediaTime();
  int frames = run.frames = (int)(seconds * (paced ? 120 : 60));
  for (int k = 0; k < frames; k++) @autoreleasepool {
    uint64_t at = t0 + (uint64_t)(k / (paced ? 120.0 : 60.0) * T);
    if (paced) mach_wait_until(at);
    else while (true) {   // closed loop: stay a few frames ahead of the encoder
      RecorderStats s = recorder_stats();
      if (k - s.framesWritten - s.framesDropped < 6) break;
      usleep(200);
    }
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    float t = k / (paced ? 120.f : 60.f); int n = kNP;
    [enc setComputePipelineState:pipe]; [enc setTexture:tex atIndex:0];
    [enc setBytes:&t length:4 atIndex:0]; [enc setBytes:pd.data() length:pd.size() * 4 atIndex:1]; [enc setBytes:&n length:4 atIndex:2];
    [enc dispatchThreads:MTLSizeMake(kW, kH, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [enc endEncoding];
    double a0 = CACurrentMediaTime();
    recorder_append(cb, tex, at);
    double a = CACurrentMediaTime() - a0;
    run.appendMax = std::max(run.appendMax, a); run.appendSum += a;
    [cb commit];
    if (!paced) [cb waitUntilScheduled];
  }
  run.written = recorder_stats().framesWritten;
  stop = true;
  if (tone.joinable()) tone.join();
  memw.join();
  __block bool done = false;
  Run* rp = &run;
  recorder_stop(^(NSURL* u, double s, int64_t d, NSError* err) { rp->url = u; rp->seconds = s; rp->dropped = d; rp->error = err; done = true; });
  while (!done) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
  run.wall = CACurrentMediaTime() - wall0;
  return run;
}

static NSString* fourcc(FourCharCode c) { return [NSString stringWithFormat:@"%c%c%c%c", c >> 24, c >> 16 & 255, c >> 8 & 255, c & 255]; }

static void topBoxes(NSURL* url) {
  NSData* d = [NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:nil];
  const uint8_t* b = (const uint8_t*)d.bytes; uint64_t off = 0;
  printf("  top-level boxes:");
  while (off + 8 <= d.length) {
    uint64_t size = (uint64_t)b[off] << 24 | b[off + 1] << 16 | b[off + 2] << 8 | b[off + 3];
    char type[5] = {(char)b[off + 4], (char)b[off + 5], (char)b[off + 6], (char)b[off + 7], 0};
    if (size == 1) { size = 0; for (int i = 0; i < 8; i++) size = size << 8 | b[off + 8 + i]; }
    printf(" %s(%llu)", type, size);
    if (size < 8) break;
    off += size;
  }
  printf("\n");
}

#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static void inspect(NSURL* url) {
  AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
  printf("  duration %.3f s\n", CMTimeGetSeconds(asset.duration));
  AVAssetTrack* v = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
  CMFormatDescriptionRef fd = (__bridge CMFormatDescriptionRef)v.formatDescriptions.firstObject;
  NSDictionary* ext = (__bridge NSDictionary*)CMFormatDescriptionGetExtensions(fd);
  NSData* hvcC = ext[(id)kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms][@"hvcC"];
  const uint8_t* h = (const uint8_t*)hvcC.bytes;
  printf("  video %s %dx%d, profile_idc %d (2 = Main10), chroma_format %d, luma %d-bit, chroma %d-bit, %.2f fps, %lld frames, %.1f Mb/s\n",
         fourcc(CMFormatDescriptionGetMediaSubType(fd)).UTF8String, (int)v.naturalSize.width, (int)v.naturalSize.height,
         h[1] & 31, h[16] & 3, (h[17] & 7) + 8, (h[18] & 7) + 8, v.nominalFrameRate,
         (long long)llround(CMTimeGetSeconds(v.timeRange.duration) * v.nominalFrameRate), v.estimatedDataRate / 1e6);
  printf("  colour: primaries %s, transfer %s, matrix %s, chroma location %s, full range %s\n",
         [ext[(id)kCMFormatDescriptionExtension_ColorPrimaries] UTF8String], [ext[(id)kCMFormatDescriptionExtension_TransferFunction] UTF8String],
         [ext[(id)kCMFormatDescriptionExtension_YCbCrMatrix] UTF8String], [ext[(id)kCMFormatDescriptionExtension_ChromaLocationTopField] UTF8String],
         [[ext[(id)kCMFormatDescriptionExtension_FullRangeVideo] description] UTF8String]);
  for (AVAssetTrack* a in [asset tracksWithMediaType:AVMediaTypeAudio]) {
    const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(
        (__bridge CMAudioFormatDescriptionRef)a.formatDescriptions.firstObject);
    printf("  audio %s %.0f Hz %u ch, %.0f kb/s, %.3f s\n", fourcc(asbd->mFormatID).UTF8String, asbd->mSampleRate,
           asbd->mChannelsPerFrame, a.estimatedDataRate / 1e3, CMTimeGetSeconds(a.timeRange.duration));
  }
  topBoxes(url);
}

// Decode one frame with the hardware decoder and let Core Image interpret its HLG tags
// into extended linear sRGB; compare the patch centres with what went in.
static void roundTrip(NSURL* url) {
  AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
  AVAssetTrack* v = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
  AVAssetReader* rd = [AVAssetReader assetReaderWithAsset:asset error:nil];
  rd.timeRange = CMTimeRangeMake(CMTimeMake(5, 1), CMTimeMake(1, 1));
  AVAssetReaderTrackOutput* out = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:v outputSettings:@{
    (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)}];
  [rd addOutput:out]; [rd startReading];
  CMSampleBufferRef sb = [out copyNextSampleBuffer];
  CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sb);
  printf("  decoded frame at %.3f s, transfer attachment %s\n", CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb)),
         [(__bridge NSString*)CVBufferCopyAttachment(pb, kCVImageBufferTransferFunctionKey, NULL) UTF8String]);
  CGColorSpaceRef lin = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
  CIContext* ctx = [CIContext contextWithOptions:@{kCIContextWorkingColorSpace: (__bridge id)lin, kCIContextWorkingFormat: @(kCIFormatRGBAf)}];
  CIImage* im = [CIImage imageWithCVPixelBuffer:pb options:@{kCIImageExpandToHDR: @YES}];
  const int s = 100;
  std::vector<float> px(s * s * 4);
  printf("  %-10s %-22s %-22s %s\n", "patch", "in (linear sRGB)", "out (linear sRGB)", "ratio out/in");
  for (int i = 0; i < kNP; i++) {
    int x = patchX(i) + (kPatch - s) / 2, y = patchY(i) + (kPatch - s) / 2;
    // CI's origin is bottom left.
    [ctx render:im toBitmap:px.data() rowBytes:s * 16 bounds:CGRectMake(x, kH - y - s, s, s) format:kCIFormatRGBAf colorSpace:lin];
    double m[3] = {0, 0, 0};
    for (int k = 0; k < s * s; k++) for (int c = 0; c < 3; c++) m[c] += px[k * 4 + c] / (s * s);
    auto& p = kPatches[i];
    double peak = std::max({p.r, p.g, p.b}), outPeak = std::max({m[0], m[1], m[2]});
    printf("  %-10s %.3f %.3f %.3f      %.3f %.3f %.3f      %.3f\n", p.name, p.r, p.g, p.b, m[0], m[1], m[2], outPeak / peak);
  }
  CFRelease(sb); CGColorSpaceRelease(lin);
}

// When the picture goes black and when the sound goes silent, in the film's own time.
static void syncCheck(NSURL* url) {
  AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
  AVAssetReader* rd = [AVAssetReader assetReaderWithAsset:asset error:nil];
  rd.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(6.5, 600), CMTimeMake(1, 1));
  AVAssetReaderTrackOutput* vo = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:[asset tracksWithMediaType:AVMediaTypeVideo][0]
      outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)}];
  AVAssetReaderTrackOutput* ao = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:[asset tracksWithMediaType:AVMediaTypeAudio][0]
      outputSettings:@{AVFormatIDKey: @(kAudioFormatLinearPCM), AVLinearPCMBitDepthKey: @32, AVLinearPCMIsFloatKey: @YES,
                       AVLinearPCMIsNonInterleaved: @NO, AVLinearPCMIsBigEndianKey: @NO}];
  [rd addOutput:vo]; [rd addOutput:ao]; [rd startReading];
  double vBlack = -1, aSilent = -1;
  while (CMSampleBufferRef sb = [vo copyNextSampleBuffer]) {
    CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sb);
    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    uint16_t yv = ((uint16_t*)CVPixelBufferGetBaseAddressOfPlane(pb, 0))[40 * CVPixelBufferGetBytesPerRowOfPlane(pb, 0) / 2 + 40] >> 6;
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    if (vBlack < 0 && yv < 80) vBlack = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
    CFRelease(sb);
  }
  int quiet = 0;
  while (CMSampleBufferRef sb = [ao copyNextSampleBuffer]) {
    double t0 = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb); size_t len = 0; char* data = NULL;
    CMBlockBufferGetDataPointer(bb, 0, NULL, &len, &data);
    const float* f = (const float*)data;
    for (size_t i = 0; i < len / 8 && aSilent < 0; i++) {
      quiet = fabsf(f[2 * i]) < 0.01f ? quiet + 1 : 0;
      if (quiet == 48) aSilent = t0 + (i - 47) / 48000.0;   // first of 1 ms of silence
    }
    CFRelease(sb);
  }
  printf("  sync marker: picture black at %.4f s, sound silent at %.4f s, offset %+.1f ms\n", vBlack, aSilent, (aSilent - vBlack) * 1000);
}

static void printMem(const Run& r) {
  if (r.mem.empty()) return;
  double lo = 1e9, hi = 0;
  for (size_t i = 3; i < r.mem.size(); i++) { lo = std::min(lo, r.mem[i]); hi = std::max(hi, r.mem[i]); }
  printf("  footprint MB per second:");
  for (double m : r.mem) printf(" %.0f", m);
  printf("\n  after warm-up: min %.0f MB, max %.0f MB\n", lo, hi);
}

static void report(const char* label, const Run& r) {
  if (r.error) { printf("%s: ERROR %s\n", label, r.error.description.UTF8String); return; }
  printf("%s: %s, %.2f s of film, %lld frames written, %lld dropped, wall %.2f s, recorder_append mean %.0f us max %.0f us\n", label,
         r.url.lastPathComponent.UTF8String, r.seconds, (long long)r.written, (long long)r.dropped, r.wall,
         r.appendSum / r.frames * 1e6, r.appendMax * 1e6);
}

int main(int argc, char** argv) {
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    NSString* mode = argc > 1 ? @(argv[1]) : @"all";
    bool all = [mode isEqual:@"all"];
    if (all || [mode isEqual:@"film"]) {
      Run r = record(dev, @"test_film", 10, true, true, 0);
      report("film (10 s, 120 Hz, tone)", r);
      if (!r.error) { inspect(r.url); roundTrip(r.url); syncCheck(r.url); }
      Run s = record(dev, @"test_silent", 3, false, true, 0);
      report("silent (3 s, no audio)", s);
      if (!s.error) inspect(s.url);
    }
    if (all || [mode isEqual:@"mem"]) { Run r = record(dev, @"test_mem", 30, true, true, 0); report("memory (30 s)", r); printMem(r); }
    if (all || [mode isEqual:@"slow"]) { Run r = record(dev, @"test_slow", argc > 2 ? atof(argv[2]) : 30, true, true, 40); report("slow consumer (40 ms per frame)", r); printMem(r); }
    if (all || [mode isEqual:@"bench"]) {
      Run r = record(dev, @"test_bench", 20, false, false, 0);
      report("bench (unpaced)", r);
      if (!r.error) printf("  throughput %.1f frames/s encoded at %dx%d\n", r.written / r.wall, kW & ~1, kH & ~1);
    }
    if (all || [mode isEqual:@"repeat"]) {   // nothing should survive a recording
      printf("repeat: footprint after each 4 s recording:");
      for (int i = 0; i < 5; i++) @autoreleasepool { record(dev, @"test_repeat", 4, true, true, 0); printf(" %.0f MB", phys() / 1048576.0); fflush(stdout); }
      printf("\n");
    }
    if (all || [mode isEqual:@"fail"]) {   // the file cannot be created once frames arrive
      NSString* dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"recorder_fail"];
      [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions: @0500} error:nil];
      Run r = record(dev, [dir stringByAppendingPathComponent:@"x.mp4"], 2, true, true, 0);
      printf("fail: error code %ld, %lld frames written\n", (long)r.error.code, (long long)r.written);
    }
    if (all || [mode isEqual:@"impossible"]) {
      NSError* e = nil;
      NSURL* u = [NSURL fileURLWithPath:@"mac/build/test_impossible.mp4"];
      bool ok = recorder_start(dev, 16384, 16384, u, false, 48000, nil, &e);
      printf("impossible 16384x16384: started=%d error domain=%s code=%ld status=%s\n", ok, e.domain.UTF8String, (long)e.code,
             [[e.userInfo[@"status"] description] UTF8String]);
      if (ok) recorder_stop(^(NSURL*, double, int64_t, NSError*) {});
      // The same size without the requirement: VideoToolbox quietly falls back to software.
      VTCompressionSessionRef sw = NULL;
      OSStatus st = VTCompressionSessionCreate(NULL, 16384, 16384, kCMVideoCodecType_HEVC, NULL, NULL, NULL, NULL, NULL, &sw);
      CFBooleanRef hw = NULL;
      if (!st) VTSessionCopyProperty(sw, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, NULL, &hw);
      printf("same size, hardware not required: create=%d hardware=%d\n", (int)st, hw == kCFBooleanTrue);
      if (sw) { VTCompressionSessionInvalidate(sw); CFRelease(sw); }
      // The encoder's preferred input format, when the session is free to choose.
      VTCompressionSessionRef vt = NULL;
      VTCompressionSessionCreate(NULL, kW & ~1, kH & ~1, kCMVideoCodecType_HEVC,
          (__bridge CFDictionaryRef)@{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES}, NULL, NULL, NULL, NULL, &vt);
      VTSessionSetProperty(vt, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_HEVC_Main10_AutoLevel);
      VTCompressionSessionPrepareToEncodeFrames(vt);
      NSDictionary* attrs = (__bridge NSDictionary*)CVPixelBufferPoolGetPixelBufferAttributes(VTCompressionSessionGetPixelBufferPool(vt));
      id fmt = attrs[(id)kCVPixelBufferPixelFormatTypeKey];
      if ([fmt isKindOfClass:NSArray.class]) fmt = [fmt firstObject];
      printf("encoder's native Main10 input: %s\n", fourcc([fmt unsignedIntValue]).UTF8String);
      VTCompressionSessionInvalidate(vt); CFRelease(vt);
    }
  }
  return 0;
}
