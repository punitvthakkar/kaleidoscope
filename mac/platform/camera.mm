#import "camera.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreML/CoreML.h>
#import <QuartzCore/QuartzCore.h>
#include <os/lock.h>
#include <atomic>
#include <cmath>

// ---------------- Vision -> MediaPipe ----------------
namespace camera_detail {

VNHumanHandPoseObservationJointName joint_name(int i) {
  static VNHumanHandPoseObservationJointName const names[21] = {
    VNHumanHandPoseObservationJointNameWrist,
    VNHumanHandPoseObservationJointNameThumbCMC, VNHumanHandPoseObservationJointNameThumbMP,
    VNHumanHandPoseObservationJointNameThumbIP, VNHumanHandPoseObservationJointNameThumbTip,
    VNHumanHandPoseObservationJointNameIndexMCP, VNHumanHandPoseObservationJointNameIndexPIP,
    VNHumanHandPoseObservationJointNameIndexDIP, VNHumanHandPoseObservationJointNameIndexTip,
    VNHumanHandPoseObservationJointNameMiddleMCP, VNHumanHandPoseObservationJointNameMiddlePIP,
    VNHumanHandPoseObservationJointNameMiddleDIP, VNHumanHandPoseObservationJointNameMiddleTip,
    VNHumanHandPoseObservationJointNameRingMCP, VNHumanHandPoseObservationJointNameRingPIP,
    VNHumanHandPoseObservationJointNameRingDIP, VNHumanHandPoseObservationJointNameRingTip,
    VNHumanHandPoseObservationJointNameLittleMCP, VNHumanHandPoseObservationJointNameLittlePIP,
    VNHumanHandPoseObservationJointNameLittleDIP, VNHumanHandPoseObservationJointNameLittleTip,
  };
  return names[i];
}

bool mediapipe_right(VNChirality c) { return c == VNChiralityLeft; }

// Low-confidence joints. The wrist and middle MCP set the hand's size and angle, so
// without them, or with most joints unsure, the hand is dropped. Any other unsure
// joint keeps where it was last seen relative to the wrist (for a quarter second),
// else folds onto its parent joint, so a half-seen finger reads as curled and rests
// instead of flickering between painting and not.
bool to_hand_obs(const Joint j[21], VNChirality chir, float score, double t,
                 HandMemory& mem, HandObs& out) {
  const float k = kMinJointConfidence;
  int good = 0;
  for (int i = 0; i < 21; i++) good += j[i].c >= k;
  if (j[0].c < k || j[9].c < k || good < 11) return false;
  const bool recent = t - mem.t < 0.25;
  const float wx = j[0].x, wy = 1 - j[0].y;
  for (int i = 0; i < 21; i++) {
    if (j[i].c >= k) { out.x[i] = j[i].x; out.y[i] = 1 - j[i].y; continue; }
    if (recent) { out.x[i] = mem.x[i] - mem.x[0] + wx; out.y[i] = mem.y[i] - mem.y[0] + wy; continue; }
    const int parent = (i - 1) % 4 == 0 ? 0 : i - 1;     // 1, 5, 9, 13, 17 hang off the wrist
    out.x[i] = out.x[parent]; out.y[i] = out.y[parent];
  }
  out.right = mediapipe_right(chir);
  out.score = score;
  for (int i = 0; i < 21; i++) { mem.x[i] = out.x[i]; mem.y[i] = out.y[i]; }
  mem.t = t;
  return true;
}

}  // namespace camera_detail

using namespace camera_detail;

// ---------------- which cameras there are ----------------
// Built-in, plugged in (USB, capture cards, displays) and an iPhone nearby.
static NSArray<AVCaptureDevice*>* videoDevices() {
  NSArray<AVCaptureDevice*>* all = [AVCaptureDeviceDiscoverySession
      discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal,
                                        AVCaptureDeviceTypeContinuityCamera]
                            mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified].devices;
  return [all filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(AVCaptureDevice* d, id) { return !d.suspended; }]];
}

std::vector<CameraDevice> camera_devices() {
  std::vector<CameraDevice> out;
  @autoreleasepool {
    for (AVCaptureDevice* d in videoDevices()) out.push_back({d.uniqueID.UTF8String, d.localizedName.UTF8String ?: "Camera"});
  }
  return out;
}

// ---------------- capture + tracking ----------------
@interface FNCamera : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@end

