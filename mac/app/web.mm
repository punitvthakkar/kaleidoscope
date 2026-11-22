// The interface: the web page in a transparent WKWebView, served from the app bundle
// on a fixed origin, and both directions of the protocol at the top of web/shim.js.
#import "app.h"
#import <WebKit/WebKit.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#include "../platform/audio.h"
#include "../platform/recorder.h"

static NSString* const kScheme = @"fractonaut";
static NSString* const kHost = @"app";
FXWeb* gWeb;

// ---------------- page files, from Resources/web ----------------
@interface FXSchemeHandler : NSObject <WKURLSchemeHandler>
@end
@implementation FXSchemeHandler
- (void)webView:(WKWebView*)wv startURLSchemeTask:(id<WKURLSchemeTask>)task {
  NSURL* url = task.request.URL;
  NSString* path = url.path.length > 1 ? url.path : @"/index.html";
  NSString* root = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"web"];
  NSString* file = [[root stringByAppendingPathComponent:path] stringByStandardizingPath];
  NSData* data = [url.host isEqualToString:kHost] && [file hasPrefix:[root stringByAppendingString:@"/"]]
      ? [NSData dataWithContentsOfFile:file] : nil;
  if (!data) {
    [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil]];
    return;
  }
  static NSDictionary* types = @{@"html": @"text/html; charset=utf-8", @"png": @"image/png", @"js": @"text/javascript",
                                 @"css": @"text/css", @"svg": @"image/svg+xml", @"json": @"application/json",
                                 @"woff2": @"font/woff2", @"jpg": @"image/jpeg", @"webp": @"image/webp"};
  NSString* type = types[file.pathExtension.lowercaseString] ?: @"application/octet-stream";
  NSHTTPURLResponse* res = [[NSHTTPURLResponse alloc] initWithURL:url statusCode:200 HTTPVersion:@"HTTP/1.1"
      headerFields:@{@"Content-Type": type, @"Content-Length": [@(data.length) stringValue], @"Cache-Control": @"no-cache"}];
  [task didReceiveResponse:res];
  [task didReceiveData:data];
  [task didFinish];
}
- (void)webView:(WKWebView*)wv stopURLSchemeTask:(id<WKURLSchemeTask>)task {}
@end

// No browser context menu (Reload, Inspect Element) in an app.
@interface FXWebView : WKWebView
@end
@implementation FXWebView
- (void)willOpenMenu:(NSMenu*)menu withEvent:(NSEvent*)event { [menu removeAllItems]; }
@end

// Retains nothing, so the content controller does not keep FXWeb alive.
@interface FXWeakHandler : NSObject <WKScriptMessageHandler>
@property(weak) id<WKScriptMessageHandler> target;
@end
@implementation FXWeakHandler
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
  [self.target userContentController:c didReceiveScriptMessage:m];
}
@end

static void filmTap(const float* l, const float* r, int frames, uint64_t hostTime) { recorder_append_audio(l, r, frames, hostTime); }

static NSString* json(id obj) {
  NSData* d = [NSJSONSerialization dataWithJSONObject:obj ?: @{} options:0 error:nil];
  return d ? [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] : @"{}";
}

@interface FXWeb () <WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate>
@end

@implementation FXWeb {
  FXWebView* _web;
  FXMetalView* _pic;
  __weak NSWindow* _window;
  bool _ready;
  NSTimer* _stateTimer;
  int _maxSize;
  // camera and hands
  bool _camOn, _handsOn, _camRunning, _handsReadySent;
  int _camId, _handsId;
  // films
  enum { FILM_IDLE, FILM_ON, FILM_STOPPING } _film;
  bool _filmSound;
  NSString* _filmFile;
  IOPMAssertionID _awake;
}

- (NSView*)view { return _web; }

