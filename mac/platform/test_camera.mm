// test_camera: checks the Vision -> MediaPipe conversion and measures detection cost.
//   test_camera          unit checks + benchmark (no camera needed)
//   test_camera --live   also opens the camera for 5 s and prints landmark summaries
#import "camera.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreText/CoreText.h>
#import <CoreML/CoreML.h>
#import <QuartzCore/QuartzCore.h>
#include <sys/resource.h>
#include <mach/mach_time.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace camera_detail;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static double ms() {
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return mach_absolute_time() * tb.numer / (double)tb.denom / 1e6;
}
static double cpuMs() {
  rusage u; getrusage(RUSAGE_SELF, &u);
  return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}

// A 1280x720 BGRA IOSurface buffer with an emoji drawn in the middle (a real hand
// shape Vision recognises), optionally mirrored left-right.
static CVPixelBufferRef render(NSString* s, bool mirror) {
  const int W = 1280, H = 720;
  CVPixelBufferRef pb = nullptr;
  NSDictionary* a = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES };
  CVPixelBufferCreate(nil, W, H, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)a, &pb);
  CVPixelBufferLockBaseAddress(pb, 0);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef c = CGBitmapContextCreate(CVPixelBufferGetBaseAddress(pb), W, H, 8, CVPixelBufferGetBytesPerRow(pb), cs,
                                         (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGContextSetRGBFillColor(c, 0.2, 0.3, 0.35, 1);
  CGContextFillRect(c, CGRectMake(0, 0, W, H));
  if (mirror) { CGContextTranslateCTM(c, W, 0); CGContextScaleCTM(c, -1, 1); }
  CTFontRef f = CTFontCreateWithName(CFSTR("Apple Color Emoji"), 520, nil);
  NSAttributedString* as = [[NSAttributedString alloc] initWithString:s attributes:@{ (id)kCTFontAttributeName: (__bridge id)f }];
  CTLineRef l = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)as);
  CGRect b = CTLineGetImageBounds(l, c);
  CGContextSetTextPosition(c, (W - b.size.width) / 2 - b.origin.x, (H - b.size.height) / 2 - b.origin.y);
  CTLineDraw(l, c);
  CFRelease(l); CFRelease(f); CGContextRelease(c); CGColorSpaceRelease(cs);
  CVPixelBufferUnlockBaseAddress(pb, 0);
  return pb;
}

static bool detectOne(CVPixelBufferRef pb, HandObs& out) {
  VNDetectHumanHandPoseRequest* r = [VNDetectHumanHandPoseRequest new];
  r.maximumHandCount = 1;
  VNImageRequestHandler* h = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pb orientation:kCGImagePropertyOrientationUp options:@{}];
  if (![h performRequests:@[r] error:nil] || !r.results.count) return false;
  VNHumanHandPoseObservation* ob = r.results.firstObject;
  NSDictionary* pts = [ob recognizedPointsForJointsGroupName:VNHumanHandPoseObservationJointsGroupNameAll error:nil];
  Joint j[21];
  for (int i = 0; i < 21; i++) {
    VNRecognizedPoint* p = pts[joint_name(i)];
    j[i] = {(float)p.location.x, (float)p.location.y, p.confidence};
  }
  HandMemory mem;
  return to_hand_obs(j, ob.chirality, ob.confidence, 0, mem, out);
}

