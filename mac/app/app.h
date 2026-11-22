// The app shell: one window, a Metal view drawing the picture and the web interface
// above it. view.mm owns the render thread and native input; web.mm owns the
// interface and the protocol in web/shim.js; main.mm owns the window and the system.
#pragma once
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#include <atomic>
#include "../core/engine.h"
#include "../render/renderer.h"
#include "../platform/camera.h"

// Shared state. Created on the main thread before anything starts.
extern Engine* gEngine;
extern Camera* gCamera;
extern std::atomic<bool> gOverUI;     // the interface has the pointer (protocol `overUI`)
extern bool gDebug;                   // FRACTONAUT_DEBUG is set

NSURL* downloadsURL(NSString* file);
void debugLog(NSString* fmt, ...) NS_FORMAT_FUNCTION(1, 2);

// ---- the picture (view.mm) ----
@interface FXMetalView : NSView
@property(readonly) Renderer* renderer;
@property(readonly) id<MTLDevice> device;
- (instancetype)initWithFrame:(NSRect)frame shaderDir:(NSString*)dir error:(NSError**)error;
- (void)start;
- (void)stop;
- (void)setRunning:(BOOL)running;          // pause the display link while fully hidden
- (void)setMaxRate:(double)hz;             // 0: the display's own maximum
- (void)updateScreen;                      // EDR headroom and refresh range, from the window's screen
- (void)renderThread:(dispatch_block_t)block;
// A hi-res still, longEdge pixels, after `frames` steps of a copy of the engine, one per
// displayed frame. done (any thread) gets nil on failure.
- (void)exportLongEdge:(int)longEdge frames:(int)frames done:(void (^)(NSData* png))done;   // runs between frames, on the render thread
// nil: capture off. wants (may be nil) picks the frames the hook is given, by display time.
- (void)setFrameHook:(void (^)(id<MTLCommandBuffer>, id<MTLTexture>, uint64_t hostTime))hook
               wants:(bool (^)(uint64_t hostTime))wants;
@property(readonly) float headroom;
// Debug: a one-line report of the last second.
- (NSString*)debugLine;
@end

// Native pointer, wheel and trackpad gestures over the bare picture.
void input_install(NSWindow* window, NSView* view);
void input_over_ui(bool on);              // protocol `overUI`

// ---- the interface (web.mm) ----
@interface FXWeb : NSObject
@property(readonly) NSView* view;
- (instancetype)initWithFrame:(NSRect)frame picture:(FXMetalView*)picture window:(NSWindow*)window;
- (void)event:(NSString*)name payload:(NSDictionary*)payload;
- (void)event:(NSString*)name json:(NSString*)payloadJSON;   // payload already JSON (name must be a plain word)
- (void)fullscreenChanged:(BOOL)on;
- (void)saveView;
@end
extern FXWeb* gWeb;