- (instancetype)initWithFrame:(NSRect)frame picture:(FXMetalView*)picture window:(NSWindow*)window {
  if (!(self = [super init])) return nil;
  gWeb = self;
  _pic = picture; _window = window;
  _maxSize = renderer_max_size(picture.renderer);
  [self makeCamera];

  WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
  cfg.websiteDataStore = WKWebsiteDataStore.defaultDataStore;   // persistent: localStorage survives relaunch
  [cfg setURLSchemeHandler:[FXSchemeHandler new] forURLScheme:kScheme];
  FXWeakHandler* h = [FXWeakHandler new];
  h.target = self;
  [cfg.userContentController addScriptMessageHandler:h name:@"fx"];
  if (gDebug) {
    [cfg.userContentController addScriptMessageHandler:h name:@"fxdebug"];
    [cfg.userContentController addUserScript:[[WKUserScript alloc] initWithSource:
        @"(() => { const h = webkit.messageHandlers.fxdebug, say = (t) => { try { h.postMessage({ cmd: 'log', text: t }); } catch (e) {} };"
         "for (const l of ['log', 'warn', 'error']) { const o = console[l]; console[l] = (...a) => { say(l + ': ' + a.map(String).join(' ')); o.apply(console, a); }; }"
         "addEventListener('error', (e) => say('error: ' + e.message + ' ' + e.filename + ':' + e.lineno));"
         "addEventListener('unhandledrejection', (e) => say('rejection: ' + e.reason)); })();"
        injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
  }
  _web = [[FXWebView alloc] initWithFrame:frame configuration:cfg];
  _web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [_web setValue:@NO forKey:@"drawsBackground"];   // the picture shows through
  _web.underPageBackgroundColor = NSColor.clearColor;
  _web.allowsMagnification = NO;
  _web.allowsBackForwardNavigationGestures = NO;
  _web.navigationDelegate = self;
  _web.UIDelegate = self;
  if (gDebug) _web.inspectable = YES;
  [_web addObserver:self forKeyPath:@"URL" options:0 context:nil];

  NSString* hash = [NSUserDefaults.standardUserDefaults stringForKey:@"view"];
  NSString* address = [NSString stringWithFormat:@"%@://%@/index.html%@", kScheme, kHost, hash.length ? [@"#" stringByAppendingString:hash] : @""];
  [_web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:address]]];
  return self;
}

// The URL hash holds the view: keep it for the next launch.
- (void)observeValueForKeyPath:(NSString*)k ofObject:(id)o change:(NSDictionary*)c context:(void*)ctx { [self saveView]; }
- (void)saveView {
  NSURL* u = _web.URL;
  if (![u.scheme isEqualToString:kScheme]) return;
  [NSUserDefaults.standardUserDefaults setObject:u.fragment ?: @"" forKey:@"view"];
}

// ---------------- native -> JS ----------------
- (void)event:(NSString*)name payload:(NSDictionary*)payload {
  if (!_ready) return;
  [_web evaluateJavaScript:[NSString stringWithFormat:@"window.__fx && __fx.event(...%@)", json(@[name, payload ?: @{}])] completionHandler:nil];
}

- (void)event:(NSString*)name json:(NSString*)payloadJSON {
  if (!_ready) return;
  [_web evaluateJavaScript:[NSString stringWithFormat:@"window.__fx && __fx.event(\"%@\", %@)", name, payloadJSON] completionHandler:nil];
}

- (void)reply:(int)rid ok:(bool)ok error:(NSString*)error {
  NSMutableDictionary* p = [@{@"id": @(rid), @"ok": @(ok)} mutableCopy];
  if (error) p[@"error"] = error;
  [self event:@"reply" payload:p];
}

- (void)pushState {
  if (!_ready) return;
  const EngineStats s = engine_stats(gEngine);
  NSDictionary* st = @{@"fps": @(s.fps), @"quality": @(s.quality), @"width": @(s.renderW), @"height": @(s.renderH),
                       @"refreshHz": @(s.refreshHz), @"maxSize": @(_maxSize), @"painted": @(s.painted), @"taps": @(s.taps),
                       @"particles": @(s.particles)};
  [_web evaluateJavaScript:[NSString stringWithFormat:@"window.__fx && __fx.state(%@)", json(st)] completionHandler:nil];
}

- (void)fullscreenChanged:(BOOL)on { [self event:@"fullscreen" payload:@{@"on": @(on)}]; }