static void unitChecks() {
  // 1. Index table: MediaPipe order, every Vision joint exactly once.
  NSArray* want = @[
    VNHumanHandPoseObservationJointNameWrist,
    VNHumanHandPoseObservationJointNameThumbCMC, VNHumanHandPoseObservationJointNameThumbMP, VNHumanHandPoseObservationJointNameThumbIP, VNHumanHandPoseObservationJointNameThumbTip,
    VNHumanHandPoseObservationJointNameIndexMCP, VNHumanHandPoseObservationJointNameIndexPIP, VNHumanHandPoseObservationJointNameIndexDIP, VNHumanHandPoseObservationJointNameIndexTip,
    VNHumanHandPoseObservationJointNameMiddleMCP, VNHumanHandPoseObservationJointNameMiddlePIP, VNHumanHandPoseObservationJointNameMiddleDIP, VNHumanHandPoseObservationJointNameMiddleTip,
    VNHumanHandPoseObservationJointNameRingMCP, VNHumanHandPoseObservationJointNameRingPIP, VNHumanHandPoseObservationJointNameRingDIP, VNHumanHandPoseObservationJointNameRingTip,
    VNHumanHandPoseObservationJointNameLittleMCP, VNHumanHandPoseObservationJointNameLittlePIP, VNHumanHandPoseObservationJointNameLittleDIP, VNHumanHandPoseObservationJointNameLittleTip];
  NSMutableSet* seen = [NSMutableSet set];
  for (int i = 0; i < 21; i++) { CHECK([joint_name(i) isEqualToString:want[i]]); [seen addObject:joint_name(i)]; }
  CHECK(seen.count == 21);
  NSArray* supported = [[VNDetectHumanHandPoseRequest new] supportedJointNamesAndReturnError:nil] ?: @[];
  for (id n in want) CHECK([supported containsObject:n]);

  // 2. y flip and pass-through x, score and side, with every joint confident.
  Joint j[21];
  for (int i = 0; i < 21; i++) j[i] = {0.01f * i + 0.1f, 0.02f * i + 0.05f, 0.9f};
  HandMemory mem; HandObs o;
  CHECK(to_hand_obs(j, VNChiralityRight, 0.8f, 1.0, mem, o));
  bool exact = true;
  for (int i = 0; i < 21; i++) exact &= std::fabs(o.x[i] - j[i].x) < 1e-6f && std::fabs(o.y[i] - (1 - j[i].y)) < 1e-6f;
  CHECK(exact);
  CHECK(o.score == 0.8f && !o.right);
  CHECK(mediapipe_right(VNChiralityLeft) && !mediapipe_right(VNChiralityRight));

  // 3. Low confidence: a weak tip with fresh memory follows the wrist from where it was.
  Joint k[21]; std::copy(j, j + 21, k);
  for (int i = 0; i < 21; i++) k[i].x += 0.05f;                  // whole hand moved right
  k[8].c = 0.1f; k[8].x = 0; k[8].y = 0;                         // index tip lost
  HandObs o2;
  CHECK(to_hand_obs(k, VNChiralityRight, 0.8f, 1.1, mem, o2));
  CHECK(std::fabs(o2.x[8] - (o.x[8] + 0.05f)) < 1e-5f && std::fabs(o2.y[8] - o.y[8]) < 1e-5f);
  //    ...and with stale memory folds onto its parent (finger reads as curled).
  HandMemory fresh; HandObs o4;
  Joint m[21]; std::copy(j, j + 21, m); m[7].c = m[8].c = 0.1f; m[5].c = 0.2f;
  CHECK(to_hand_obs(m, VNChiralityRight, 0.8f, 1.0, fresh, o4));
  CHECK(o4.x[5] == o4.x[0] && o4.x[8] == o4.x[6] && o4.y[7] == o4.y[6]);
  //    Without the wrist or middle MCP, or with most joints unsure, the hand is dropped.
  m[0].c = 0.1f; CHECK(!to_hand_obs(m, VNChiralityRight, 0.8f, 1.0, fresh, o4));
  std::copy(j, j + 21, m); m[9].c = 0.1f; CHECK(!to_hand_obs(m, VNChiralityRight, 0.8f, 1.0, fresh, o4));
  std::copy(j, j + 21, m); for (int i = 10; i < 21; i++) m[i].c = 0.1f; CHECK(!to_hand_obs(m, VNChiralityRight, 0.8f, 1.0, fresh, o4));

  // 4. Real detection on a rendered raised hand (palm toward the viewer, so a right
  //    hand), and on its mirror image.
  CVPixelBufferRef a = render(@"✋", false), b = render(@"✋", true);
  HandObs h, hm;
  const bool ok = detectOne(a, h), okm = detectOne(b, hm);
  CHECK(ok && okm);
  if (ok && okm) {
    CHECK(h.y[0] > h.y[12]);                              // top-left origin: wrist below the middle tip
    for (int f = 8; f <= 20; f += 4) CHECK(h.y[f] < h.y[f - 3]);   // fingertips above their MCPs
    CHECK(h.x[4] > h.x[20]);                              // thumb on the image's right: a right hand
    CHECK(!h.right);                                      // which MediaPipe calls "Left" when unmirrored
    CHECK(hm.right);
    float err = 0;
    for (int i = 0; i < 21; i++) err = std::max(err, std::max(std::fabs(hm.x[i] - (1 - h.x[i])), std::fabs(hm.y[i] - h.y[i])));
    CHECK(err < 0.03f);
    printf("emoji hand: score %.2f  wrist (%.3f, %.3f)  index tip (%.3f, %.3f)  mirror max error %.4f\n",
           h.score, h.x[0], h.y[0], h.x[8], h.y[8], err);
  }
  CVPixelBufferRelease(a); CVPixelBufferRelease(b);

  // 5. The capture output accepts the settings camera.mm asks for.
  @try {
    AVCaptureVideoDataOutput* out = [AVCaptureVideoDataOutput new];
    out.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                           (id)kCVPixelBufferMetalCompatibilityKey: @YES };
  } @catch (NSException* e) { CHECK(!"videoSettings rejected"); printf("  %s\n", e.reason.UTF8String); }
}

