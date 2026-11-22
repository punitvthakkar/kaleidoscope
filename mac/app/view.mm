// The picture: a CAMetalLayer driven by CAMetalDisplayLink on its own render thread,
// and native pointer input over the bare picture.
#import "app.h"
#import <QuartzCore/QuartzCore.h>
#include <mach/mach_time.h>
#include <cmath>
#include "../platform/audio.h"

static uint64_t machTicks(double seconds) {
  static mach_timebase_info_data_t tb = [] { mach_timebase_info_data_t t; mach_timebase_info(&t); return t; }();
  return (uint64_t)(seconds * 1e9 * tb.denom / tb.numer);
}

@interface FXMetalView () <CAMetalDisplayLinkDelegate>
@end

@implementation FXMetalView {
  CAMetalLayer* _metal;
  CAMetalDisplayLink* _link;        // render thread
  CFRunLoopRef _runLoop;
  Renderer* _r;
  std::atomic<float> _headroomA;
  std::atomic<double> _interval;    // the display's shortest frame interval we may use
  double _maxRate;                  // main thread
  CAFrameRateRange _range;          // main thread copy of what the link was given

  // render thread
  double _lastPresent;
  bool _half;                       // coolHands: the link runs at half the display's rate
  CAFrameRateRange _linkRange;      // the full range, as the render thread last applied it
  int _rw, _rh, _taps, _steps; float _rfs;
  float _bass, _mid, _high;
  void (^_hook)(id<MTLCommandBuffer>, id<MTLTexture>, uint64_t);
  bool (^_wants)(uint64_t);
  uint64_t _hostNow;
  HandsState _grip; double _gripAt; bool _gripHeld;
  Engine* _exportEngine;            // the copy a running export steps

  // debug, over the last second
  std::atomic<int> _frames, _dropped, _gpuN, _calls;
  std::atomic<double> _gpuSum;
}

- (instancetype)initWithFrame:(NSRect)frame shaderDir:(NSString*)dir error:(NSError**)error {
  if (!(self = [super initWithFrame:frame])) return nil;
  _device = MTLCreateSystemDefaultDevice();
  _r = renderer_create(_device, dir, error);
  if (!_r) return nil;
  _headroomA = 1; _interval = 1 / 60.0;
  self.wantsLayer = YES;
  self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
  _metal.device = _device;
  _metal.pixelFormat = MTLPixelFormatRGBA16Float;
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
  _metal.colorspace = cs;
  CGColorSpaceRelease(cs);
  _metal.wantsExtendedDynamicRangeContent = YES;
  _metal.framebufferOnly = YES;
  _metal.opaque = YES;
  _metal.maximumDrawableCount = 3;
  // While the window resizes, the last drawable stretches until the next frame lands.
  _metal.contentsGravity = kCAGravityResize;
  _metal.backgroundColor = CGColorGetConstantColor(kCGColorBlack);

  __unsafe_unretained FXMetalView* me = self;   // lives as long as the app
  renderer_set_gpu_time_handler(_r, ^(double s) {
    engine_report_gpu(gEngine, s, me->_interval.load());
    me->_gpuSum.fetch_add(s); me->_gpuN++;
  });
  return self;
}

- (CALayer*)makeBackingLayer {
  _metal = [CAMetalLayer layer];
  return _metal;
}

- (Renderer*)renderer { return _r; }
- (float)headroom { return _headroomA; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return NO; }
- (NSView*)hitTest:(NSPoint)p { return nil; }   // the interface above takes every event

// ---------------- sizes ----------------
- (void)tellEngine {
  NSSize s = self.bounds.size;
  CGFloat scale = self.window ? self.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
  _metal.contentsScale = scale;
  engine_resize(gEngine, s.width, s.height, scale);
}
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size]; [self tellEngine]; }
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; [self tellEngine]; [self updateScreen]; }
- (void)viewDidMoveToWindow { [super viewDidMoveToWindow]; [self tellEngine]; [self updateScreen]; }

- (void)updateScreen {
  NSScreen* s = self.window.screen ?: NSScreen.mainScreen;
  _headroomA = (float)std::max<CGFloat>(1, s.maximumExtendedDynamicRangeColorComponentValue);
  double fast = s.minimumRefreshInterval > 0 ? s.minimumRefreshInterval : 1 / 60.0;
  double slow = std::max(fast, s.maximumRefreshInterval);
  if (_maxRate > 0) fast = std::max(fast, 1 / _maxRate);
  _interval = fast;
  CAFrameRateRange range = CAFrameRateRangeMake((float)std::min(1 / slow, 1 / fast), (float)(1 / fast), (float)(1 / fast));
  if (CAFrameRateRangeIsEqualToRange(range, _range)) return;
  _range = range;
  [self renderThread:^{ self->_linkRange = range; [self applyRange]; }];
}