// ---------------- navigation: the page and nothing else ----------------
- (void)webView:(WKWebView*)wv decidePolicyForNavigationAction:(WKNavigationAction*)a decisionHandler:(void (^)(WKNavigationActionPolicy))decide {
  NSURL* u = a.request.URL;
  decide([u.scheme isEqualToString:kScheme] && [u.host isEqualToString:kHost] ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (WKWebView*)webView:(WKWebView*)wv createWebViewWithConfiguration:(WKWebViewConfiguration*)c
    forNavigationAction:(WKNavigationAction*)a windowFeatures:(WKWindowFeatures*)f { return nil; }
- (void)webView:(WKWebView*)wv didFinishNavigation:(WKNavigation*)n { [_window makeFirstResponder:_web]; }
- (void)webView:(WKWebView*)wv didFailProvisionalNavigation:(WKNavigation*)n withError:(NSError*)e { NSLog(@"fractonaut: page failed to load: %@", e); }
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)wv {
  NSLog(@"fractonaut: interface process ended");
  _ready = false;
  static double last = -1e9;
  const double now = CACurrentMediaTime();
  if (now - last > 10) { last = now; [wv reload]; }   // once, not in a loop
}

// ---------------- JS -> native ----------------
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
  if ([m.name isEqualToString:@"fxdebug"]) { [self debug:m.body]; return; }
  if (![m.body isKindOfClass:NSArray.class]) return;
  // Engine and renderer work of one batch lands between two frames, in order.
  NSMutableArray<dispatch_block_t>* frame = [NSMutableArray array];
  for (NSDictionary* msg in (NSArray*)m.body)
    if ([msg isKindOfClass:NSDictionary.class]) [self handle:msg frame:frame];
  if (frame.count) [_pic renderThread:^{ for (dispatch_block_t b in frame) b(); }];
}

static double num(id v) { return [v respondsToSelector:@selector(doubleValue)] ? [v doubleValue] : 0; }