static void bench(const char* label, CVPixelBufferRef pb, id device) {
  VNDetectHumanHandPoseRequest* r = [VNDetectHumanHandPoseRequest new];
  r.maximumHandCount = 2;
  if (device) [r setComputeDevice:device forComputeStage:VNComputeStageMain];
  std::vector<double> v;
  double cpu0 = 0;
  for (int i = 0; i < 220; i++) {
    @autoreleasepool {
      if (i == 20) cpu0 = cpuMs();
      const double t = ms();
      VNImageRequestHandler* h = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pb orientation:kCGImagePropertyOrientationUp options:@{}];
      [h performRequests:@[r] error:nil];
      if (i >= 20) v.push_back(ms() - t);                // the first 20 warm up
    }
  }
  const double cpu = (cpuMs() - cpu0) / v.size();
  std::sort(v.begin(), v.end());
  double mean = 0; for (double x : v) mean += x; mean /= v.size();
  printf("  %-22s mean %5.2f ms  p95 %5.2f ms  cpu %5.2f ms/run  hands %lu\n", label, mean, v[v.size() * 95 / 100], cpu,
         (unsigned long)r.results.count);
}

static void benchmark() {
  printf("detection cost, 1280x720 BGRA, 200 runs after 20 warm-up, revision %lu:\n",
         (unsigned long)[VNDetectHumanHandPoseRequest new].revision);
  CVPixelBufferRef hand = render(@"✋", false), empty = render(@" ", false);
  NSArray* devs = [[VNDetectHumanHandPoseRequest new] supportedComputeStageDevicesAndReturnError:nil][VNComputeStageMain];
  struct { const char* name; id dev; } opts[4] = {{"default", nil}};
  int n = 1;
  for (id d in devs) {
    const char* nm = [d isKindOfClass:[MLNeuralEngineComputeDevice class]] ? "neural engine"
                   : [d isKindOfClass:[MLGPUComputeDevice class]] ? "gpu" : "cpu";
    if (n < 4) opts[n++] = {nm, d};
  }
  for (int i = 0; i < n; i++) {
    char l[64];
    snprintf(l, sizeof l, "%s, hand", opts[i].name); bench(l, hand, opts[i].dev);
    snprintf(l, sizeof l, "%s, no hand", opts[i].name); bench(l, empty, opts[i].dev);
  }
  CVPixelBufferRelease(hand); CVPixelBufferRelease(empty);
}

static void live() {
  printf("live: camera light + two hands for 5 s\n");
  std::atomic<int> results{0}, withHands{0};
  std::atomic<bool> denied{false};
  CameraCallbacks cb;
  cb.hands = [&](const HandObs* h, int n, double t) {
    int k = results++;
    if (n) withHands++;
    if (k % 10 == 0)
      printf("  t=%.3f (lag %.0f ms) hands=%d\n", t, (CACurrentMediaTime() - t) * 1e3, n);
    for (int i = 0; i < n && k % 10 == 0; i++)
      printf("    %s score %.2f wrist (%.2f, %.2f) index tip (%.2f, %.2f)\n", h[i].right ? "Right" : "Left ",
             h[i].score, h[i].x[0], h[i].y[0], h[i].x[8], h[i].y[8]);
  };
  cb.state = [](bool running, bool ready) { printf("  state: running %d, hands ready %d\n", running, ready); };
  cb.error = [&](CameraError e) { denied = true; printf("  error %d (0 permission denied, 1 no camera, 2 failed)\n", (int)e); };
  Camera* c = camera_create(cb);
  camera_set_two_hands(c, true);
  camera_set_light(c, true);
  camera_set_hands(c, true);
  int frames = 0;
  const double t0 = ms();
  while (ms() - t0 < 5000 && !denied) {
    @autoreleasepool { [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]]; }
    if (CVPixelBufferRef pb = camera_take_frame(c)) {
      if (!frames++)
        printf("  first frame %zux%zu BGRA %d IOSurface %d\n", CVPixelBufferGetWidth(pb), CVPixelBufferGetHeight(pb),
               CVPixelBufferGetPixelFormatType(pb) == kCVPixelFormatType_32BGRA, CVPixelBufferGetIOSurface(pb) != nullptr);
      CVPixelBufferRelease(pb);
    }
  }
  camera_destroy(c);
  printf("  %d frames taken, %d hand results (%d with a hand) in 5 s\n", frames, results.load(), withHands.load());
}

int main(int argc, char** argv) {
  @autoreleasepool {
    unitChecks();
    printf(fails ? "unit checks: %d FAILED\n" : "unit checks: all passed\n", fails);
    benchmark();
    if (argc > 1 && !strcmp(argv[1], "--live")) live();
  }
  return fails ? 1 : 0;
}