@implementation FNCamera {
@public
  CameraCallbacks _cb;
  dispatch_queue_t _sessionQ, _videoQ, _trackQ;
  AVCaptureSession* _session;           // session queue only
  AVCaptureDevice* _device;
  NSString* _wanted;                    // session queue only: the chosen camera's uniqueID, nil for automatic
  id _plugged, _unplugged;              // notification tokens
  bool _authorized, _asking;            // session queue only
  std::atomic<bool> _light, _hands, _two, _grip, _busy, _dead;

  os_unfair_lock _frameLock;
  CVPixelBufferRef _latest;
  bool _fresh;

  // Cadence. Times are host-clock seconds; written on the tracking queue.
  std::atomic<double> _seenAt, _fastUntil;
  double _sentAt;                       // video queue
  double _heardAt;                      // tracking queue
  HandMemory _mem[2];
  bool _was[2];
  float _gx[2], _gy[2];
  VNDetectHumanHandPoseRequest* _req;
  std::atomic<bool> _ready;
}

- (instancetype)initWith:(CameraCallbacks)cb {
  if (!(self = [super init])) return nil;
  _cb = std::move(cb);
  _sessionQ = dispatch_queue_create("fractonaut.camera.session", DISPATCH_QUEUE_SERIAL);
  _videoQ = dispatch_queue_create("fractonaut.camera.video",
      dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
  _trackQ = dispatch_queue_create("fractonaut.camera.hands",
      dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
  _frameLock = OS_UNFAIR_LOCK_INIT;
  _sentAt = -1e9;
  __weak FNCamera* me = self;
  void (^changed)(NSNotification*) = ^(NSNotification* n) { [me deviceChanged:n]; };
  NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
  _plugged = [nc addObserverForName:AVCaptureDeviceWasConnectedNotification object:nil queue:nil usingBlock:changed];
  _unplugged = [nc addObserverForName:AVCaptureDeviceWasDisconnectedNotification object:nil queue:nil usingBlock:changed];
  videoDevices();                       // discovery must have run once for those to arrive
  return self;
}

// Session queue. The chosen camera if it is there, else the system's default.
- (AVCaptureDevice*)pick {
  AVCaptureDevice* dev = _wanted.length ? [AVCaptureDevice deviceWithUniqueID:_wanted] : nil;
  if (dev && (![dev hasMediaType:AVMediaTypeVideo] || dev.suspended)) dev = nil;
  if (!dev) dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
  if (!dev) dev = videoDevices().firstObject;
  return dev;
}

// Session queue. Stops the camera so the next update starts it afresh.
- (void)closeSession {
  if (!_session) return;
  [_session stopRunning];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  _session = nil;                      // drop the session and device so the light goes off
  _device = nil;
  [self dropFrame];
}

// Session queue. Moves a running camera to whichever one should be used now.
- (void)changeOver {
  if (!_session) return;
  AVCaptureDevice* next = [self pick];
  if (next && [next.uniqueID isEqualToString:_device.uniqueID]) return;
  [self closeSession];
  [self update];
  if (!_session) [self reportState];
}

// Any thread. A camera unplugged while in use gives way to another; the chosen one
// coming back takes over again.
- (void)deviceChanged:(NSNotification*)n {
  AVCaptureDevice* d = n.object;
  if (![d isKindOfClass:AVCaptureDevice.class] || ![d hasMediaType:AVMediaTypeVideo] || _dead) return;
  dispatch_async(_sessionQ, ^{ [self changeOver]; });
  [self post:^{ if (self->_cb.devices) self->_cb.devices(); }];
}

- (void)post:(void (^)(void))b {
  dispatch_async(dispatch_get_main_queue(), ^{ if (!self->_dead) b(); });
}
- (void)reportError:(CameraError)e {
  [self post:^{ if (self->_cb.error) self->_cb.error(e); }];
}
- (void)reportState {
  bool running = _session != nil, ready = _ready && _hands;
  [self post:^{ if (self->_cb.state) self->_cb.state(running, ready); }];
}

// Session queue. Starts, retunes or fully releases the camera to match light/hands.
- (void)update {
  const bool want = (_light || _hands) && !_dead;
  if (!want) {
    if (_session) {
      [self closeSession];
      [self reportState];
    }
    return;
  }
  if (!_authorized) {
    AVAuthorizationStatus s = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (s == AVAuthorizationStatusNotDetermined) {
      // The prompt may stay unanswered; the queue stays free meanwhile.
      if (!_asking) {
        _asking = true;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL) {
          dispatch_async(self->_sessionQ, ^{ self->_asking = false; [self update]; });
        }];
      }
      return;
    }
    if (s != AVAuthorizationStatusAuthorized) {
      _light = _hands = false;
      [self reportError:CameraError::PermissionDenied];
      return;
    }
    _authorized = true;
  }
  NSString* preset = _light ? AVCaptureSessionPreset1280x720 : AVCaptureSessionPreset640x480;
  if (!_session) {
    AVCaptureDevice* dev = [self pick];
    NSError* err = nil;
    AVCaptureDeviceInput* in = dev ? [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err] : nil;
    if (!in) {
      _light = _hands = false;
      [self reportError:dev ? CameraError::Failed : CameraError::NoCamera];
      return;
    }
    AVCaptureVideoDataOutput* out = [AVCaptureVideoDataOutput new];
    out.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                           (id)kCVPixelBufferMetalCompatibilityKey: @YES };
    out.alwaysDiscardsLateVideoFrames = YES;
    [out setSampleBufferDelegate:self queue:_videoQ];
    AVCaptureSession* s = [AVCaptureSession new];
    [s beginConfiguration];
    if ([s canAddInput:in]) [s addInput:in];
    if ([s canAddOutput:out]) [s addOutput:out];
    AVCaptureConnection* conn = [out connectionWithMediaType:AVMediaTypeVideo];
    if (conn.isVideoMirroringSupported) { conn.automaticallyAdjustsVideoMirroring = NO; conn.videoMirrored = NO; }
    s.sessionPreset = [s canSetSessionPreset:preset] ? preset : AVCaptureSessionPresetHigh;
    [s commitConfiguration];
    [self capFrameRate:dev];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(runtimeError:)
        name:AVCaptureSessionRuntimeErrorNotification object:s];
    _session = s; _device = dev;
    [s startRunning];
    [self reportState];
  } else if (![_session.sessionPreset isEqualToString:preset] && [_session canSetSessionPreset:preset]) {
    [_session beginConfiguration];
    _session.sessionPreset = preset;
    [_session commitConfiguration];
    [self capFrameRate:_device];
  }
}