// Render thread.
- (void)applyRange {
  CAFrameRateRange r = _linkRange;
  if (_half) r = CAFrameRateRangeMake(r.minimum / 2, r.maximum / 2, r.preferred / 2);
  _link.preferredFrameRateRange = r;
}

- (void)setMaxRate:(double)hz { _maxRate = hz; [self updateScreen]; }

// ---------------- render thread ----------------
- (void)start {
  [self tellEngine];
  CGSize px = [self convertSizeToBacking:self.bounds.size];
  _metal.drawableSize = CGSizeMake(std::max<CGFloat>(1, px.width), std::max<CGFloat>(1, px.height));
  dispatch_semaphore_t ready = dispatch_semaphore_create(0);
  NSThread* t = [[NSThread alloc] initWithBlock:^{
    self->_runLoop = CFRunLoopGetCurrent();
    [NSRunLoop.currentRunLoop addPort:[NSMachPort port] forMode:NSDefaultRunLoopMode];   // keeps the loop alive while paused
    self->_link = [[CAMetalDisplayLink alloc] initWithMetalLayer:self->_metal];
    self->_link.delegate = self;
    self->_linkRange = self->_range;
    [self applyRange];
    [self->_link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
    dispatch_semaphore_signal(ready);
    for (;;) @autoreleasepool { [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:NSDate.distantFuture]; }
  }];
  t.name = @"fractonaut.render";
  t.qualityOfService = NSQualityOfServiceUserInteractive;
  [t start];
  dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);
}

- (void)renderThread:(dispatch_block_t)block {
  if (!_runLoop) { block(); return; }   // not started yet: nothing else touches the renderer
  CFRunLoopPerformBlock(_runLoop, kCFRunLoopDefaultMode, block);
  CFRunLoopWakeUp(_runLoop);
}

// Quitting: no frame may run while the process tears down.
- (void)stop {
  if (!_runLoop) return;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [self renderThread:^{
    [self->_link invalidate];
    self->_link = nil;
    renderer_finish(self->_r);
    dispatch_semaphore_signal(done);
  }];
  dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC));
}

- (void)setRunning:(BOOL)running {
  [self renderThread:^{
    if (self->_link.paused == !running) return;
    self->_link.paused = !running;
    self->_lastPresent = 0;
  }];
}

- (void)setFrameHook:(void (^)(id<MTLCommandBuffer>, id<MTLTexture>, uint64_t))hook wants:(bool (^)(uint64_t))wants {
  [self renderThread:^{
    self->_hook = hook;
    self->_wants = wants;
    if (!hook) self->_wants = nil;
    if (hook) {
      __unsafe_unretained FXMetalView* me = self;
      renderer_set_frame_hook(self->_r, ^(id<MTLCommandBuffer> cmd, id<MTLTexture> cap) { if (me->_hook) me->_hook(cmd, cap, me->_hostNow); });
      renderer_set_capture_filter(self->_r, ^bool { return !me->_wants || me->_wants(me->_hostNow); });
      renderer_set_capture(self->_r, true);
    } else {
      renderer_set_capture(self->_r, false);
      renderer_set_capture_filter(self->_r, nil);
      renderer_set_frame_hook(self->_r, nil);
    }
  }];
}

static void haptic(int kind) {
  dispatch_async(dispatch_get_main_queue(), ^{
    [NSHapticFeedbackManager.defaultPerformer
        performFeedbackPattern:kind == HAPTIC_CONFIRM ? NSHapticFeedbackPatternLevelChange : NSHapticFeedbackPatternGeneric
              performanceTime:NSHapticFeedbackPerformanceTimeNow];
  });
}

- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)u {
  @autoreleasepool { [self frame:u]; }
}

