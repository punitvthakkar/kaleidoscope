// fractonaut for macOS: the application, its window, and what the system offers.
#import "app.h"
#import <QuartzCore/QuartzCore.h>
#include <mach/mach.h>
#include <pwd.h>
#include <unistd.h>
#include <cstdio>
#include <string>

Engine* gEngine;
Camera* gCamera;
std::atomic<bool> gOverUI{false};
bool gDebug;

NSURL* downloadsURL(NSString* file) {
  if (![file isKindOfClass:NSString.class]) return nil;
  file = file.lastPathComponent;
  if (!file.length || [file hasPrefix:@"."]) return nil;
  // The real Downloads folder (the sandbox's home is the container).
  const struct passwd* pw = getpwuid(getuid());
  NSString* home = pw ? @(pw->pw_dir) : NSHomeDirectory();
  return [NSURL fileURLWithPath:[[home stringByAppendingPathComponent:@"Downloads"] stringByAppendingPathComponent:file]];
}

void debugLog(NSString* fmt, ...) {
  if (!gDebug) return;
  va_list a;
  va_start(a, fmt);
  NSString* s = [[NSString alloc] initWithFormat:fmt arguments:a];
  va_end(a);
  fprintf(stderr, "[fx] %s\n", s.UTF8String);
}

static double footprintMB() {
  task_vm_info_data_t info;
  mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
  return info.phys_footprint / 1048576.0;
}

@interface FXApp : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation FXApp {
  NSWindow* _window;
  FXMetalView* _pic;
  FXWeb* _web;
  id<NSObject> _activity;
  NSTimer* _screenTimer, *_debugTimer;
}

static NSMenuItem* item(NSString* title, SEL action, NSString* key, NSEventModifierFlags mods = NSEventModifierFlagCommand) {
  NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
  i.keyEquivalentModifierMask = mods;
  return i;
}

- (void)buildMenu {
  NSMenu* bar = [NSMenu new];
  NSString* name = @"fractonaut";

  NSMenu* app = [[NSMenu alloc] initWithTitle:name];
  [app addItem:item([@"Hide " stringByAppendingString:name], @selector(hide:), @"h")];
  [app addItem:item(@"Hide Others", @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption)];
  [app addItem:item(@"Show All", @selector(unhideAllApplications:), @"")];
  [app addItem:NSMenuItem.separatorItem];
  [app addItem:item([@"Quit " stringByAppendingString:name], @selector(terminate:), @"q")];

  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit addItem:item(@"Undo", @selector(undo:), @"z")];
  [edit addItem:item(@"Redo", @selector(redo:), @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift)];
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItem:item(@"Cut", @selector(cut:), @"x")];
  [edit addItem:item(@"Copy", @selector(copy:), @"c")];
  [edit addItem:item(@"Paste", @selector(paste:), @"v")];
  [edit addItem:item(@"Select All", @selector(selectAll:), @"a")];

  NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
  [window addItem:item(@"Minimize", @selector(performMiniaturize:), @"m")];
  [window addItem:item(@"Zoom", @selector(performZoom:), @"")];
  [window addItem:NSMenuItem.separatorItem];
  [window addItem:item(@"Enter Full Screen", @selector(toggleFullScreen:), @"f", NSEventModifierFlagCommand | NSEventModifierFlagControl)];

  for (NSMenu* m in @[app, edit, window]) {
    NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:m.title action:nil keyEquivalent:@""];
    top.submenu = m;
    [bar addItem:top];
  }
  NSApp.mainMenu = bar;
  NSApp.windowsMenu = window;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
  [self buildMenu];
  gEngine = engine_create();

  NSRect vis = NSScreen.mainScreen.visibleFrame;
  NSRect frame = NSMakeRect(0, 0, round(vis.size.width * 0.8), round(vis.size.height * 0.85));
  _window = [[NSWindow alloc] initWithContentRect:frame
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
        backing:NSBackingStoreBuffered defer:NO];
  _window.titlebarAppearsTransparent = YES;
  _window.titleVisibility = NSWindowTitleHidden;
  _window.title = @"fractonaut";
  _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  _window.backgroundColor = NSColor.blackColor;
  _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
  _window.contentMinSize = NSMakeSize(480, 320);
  _window.releasedWhenClosed = NO;
  _window.delegate = self;
  [_window center];
  [_window setFrameAutosaveName:@"fractonaut"];   // restores the last frame, if any

  NSView* content = _window.contentView;
  NSString* shaders = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"shaders"];
  NSError* err = nil;
  _pic = [[FXMetalView alloc] initWithFrame:content.bounds shaderDir:shaders error:&err];
  if (!_pic) {
    fprintf(stderr, "fractonaut: the renderer could not start: %s\n", err.description.UTF8String);
    exit(1);
  }
  _pic.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [content addSubview:_pic];
  _web = [[FXWeb alloc] initWithFrame:content.bounds picture:_pic window:_window];
  [content addSubview:_web.view];
  _window.initialFirstResponder = _web.view;
  input_install(_window, _pic);

  [self watchSystem];
  [_pic updateScreen];
  [_pic start];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activate];

  if (gDebug) {
    if (getenv("FRACTONAUT_DEBUG_FRONT")) _window.level = NSFloatingWindowLevel;   // keep it visible while a test runs
    _debugTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer*) {
      debugLog(@"%@  mem %.0f MB", [self->_pic debugLine], footprintMB());
    }];
  }
}