// 30 fps: the pens glide between samples, so a faster camera buys nothing.
- (void)capFrameRate:(AVCaptureDevice*)dev {
  for (AVFrameRateRange* r in dev.activeFormat.videoSupportedFrameRateRanges) {
    if (r.minFrameRate <= 30 && r.maxFrameRate >= 30 && [dev lockForConfiguration:nil]) {
      dev.activeVideoMinFrameDuration = CMTimeMake(1, 30);
      [dev unlockForConfiguration];
      return;
    }
  }
}

- (void)runtimeError:(NSNotification*)n {
  dispatch_async(_sessionQ, ^{
    self->_light = self->_hands = false;
    [self update];
    [self reportError:CameraError::Failed];
  });
}

- (void)dropFrame {
  os_unfair_lock_lock(&_frameLock);
  CVPixelBufferRef old = _latest;
  _latest = nullptr; _fresh = false;
  os_unfair_lock_unlock(&_frameLock);
  if (old) CVPixelBufferRelease(old);
}

- (CVPixelBufferRef)takeFrame {
  CVPixelBufferRef r = nullptr;
  os_unfair_lock_lock(&_frameLock);
  if (_fresh && _latest) { r = CVPixelBufferRetain(_latest); _fresh = false; }
  os_unfair_lock_unlock(&_frameLock);
  return r;
}

// Video queue.
- (void)captureOutput:(AVCaptureOutput*)o didOutputSampleBuffer:(CMSampleBufferRef)sb
       fromConnection:(AVCaptureConnection*)c {
  CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sb);
  if (!pb || _dead) return;
  const double t = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
  if (_light) {
    CVPixelBufferRetain(pb);
    os_unfair_lock_lock(&_frameLock);
    CVPixelBufferRef old = _latest;
    _latest = pb; _fresh = true;
    os_unfair_lock_unlock(&_frameLock);
    if (old) CVPixelBufferRelease(old);
  }
  if (!_hands || _busy) return;
  // Web cadence: 30 a second while a hand moves or holds the glass, 15 while it is
  // slow or still, a few glances a second once it has left, one a second after ten
  // seconds of nobody there. The 30 and 62 ms gaps land on every and every other frame.
  const double away = t - _seenAt;
  const double gap = away < 1.5 ? 0.0 : away < 10 ? 0.140 : 1.0;   // every frame while a hand is here: responsiveness first
  if (t - _sentAt < gap) return;
  _sentAt = t;
  _busy = true;
  CVPixelBufferRetain(pb);
  dispatch_async(_trackQ, ^{
    if (self->_hands && !self->_dead) [self detect:pb at:t];
    CVPixelBufferRelease(pb);
    self->_busy = false;
  });
}