- (void)frame:(CAMetalDisplayLinkUpdate*)u {
  Engine* e = gEngine;
  _calls++;
  const double tp = u.targetPresentationTimestamp, interval = _interval;
  const bool half = engine_stats(e).halfRate;
  if (half != _half) { _half = half; [self applyRange]; }
  double dt = _lastPresent > 0 ? tp - _lastPresent : interval;
  if (_lastPresent > 0 && dt > interval * (half ? 2 : 1) * 1.5) _dropped++;
  _lastPresent = tp;

  EngineEvent ev;
  while (engine_poll_event(e, &ev)) {
    if (ev.type == EV_PLUCK) audio_pluck(ev.x, ev.y, ev.v);
    else if (ev.type == EV_HAPTIC) haptic(ev.a);
    else if (ev.type == EV_NEXT_CHAMBER) dispatch_async(dispatch_get_main_queue(), ^{ [gWeb event:@"nextChamber" payload:nil]; });
    // EV_BREATH_CUE: the interface keeps its own clock for the words.
  }
  if (gCamera)
    if (CVPixelBufferRef pb = camera_take_frame(gCamera)) {
      renderer_upload_camera(_r, pb);
      engine_set_camera_aspect(e, (float)CVPixelBufferGetWidth(pb) / std::max<size_t>(1, CVPixelBufferGetHeight(pb)));
      CVPixelBufferRelease(pb);
    }
  renderer_fade_veil(_r, (float)std::min(dt, 0.05));
  const Frame& f = engine_step(e, dt, _bass, _mid, _high);
  const EngineStats st = engine_stats(e);
  audio_update((float)std::min(dt, 0.05), st.energy, st.lung);
  const SynthLevels lv = audio_levels();
  _bass = lv.bass; _mid = lv.mid; _high = lv.high;
  if (st.drawW <= 0) return;

  if (st.renderW != _rw || st.renderH != _rh || st.echoScale != _rfs) {
    if (renderer_resize(_r, st.renderW, st.renderH, st.echoScale, st.capW, st.capH, st.capEchoW, st.capEchoH)) { _rw = st.renderW; _rh = st.renderH; _rfs = st.echoScale; }
  }
  if (st.ssTaps != _taps || st.rmSteps != _steps) { renderer_set_detail(_r, st.ssTaps, st.rmSteps); _taps = st.ssTaps; _steps = st.rmSteps; }
  if (_metal.drawableSize.width != st.drawW || _metal.drawableSize.height != st.drawH) {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _metal.drawableSize = CGSizeMake(st.drawW, st.drawH);
    [CATransaction commit];
  }
  const HandsState hs = engine_hands_state(e);
  [self gripFrom:hs at:tp];

  _hostNow = machTicks(tp);
  renderer_render(_r, f, u.drawable.texture, u.drawable, _headroomA);
  _frames++;
  [self exportStep];
}

// Render thread.
- (void)exportStep {
  if (renderer_export_active(_r)) { if (_exportEngine) renderer_export_step(_r, engine_step(_exportEngine, 1 / 60.0, 0, 0, 0)); }
  else if (_exportEngine) { engine_destroy(_exportEngine); _exportEngine = nullptr; }
}

- (void)exportLongEdge:(int)longEdge frames:(int)frames done:(void (^)(NSData*))done {
  [self renderThread:^{
    if (self->_exportEngine || !renderer_export_begin(self->_r, longEdge, frames, ^(NSData* png, int, int) { done(png); })) { done(nil); return; }
    self->_exportEngine = engine_clone(gEngine);
    // No frames are coming while the picture is paused or hidden: finish here.
    if (!self->_link || self->_link.paused) while (self->_exportEngine) [self exportStep];
  }];
}

// Grip markers: on change, and at most 30 times a second while a grip moves.
- (void)gripFrom:(const HandsState&)h at:(double)t {
  const bool held = h.grabMode > 0;
  if (held != _gripHeld && gCamera) { _gripHeld = held; camera_set_grip_held(gCamera, held); }
  bool changed = false, moved = false;
  for (int i = 0; i < 2; i++) {
    if (h.grabOn[i] != _grip.grabOn[i]) changed = true;
    else if (h.grabOn[i] && (std::fabs(h.grabX[i] - _grip.grabX[i]) > 1e-3f || std::fabs(h.grabY[i] - _grip.grabY[i]) > 1e-3f)) moved = true;
  }
  if (!changed && !(moved && t - _gripAt >= 1 / 30.0)) return;
  _grip = h; _gripAt = t;
  NSMutableArray* hands = [NSMutableArray array];
  for (int i = 0; i < 2; i++) [hands addObject:@{@"on": @(h.grabOn[i]), @"x": @(h.grabX[i]), @"y": @(h.grabY[i])}];
  dispatch_async(dispatch_get_main_queue(), ^{ [gWeb event:@"grip" payload:@{@"hands": hands}]; });
}