- (void)handle:(NSDictionary*)msg frame:(NSMutableArray<dispatch_block_t>*)frame {
  NSString* type = msg[@"type"];
  Engine* e = gEngine;
  Renderer* r = _pic.renderer;
  const bool on = [msg[@"on"] boolValue];
  const int rid = [msg[@"id"] intValue];

  if ([type isEqualToString:@"set"]) {
    NSDictionary* params = msg[@"params"], *system = msg[@"system"];
    [frame addObject:^{
      for (NSString* k in params) {
        id v = params[k];
        if ([v isKindOfClass:NSString.class]) engine_set_param(e, k.UTF8String, 0, [v UTF8String]);
        else engine_set_param(e, k.UTF8String, num(v), nullptr);
      }
      for (NSString* k in system) engine_set_system(e, k.UTF8String, num(system[k]));
    }];
  } else if ([type isEqualToString:@"ready"]) {
    _ready = true;
    [self pushState];
    [self fullscreenChanged:(_window.styleMask & NSWindowStyleMaskFullScreen) != 0];
    if (!_stateTimer) {
      _stateTimer = [NSTimer timerWithTimeInterval:0.2 repeats:YES block:^(NSTimer*) { [self pushState]; }];
      _stateTimer.tolerance = 0.05;
      [NSRunLoop.mainRunLoop addTimer:_stateTimer forMode:NSRunLoopCommonModes];
    }
    [self sendCameras];
    if (gDebug) [self debugScript];
  } else if ([type isEqualToString:@"veil"]) {
    const float s = (float)num(msg[@"seconds"]);
    [frame addObject:^{ renderer_start_veil(r, s); }];
  } else if ([type isEqualToString:@"clear"]) {
    [frame addObject:^{ renderer_clear(r); engine_clear_pens(e); }];
  } else if ([type isEqualToString:@"restore"]) {
    [frame addObject:^{ renderer_restore(r); }];
  } else if ([type isEqualToString:@"breathe"]) {
    [frame addObject:^{ engine_reset_breathing(e); }];
  } else if ([type isEqualToString:@"sound"]) {
    audio_set_sound(on);
    if (on) [self event:@"soundStarted" payload:nil];   // native needs no gesture: audible at once
  } else if ([type isEqualToString:@"root"]) {
    audio_set_root([msg[@"chamber"] intValue]);
  } else if ([type isEqualToString:@"mic"]) {
    if (!on) { audio_set_listen(false); return; }
    __weak FXWeb* me = self;
    audio_set_listen(true, [me, rid](ListenResult res) {
      [me reply:rid ok:res == ListenResult::listening
          error:res == ListenResult::denied ? @"denied" : res == ListenResult::unavailable ? @"unavailable" : nil];
    });
  } else if ([type isEqualToString:@"camera"]) {
    [self camera:on id:rid];
  } else if ([type isEqualToString:@"hands"]) {
    [self hands:on id:rid];
  } else if ([type isEqualToString:@"handsOptions"]) {
    const bool two = [msg[@"twoHands"] boolValue];
    camera_set_two_hands(gCamera, two);
    engine_set_two_hands(e, two);
  } else if ([type isEqualToString:@"cameraDevice"]) {
    NSString* device = msg[@"device"];
    camera_set_device(gCamera, [device isKindOfClass:NSString.class] ? device.UTF8String : "");
  } else if ([type isEqualToString:@"snapshot"]) {
    NSURL* url = downloadsURL(msg[@"file"]);
    if (url) renderer_snapshot(r, ^(NSData* png) {
      if (!png || ![png writeToURL:url atomically:YES]) NSLog(@"fractonaut: could not save %@", url.path);
    });
  } else if ([type isEqualToString:@"export"]) {
    [self exportWidth:[msg[@"width"] intValue] height:[msg[@"height"] intValue] file:msg[@"file"] id:rid];
  } else if ([type isEqualToString:@"record"]) {
    if (on) [self startFilm:msg[@"file"]];
    else [self stopFilm];
  } else if ([type isEqualToString:@"share"]) {
    NSURL* url = downloadsURL(msg[@"file"]);
    if (!url || ![NSFileManager.defaultManager fileExistsAtPath:url.path]) return;
    NSSharingServicePicker* p = [[NSSharingServicePicker alloc] initWithItems:@[url]];
    NSRect b = _web.bounds;
    [p showRelativeToRect:NSMakeRect(NSMidX(b) - 1, NSMidY(b) - 1, 2, 2) ofView:_web preferredEdge:NSRectEdgeMinY];
  } else if ([type isEqualToString:@"copy"]) {
    NSString* text = msg[@"text"];
    if (![text isKindOfClass:NSString.class]) return;
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];
  } else if ([type isEqualToString:@"fullscreen"]) {
    NSWindow* w = _window;
    if (on != ((w.styleMask & NSWindowStyleMaskFullScreen) != 0)) [w toggleFullScreen:nil];
    else [self fullscreenChanged:on];
  } else if ([type isEqualToString:@"awake"]) {
    if (on && !_awake)
      IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, CFSTR("fractonaut is showing"), &_awake);
    else if (!on && _awake) { IOPMAssertionRelease(_awake); _awake = 0; }
  } else if ([type isEqualToString:@"overUI"]) {
    input_over_ui(on);
  }
}

// ---------------- camera and hands ----------------
static NSString* cameraError(CameraError e) {
  return e == CameraError::PermissionDenied ? @"denied" : e == CameraError::NoCamera ? @"unavailable" : @"failed";
}

- (void)makeCamera {
  __weak FXWeb* me = self;
  CameraCallbacks cb;
  cb.hands = [](const HandObs* h, int n, double t) { engine_hands(gEngine, h, n, t); };
  cb.state = [me](bool running, bool ready) { [me cameraRunning:running handsReady:ready]; };
  cb.error = [me](CameraError e) { [me cameraFailed:cameraError(e)]; };
  cb.devices = [me] { [me sendCameras]; };
  gCamera = camera_create(cb);
}

// Protocol `cameras`: what the picker in All settings offers.
- (void)sendCameras {
  NSMutableArray* list = [NSMutableArray array];
  for (const CameraDevice& d : camera_devices())
    [list addObject:@{@"id": @(d.id.c_str()) ?: @"", @"label": @(d.name.c_str()) ?: @"Camera"}];
  [self event:@"cameras" payload:@{@"list": list}];
}

- (void)cameraRunning:(bool)running handsReady:(bool)ready {
  _camRunning = running;
  if (running) [self answerCamera];
  if (ready && _handsOn && !_handsReadySent) { _handsReadySent = true; [self event:@"handsReady" payload:nil]; }
}