// ---------------- system ----------------
- (void)watchSystem {
  NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
  NSOperationQueue* main = NSOperationQueue.mainQueue;
  [nc addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil queue:main usingBlock:^(NSNotification*) { [self power]; }];
  [nc addObserverForName:NSProcessInfoPowerStateDidChangeNotification object:nil queue:main usingBlock:^(NSNotification*) { [self power]; }];
  [nc addObserverForName:NSApplicationDidChangeScreenParametersNotification object:nil queue:main usingBlock:^(NSNotification*) { [self->_pic updateScreen]; }];
  [self power];
  // EDR headroom follows the display's brightness, which posts nothing: look twice a second.
  _screenTimer = [NSTimer timerWithTimeInterval:0.5 repeats:YES block:^(NSTimer*) { [self->_pic updateScreen]; }];
  _screenTimer.tolerance = 0.2;
  [NSRunLoop.mainRunLoop addTimer:_screenTimer forMode:NSRunLoopCommonModes];
}

// Heat and Low Power Mode both lower the engine's detail ceiling; Low Power Mode also caps at 60 Hz.
- (void)power {
  NSProcessInfo* p = NSProcessInfo.processInfo;
  const bool low = p.lowPowerModeEnabled;
  const int thermal = std::max((int)p.thermalState, low ? 1 : 0);
  engine_set_thermal(gEngine, thermal);
  [_pic setMaxRate:low ? 60 : 0];
  debugLog(@"thermal state %ld, low power %d", (long)p.thermalState, low);
}

- (void)windowDidChangeScreen:(NSNotification*)n { [_pic updateScreen]; }
- (void)windowDidChangeBackingProperties:(NSNotification*)n { [_pic updateScreen]; }

// Fully hidden or minimised: no frames at all.
- (void)windowDidChangeOcclusionState:(NSNotification*)n {
  const bool visible = _window.occlusionState & NSWindowOcclusionStateVisible;
  [_pic setRunning:visible];
  debugLog(@"window %@", visible ? @"visible" : @"hidden: frames paused");
}

- (void)windowDidBecomeKey:(NSNotification*)n {
  if (!_activity)
    _activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInteractive reason:@"Drawing the picture"];
}
- (void)windowDidResignKey:(NSNotification*)n {
  if (_activity) { [NSProcessInfo.processInfo endActivity:_activity]; _activity = nil; }
}

- (void)windowDidEnterFullScreen:(NSNotification*)n { [_web fullscreenChanged:YES]; }
- (void)windowDidExitFullScreen:(NSNotification*)n { [_web fullscreenChanged:NO]; }

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)a { return YES; }
- (void)applicationWillTerminate:(NSNotification*)n { [_web saveView]; [_pic stop]; }
@end

// ---------------- app icon: `fractonaut --icon <shaderDir> <out.png>` ----------------
// Renders the first chamber at 1024 x 1024. Parameters come on stdin as "key value"
// lines (render/chambers.mjs, filtered to one chamber).
static int renderIcon(const char* shaderDir, const char* out) {
  gEngine = engine_create();
  char key[128], val[128];
  while (scanf("%127s %127s", key, val) == 2) {
    char* end = nullptr;
    const double v = strtod(val, &end);
    if (end && *end == 0) engine_set_param(gEngine, key, v, nullptr);
    else engine_set_param(gEngine, key, 0, val);
  }
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  NSError* err = nil;
  Renderer* r = renderer_create(dev, @(shaderDir), &err);
  if (!r) { fprintf(stderr, "icon: %s\n", err.description.UTF8String); return 1; }
  const int S = 1024;
  engine_resize(gEngine, S, S, 1);
  MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:S height:S mipmapped:NO];
  td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  td.storageMode = MTLStorageModePrivate;
  id<MTLTexture> tgt = [dev newTextureWithDescriptor:td];
  __block NSData* png = nil;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  int rw = 0, rh = 0;
  for (int i = 0; i < 240; i++) @autoreleasepool {
    const Frame& f = engine_step(gEngine, 1 / 60.0, 0, 0, 0);
    const EngineStats st = engine_stats(gEngine);
    if (st.renderW != rw || st.renderH != rh) { renderer_resize(r, st.renderW, st.renderH, st.echoScale, st.capW, st.capH, st.capEchoW, st.capEchoH); rw = st.renderW; rh = st.renderH; }
    renderer_set_detail(r, 4, 76);
    if (i == 239) renderer_snapshot(r, ^(NSData* d) { png = d; dispatch_semaphore_signal(done); });
    renderer_render(r, f, tgt, nil, 1);
  }
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  return [png writeToFile:@(out) atomically:YES] ? 0 : 1;
}

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    if (argc == 4 && !strcmp(argv[1], "--icon")) return renderIcon(argv[2], argv[3]);
    const char* dbg = getenv("FRACTONAUT_DEBUG");
    gDebug = dbg && *dbg && strcmp(dbg, "0");
    NSApplication* app = NSApplication.sharedApplication;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    static FXApp* delegate = [FXApp new];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