- (NSString*)debugLine {
  const int n = _frames.exchange(0), d = _dropped.exchange(0), g = _gpuN.exchange(0), c = _calls.exchange(0);
  const double gs = _gpuSum.exchange(0);
  const EngineStats st = engine_stats(gEngine);
  return [NSString stringWithFormat:@"fps %d  gpu %.2f ms  drawable %dx%d  render %dx%d  edr %.2f  level %d  dropped %d  calls %d%@",
          n, g ? gs / g * 1000 : 0, st.drawW, st.drawH, st.renderW, st.renderH, _headroomA.load(), st.level, d, c,
          self.window.occlusionState & NSWindowOcclusionStateVisible ? @"" : @"  (window not visible)"];
}
@end

// ---------------- input ----------------
// The web's rules (index.html, Pens): a press that starts on the bare picture is
// captured until release, even over the interface; a press that starts on the
// interface never paints; the mouse pen hovers over the picture and leaves when the
// pointer goes over the interface or out of the window.
namespace {
NSWindow* win;
NSView* pic;
bool captured, inside, mag, rot;

NSPoint at(NSEvent* e) {
  NSPoint p = [pic convertPoint:e.locationInWindow fromView:nil];
  return NSMakePoint(p.x, pic.bounds.size.height - p.y);
}
bool overContent(NSEvent* e) {
  if (e.window != win) return false;
  NSPoint p = [pic convertPoint:e.locationInWindow fromView:nil];
  if (!NSPointInRect(p, pic.bounds)) return false;
  // Title bar controls sit above the content view.
  NSView* hit = [win.contentView.superview hitTest:[win.contentView.superview convertPoint:e.locationInWindow fromView:nil]];
  return hit && [hit isDescendantOf:win.contentView];
}
void send(int kind, NSEvent* e) {
  NSPoint p = at(e);
  engine_pointer(gEngine, 1, kind, (float)p.x, (float)p.y, e.timestamp, true);
}
void leave() {
  if (!inside) return;
  inside = false;
  engine_pointer(gEngine, 1, PTR_LEAVE, 0, 0, CACurrentMediaTime(), true);
}
}  // namespace

@interface FXExitTracker : NSObject
@end
@implementation FXExitTracker
- (void)mouseExited:(NSEvent*)e { if (!captured) leave(); }
@end

void input_over_ui(bool on) {
  gOverUI = on;
  if (on && !captured) leave();
}

void input_install(NSWindow* window, NSView* view) {
  win = window; pic = view;
  window.acceptsMouseMovedEvents = YES;
  static FXExitTracker* tracker = [FXExitTracker new];
  [view addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
      options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:tracker userInfo:nil]];
  const NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged |
                           NSEventMaskScrollWheel | NSEventMaskMagnify | NSEventMaskRotate;
  [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* e) {
    const bool picture = overContent(e) && !gOverUI;
    switch (e.type) {
      case NSEventTypeLeftMouseDown:
        if (picture) { captured = true; inside = true; send(PTR_DOWN, e); }
        break;
      case NSEventTypeLeftMouseDragged:
        if (captured) send(PTR_MOVE, e);
        break;
      case NSEventTypeLeftMouseUp:
        if (!captured) break;
        captured = false;
        send(PTR_UP, e);
        if (!picture) leave();
        break;
      case NSEventTypeMouseMoved:
        if (picture) { inside = true; send(PTR_MOVE, e); }
        else leave();
        break;
      case NSEventTypeScrollWheel: {
        if (!picture || captured) break;
        // Line-based wheels: the web's DOM_DELTA_LINE factor.
        const double k = e.hasPreciseScrollingDeltas ? 1 : 16;
        engine_scroll(gEngine, (float)(-e.scrollingDeltaX * k), (float)(-e.scrollingDeltaY * k), e.modifierFlags & NSEventModifierFlagShift);
        break;
      }
      case NSEventTypeMagnify:
      case NSEventTypeRotate: {
        bool& act = e.type == NSEventTypeMagnify ? mag : rot;
        if (e.phase & NSEventPhaseBegan) {
          if (!picture) break;
          if (!mag && !rot) engine_gesture_begin(gEngine, e.timestamp);
          act = true;
        } else if (e.phase & NSEventPhaseChanged) {
          if (!act) break;
          if (e.type == NSEventTypeMagnify) engine_magnify(gEngine, (float)e.magnification, e.timestamp);
          else engine_rotate(gEngine, (float)(e.rotation * M_PI / 180), e.timestamp);
        } else if (e.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
          if (!act) break;
          act = false;
          if (!mag && !rot) engine_gesture_end(gEngine);
        }
        break;
      }
      default: break;
    }
    return e;
  }];
}