- (void)answerCamera {
  if (_camId) { [self reply:_camId ok:true error:nil]; _camId = 0; engine_set_cam_on(gEngine, _camOn); }
  if (_handsId) { [self reply:_handsId ok:true error:nil]; _handsId = 0; }
}

- (void)cameraFailed:(NSString*)error {
  if (_camId) { [self reply:_camId ok:false error:error]; _camId = 0; }
  if (_handsId) { [self reply:_handsId ok:false error:error]; _handsId = 0; }
  else if (_handsOn) [self event:@"handsFailed" payload:nil];
  if (_camOn) [self event:@"cameraFailed" payload:nil];
  _camOn = _handsOn = false;
  camera_set_light(gCamera, false);
  camera_set_hands(gCamera, false);
  engine_set_cam_on(gEngine, false);
  engine_set_hands_enabled(gEngine, false, CACurrentMediaTime());
}

- (void)camera:(bool)on id:(int)rid {
  if (_camId) { [self reply:_camId ok:false error:@"failed"]; _camId = 0; }   // superseded
  _camOn = on;
  camera_set_light(gCamera, on);
  if (!on) { engine_set_cam_on(gEngine, false); if (_handsOn) engine_set_camera_aspect(gEngine, 4 / 3.f); return; }
  _camId = rid;
  if (_camRunning) [self answerCamera];
}

- (void)hands:(bool)on id:(int)rid {
  if (_handsId) { [self reply:_handsId ok:false error:@"failed"]; _handsId = 0; }
  _handsOn = on;
  _handsReadySent = false;
  camera_set_hands(gCamera, on);
  engine_set_hands_enabled(gEngine, on, CACurrentMediaTime());
  if (!on) return;
  if (!_camOn) engine_set_camera_aspect(gEngine, 4 / 3.f);   // hands alone run at 640 x 480
  _handsId = rid;
  if (_camRunning) [self answerCamera];
}

// ---------------- stills ----------------
- (void)exportWidth:(int)w height:(int)h file:(NSString*)file id:(int)rid {
  NSURL* url = downloadsURL(file);
  __weak FXWeb* me = self;
  [_pic exportLongEdge:std::max(w, h) frames:60 done:^(NSData* png) {
    const bool ok = url && png && [png writeToURL:url atomically:YES];
    dispatch_async(dispatch_get_main_queue(), ^{ [me reply:rid ok:ok error:ok ? nil : @"failed"]; });
  }];
}

// ---------------- films ----------------
- (void)startFilm:(NSString*)file {
  NSURL* url = downloadsURL(file);
  const EngineStats st = engine_stats(gEngine);
  if (_film != FILM_IDLE || recorder_active() || !url || st.drawW <= 0) { [self event:@"recordFailed" payload:nil]; return; }
  [NSFileManager.defaultManager removeItemAtURL:url error:nil];
  _filmSound = audio_film_has_sound();
  __weak FXWeb* me = self;
  NSError* err = nil;
  if (!recorder_start(_pic.device, st.drawW, st.drawH, url, _filmSound, audio_sample_rate(),
                      ^(NSError* e) { NSLog(@"fractonaut: film stopped: %@", e); [me endFilm:false]; }, &err)) {
    NSLog(@"fractonaut: film could not start: %@", err);
    [self event:@"recordFailed" payload:nil];
    return;
  }
  _film = FILM_ON;
  _filmFile = file;
  if (_filmSound) audio_film_begin(filmTap);
  const bool sound = _filmSound;
  __block bool started = false;   // render thread
  [_pic setFrameHook:^(id<MTLCommandBuffer> cmd, id<MTLTexture> tex, uint64_t host) {
    recorder_append(cmd, tex, host);
    if (started) return;
    started = true;
    dispatch_async(dispatch_get_main_queue(), ^{ [me event:@"recordStarted" payload:@{@"sound": @(sound)}]; });
  } wants:^bool(uint64_t host) { return recorder_wants(host); }];
}

- (void)stopFilm { [self endFilm:true]; }