// Tracking queue.
- (void)detect:(CVPixelBufferRef)pb at:(double)t {
  @autoreleasepool {
    if (!_req) {
      _req = [VNDetectHumanHandPoseRequest new];
      // Measured on M5 Max: Neural Engine and GPU run a detection in about the same
      // time (~7 ms at 720p); the Neural Engine leaves the GPU to the renderer.
      for (id d in [_req supportedComputeStageDevicesAndReturnError:nil][VNComputeStageMain])
        if ([d isKindOfClass:[MLNeuralEngineComputeDevice class]]) [_req setComputeDevice:d forComputeStage:VNComputeStageMain];
    }
    _req.maximumHandCount = _two ? 2 : 1;
    VNImageRequestHandler* h = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pb
        orientation:kCGImagePropertyOrientationUp options:@{}];
    NSArray<VNHumanHandPoseObservation*>* res = [h performRequests:@[_req] error:nil] ? _req.results : nil;

    HandObs hands[2];
    int n = 0, slotOf[2] = {-1, -1};
    for (VNHumanHandPoseObservation* ob in res) {
      if (n == 2) break;
      NSDictionary<VNHumanHandPoseObservationJointName, VNRecognizedPoint*>* pts =
          [ob recognizedPointsForJointsGroupName:VNHumanHandPoseObservationJointsGroupNameAll error:nil];
      Joint j[21];
      for (int i = 0; i < 21; i++) {
        VNRecognizedPoint* p = pts[joint_name(i)];
        j[i] = p ? Joint{(float)p.location.x, (float)p.location.y, p.confidence} : Joint{0, 0, 0};
      }
      VNChirality chir = ob.chirality;
      // Each side keeps its own memory; an unknown or repeated side takes the free one.
      int slot = chir == VNChiralityUnknown ? -1 : mediapipe_right(chir) ? 1 : 0;
      if (slot < 0 || (n && slotOf[0] == slot)) slot = n ? 1 - slotOf[0] : 0;
      if (chir == VNChiralityUnknown) chir = slot ? VNChiralityLeft : VNChiralityRight;
      if (to_hand_obs(j, chir, ob.confidence, t, _mem[slot], hands[n])) slotOf[n++] = slot;
    }

    // Motion tiers (web onHands): a hand that just appeared, or a grip point moving
    // faster than 0.3 screen-widths a second, earns the full rate for half a second.
    const double secs = std::fmax(0.01, t - _heardAt);
    _heardAt = t;
    bool seen[2] = {false, false};
    for (int i = 0; i < n; i++) {
      const int s = slotOf[i];
      const float gx = (hands[i].x[4] + hands[i].x[8]) / 2 * 1.25f, gy = (hands[i].y[4] + hands[i].y[8]) / 2 * 1.25f;
      if (!_was[s] || std::hypot(gx - _gx[s], gy - _gy[s]) / secs > 0.3) _fastUntil = t + 0.5;
      _gx[s] = gx; _gy[s] = gy;
      seen[s] = true;
    }
    _was[0] = seen[0]; _was[1] = seen[1];
    if (n) _seenAt = t;

    if (!_ready) { _ready = true; [self reportState]; }
    if (_cb.hands && !_dead) _cb.hands(hands, n, t);
  }
}

@end

// ---------------- C API ----------------
struct Camera { FNCamera* obj; };

Camera* camera_create(CameraCallbacks cb) { return new Camera{[[FNCamera alloc] initWith:std::move(cb)]}; }

void camera_destroy(Camera* c) {
  if (!c) return;
  FNCamera* o = c->obj;
  o->_light = o->_hands = false;
  o->_dead = true;
  [NSNotificationCenter.defaultCenter removeObserver:o->_plugged];
  [NSNotificationCenter.defaultCenter removeObserver:o->_unplugged];
  dispatch_sync(o->_sessionQ, ^{ [o update]; });
  dispatch_sync(o->_videoQ, ^{});
  dispatch_sync(o->_trackQ, ^{});
  [o dropFrame];
  delete c;
}

static void retune(FNCamera* o) { dispatch_async(o->_sessionQ, ^{ [o update]; }); }

void camera_set_light(Camera* c, bool on) {
  FNCamera* o = c->obj;
  if (o->_light.exchange(on) == on) return;
  if (!on) [o dropFrame];
  retune(o);
}

void camera_set_hands(Camera* c, bool on) {
  FNCamera* o = c->obj;
  if (o->_hands.exchange(on) == on) return;
  dispatch_async(o->_trackQ, ^{
    if (on) {
      // Start in the attentive tiers, as if a hand had just been seen.
      o->_seenAt = CACurrentMediaTime(); o->_heardAt = 0;
      o->_was[0] = o->_was[1] = false;
      o->_mem[0].t = o->_mem[1].t = -1e9;
    } else {
      o->_ready = false;
      if (o->_cb.hands && !o->_dead) o->_cb.hands(nullptr, 0, CACurrentMediaTime());
      [o reportState];
    }
  });
  retune(o);
}

void camera_set_two_hands(Camera* c, bool on) { c->obj->_two = on; }

void camera_set_device(Camera* c, const char* id) {
  FNCamera* o = c->obj;
  NSString* want = id && *id ? @(id) : nil;
  dispatch_async(o->_sessionQ, ^{
    o->_wanted = want;
    [o changeOver];
  });
}
void camera_set_grip_held(Camera* c, bool held) { c->obj->_grip = held; }
CVPixelBufferRef camera_take_frame(Camera* c) { return [c->obj takeFrame]; }