// Fade the film's sound out, stop feeding frames, finish the file.
- (void)endFilm:(bool)fade {
  if (_film != FILM_ON) return;
  _film = FILM_STOPPING;
  __weak FXWeb* me = self;
  FXMetalView* pic = _pic;
  NSString* file = _filmFile;
  const bool sound = _filmSound;
  void (^finish)() = ^{
    if (sound) audio_film_end();
    [pic setFrameHook:nil wants:nil];
    [pic renderThread:^{   // after the hook is gone: no more frames
      dispatch_async(dispatch_get_main_queue(), ^{
        recorder_stop(^(NSURL* url, double secs, int64_t dropped, NSError* err) {
          FXWeb* s = me;
          if (!s) return;
          s->_film = FILM_IDLE;
          if (err) NSLog(@"fractonaut: film: %@", err);
          debugLog(@"film %@: %.2f s, %lld frames dropped", url ? @"saved" : @"failed", secs, dropped);
          if (url) [s event:@"recordSaved" payload:@{@"file": file}];
          else [s event:@"recordFailed" payload:nil];
        });
      });
    }];
  };
  if (sound && fade) audio_film_fade(finish);
  else finish();
}

// ---------------- debug (FRACTONAUT_DEBUG) ----------------
- (void)debugScript {
  NSString* js = NSProcessInfo.processInfo.environment[@"FRACTONAUT_DEBUG_JS"];
  if (js.length) [_web evaluateJavaScript:[js stringByAppendingString:@"\n;0"] completionHandler:^(id, NSError* e) { if (e) debugLog(@"debug script: %@", e); }];
}

static void writeImage(NSImage* img, NSString* path) {
  CGImageRef cg = [img CGImageForProposedRect:nil context:nil hints:nil];
  NSData* png = cg ? [[[NSBitmapImageRep alloc] initWithCGImage:cg] representationUsingType:NSBitmapImageFileTypePNG properties:@{}] : nil;
  debugLog(@"%@ %@", [png writeToFile:path atomically:YES] ? @"wrote" : @"could not write", path);
}

- (void)debug:(NSDictionary*)m {
  if (![m isKindOfClass:NSDictionary.class]) return;
  NSString* cmd = m[@"cmd"];
  if ([cmd isEqualToString:@"log"]) debugLog(@"js %@", m[@"text"]);
  else if ([cmd isEqualToString:@"quit"]) [NSApp terminate:nil];
  else if ([cmd isEqualToString:@"dump"]) {
    // The sandbox keeps writes inside the container (or Downloads).
    NSString* base = [NSTemporaryDirectory() stringByAppendingPathComponent:m[@"name"] ?: @"dump"];
    renderer_snapshot(_pic.renderer, ^(NSData* png) {
      NSString* p = [base stringByAppendingString:@"-picture.png"];
      debugLog(@"%@ %@", [png writeToFile:p atomically:YES] ? @"wrote" : @"could not write", p);
    });
    [_web takeSnapshotWithConfiguration:nil completionHandler:^(NSImage* img, NSError* e) {
      if (img) writeImage(img, [base stringByAppendingString:@"-ui.png"]);
      else debugLog(@"web snapshot failed: %@", e);
    }];
  } else if ([cmd isEqualToString:@"stroke"]) {
    // A circle painted through the engine, as a mouse drag would.
    NSSize s = _pic.bounds.size;
    const double cx = s.width / 2, cy = s.height / 2, rad = std::min(s.width, s.height) * 0.25, t0 = CACurrentMediaTime();
    __block int i = 0;
    [NSTimer scheduledTimerWithTimeInterval:1 / 120.0 repeats:YES block:^(NSTimer* t) {
      const double a = i * 0.05, now = CACurrentMediaTime();
      const float x = (float)(cx + rad * cos(a)), y = (float)(cy + rad * sin(a));
      engine_pointer(gEngine, 7, i == 0 ? PTR_DOWN : i == 180 ? PTR_UP : PTR_MOVE, x, y, now, true);
      if (i++ == 180) { [t invalidate]; debugLog(@"stroke done in %.2f s", now - t0); }
    }];
  } else if ([cmd isEqualToString:@"mini"]) {
    [_window miniaturize:nil];
  } else if ([cmd isEqualToString:@"size"]) {
    [_window setContentSize:NSMakeSize(num(m[@"w"]), num(m[@"h"]))];
  }
}
@end
