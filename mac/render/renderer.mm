// Metal port of the web Renderer. Pass order, uniforms and conditions follow
// index.html's Renderer.render line by line; comments mark native differences.
#import "renderer.h"
#import <QuartzCore/QuartzCore.h>
#import <ImageIO/ImageIO.h>
#include <os/lock.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <vector>
#ifdef FX_PASS_TIMING
#include <map>
#include <mutex>
#include <string>
#endif
#include "shaders/uniforms.h"
#include "sky_layout.h"

namespace {

const int kInFlight = 2;
const MTLPixelFormat kHDR = MTLPixelFormatRGBA16Float;   // the web's RGBA16F
const int kStarsW = 512, kStarsH = 384;                  // the sky places' points of light, one per texel: 196,608
const MTLPixelFormat kSky = MTLPixelFormatRG11B10Float; // their wedge textures: light only, 4 bytes a texel

// sRGB transfer, extended past 1 the usual way (same curve, any magnitude).
float srgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
float linToSrgb(float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f; }
// FINAL_FS tone curves on the max channel (see post.metal).
float tmSdr(float m) { return m > 0.7f ? 0.7f + 0.3f * (1 - expf(-(m - 0.7f) * 3.333f)) : m; }
float tmTo(float m, float W) { return m > 0.7f ? 0.7f + (W - 0.7f) * (1 - expf(-(m - 0.7f) / (W - 0.7f))) : m; }

// Bounds for the flash guard's HDR limit: per channel value x (before the tone
// curve, scaled by its pixel's max channel m = x / t), SDR shows at most Ks * x and
// the HDR shoulder adds at most G * x of linear light. Sampled densely over m and t.
float sdrBound() {
  float k = 0;
  for (int i = 1; i <= 4000; i++) { float m = i * 0.004f; k = fmaxf(k, srgbToLin(fminf(tmSdr(m), 1)) / m); }
  return k;
}
float hdrBound(float W) {
  float g = 0;
  for (int i = 1; i <= 1600; i++) {
    float m = i * 0.01f, ss = fminf(tmSdr(m), 1) / m, sh = fminf(tmTo(m, W), W) / m;
    for (int j = 1; j <= 20; j++) { float x = m * j / 20; g = fmaxf(g, (srgbToLin(x * sh) - srgbToLin(x * ss)) / x); }
  }
  return g;
}

}  // namespace

struct Renderer {
  id<MTLDevice> dev;
  id<MTLCommandQueue> queue;
  id<MTLLibrary> lib;
  id<MTLRenderPipelineState> feedback, raymarch, scene, down, up, lum, adapt, copy, ink, starStep, starDraw, starTrail;
  id<MTLRenderPipelineState> look, stripAdd, stripOver, aura;   // the hands' looks (looks.metal)
  // The sky places: their points (each texel one point: position.xy, velocity.zw) and
  // the wedge textures they are drawn into (sky_layout.h): sharp, and soft with its
  // trails ping-ponged. They exist only while a sky is open (encodeFrame makes and frees them).
  id<MTLTexture> star[2]; int starCur = 0;
  id<MTLTexture> skySharp, skySoft[2]; int skyCur = 0, skyKey[4] = {};
  float skyBoxPrev[4] = {}; bool skyPrevOk = false;
  struct FormatPSO { MTLPixelFormat f; id<MTLRenderPipelineState> finalPSO, blitPSO; } formats[4];
  int nFormats = 0;
  MTLRenderPassDescriptor* rp;

  id<MTLTexture> fb[2]; int cur = 0;
  id<MTLTexture> sceneT, rm, veil, backup, lumCur, lumAd[2];
  std::vector<id<MTLTexture>> bloom;
  std::vector<int> bloomW, bloomH;   // the part of each bloom level in use
  int lumIdx = 0; bool lumReset = true;
  // In use (this detail level) and allocated (the window's largest level). Targets are
  // allocated once per window size; each level draws into their top-left part.
  int W = 0, H = 0, FW = 0, FH = 0; float FS = 1;
  int capW = 0, capH = 0, capFW = 0, capFH = 0;
  bool mips = true;                  // the scene may read the echo's mip levels this frame
  unsigned frameNo = 0;
  int maxTaps = 4, rmSteps = 76;

  id<MTLBuffer> inkBuf[kInFlight]; int slot = 0, doneSlot = 0;
  id<MTLBuffer> lookSeg[kInFlight], lookVert[kInFlight];   // the hands' look: segments and strip vertices
  dispatch_semaphore_t inflight;
  void (^completed)(id<MTLCommandBuffer>);

  // Camera: the newest frame, and the one each in-flight frame holds.
  CVMetalTextureCacheRef camCache = nullptr;
  CVMetalTextureRef camRef = nullptr, camHeld[kInFlight] = {};
  id<MTLTexture> blackTex;
  float camAspect = 16.f / 9.f; bool camReady = false;

  // Calls from other threads are queued and run at the start of the next frame,
  // in order; a frame has not been rendered in between, so this matches the web.
  os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
  enum Op { CLEAR, RESTORE, VEIL } ops[16]; int nOps = 0;
  float veilAmt = 0, veilDur = 1.4f; bool veilOn = false;
  bool backupOn = false; double backupUntil = 0;
  void (^snapDone)(NSData*) = nil;

  bool capture = false;
  id<MTLTexture> capTex, snapTex;
  void (^capHandler)(id<MTLCommandBuffer>, id<MTLTexture>) = nil;
  bool (^capWants)(void) = nil;

  void (^gpuHandler)(double) = nil;

  float Ks = 0, peakFor = -1, G = 0;
  int outW = 0, outH = 0;          // last target size (the canvas)

  // A hi-res export in progress: its own renderer (shared pipelines, own targets),
  // one frame per live frame.
  struct Export {
    Renderer* sub = nullptr; int left = 0, w = 0, h = 0;
    void (^done)(NSData*, int, int) = nil;
    std::atomic<bool> finished{false};
  };
  Export* exp = nullptr;
#ifdef FX_PASS_TIMING
  id<MTLCounterSampleBuffer> tsb[kInFlight]; std::vector<std::string> tnames[kInFlight]; int tslot = 0;
#endif
};

#ifdef FX_PASS_TIMING
// Test-only: GPU time per pass, from timestamp counters at encoder boundaries.
static std::map<std::string, std::pair<double, int>> gPassMs;
static std::mutex gPassM;
static std::string texName(Renderer* r, id<MTLTexture> t) {
  if (t == r->fb[0] || t == r->fb[1]) return "feedback+ink";
  if (t == r->sceneT) return "scene";
  if (t == r->rm) return "raymarch";
  if (t == r->star[0] || t == r->star[1]) return "stars step";
  if (t == r->skySoft[0] || t == r->skySoft[1]) return "stars soft";
  if (t == r->skySharp) return "stars sharp";
  if (t == r->lumCur || t == r->lumAd[0] || t == r->lumAd[1]) return "guard";
  for (size_t i = 0; i < r->bloom.size(); i++) if (t == r->bloom[i]) return "bloom";
  if (t == r->capTex) return "final(cap)";
  if (t == r->snapTex) return "snapshot";
  return "final";
}
static void timeAttach(Renderer* r, MTLRenderPassDescriptor* rp, id<MTLTexture> t) {
  auto& names = r->tnames[r->slot];
  if (!r->tsb[r->slot]) {
    MTLCounterSampleBufferDescriptor* d = [MTLCounterSampleBufferDescriptor new];
    for (id<MTLCounterSet> cs in r->dev.counterSets) if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp]) d.counterSet = cs;
    d.sampleCount = 512; d.storageMode = MTLStorageModeShared;
    r->tsb[r->slot] = [r->dev newCounterSampleBufferWithDescriptor:d error:nil];
  }
  const int i = (int)names.size();
  names.push_back(texName(r, t));
  rp.sampleBufferAttachments[0].sampleBuffer = r->tsb[r->slot];
  rp.sampleBufferAttachments[0].startOfVertexSampleIndex = 256 + i * 2;
  rp.sampleBufferAttachments[0].endOfVertexSampleIndex = 256 + i * 2 + 1;
  rp.sampleBufferAttachments[0].startOfFragmentSampleIndex = i * 2;
  rp.sampleBufferAttachments[0].endOfFragmentSampleIndex = i * 2 + 1;
}
void renderer_pass_times_reset() { std::lock_guard<std::mutex> l(gPassM); gPassMs.clear(); }
void renderer_pass_times_print() {
  std::lock_guard<std::mutex> l(gPassM);
  double tot = 0;
  for (auto& [k, v] : gPassMs) tot += v.first;
  for (auto& [k, v] : gPassMs) if (v.second) printf("      %-13s %6.3f ms\n", k.c_str(), v.first / v.second);
  gPassMs.clear();
}
#endif

// ---------------------------------------------------------------- setup

static NSString* readFile(NSString* dir, NSString* f, NSError** error) {
  return [NSString stringWithContentsOfFile:[dir stringByAppendingPathComponent:f] encoding:NSUTF8StringEncoding error:error];
}

static NSString* shaderSource(NSString* dir, NSError** error) {
  auto rd = [&](NSString* f) { return readFile(dir, f, error); };
  NSString* common = rd(@"common.metal");
  if (!common) return nil;
  NSMutableString* src = [NSMutableString stringWithString:@"#include <metal_stdlib>\nusing namespace metal;\n"];
  for (NSString* f in @[@"uniforms.h", @"prelude.metal", @"feedback.metal", @"raymarch.metal", @"scene.metal", @"post.metal", @"ink.metal", @"looks.metal", @"stars.metal"]) {
    NSString* s = rd(f);
    if (!s) return nil;
    // Paste COMMON at its marker, keeping compiler line numbers true to each file.
    NSArray* parts = [s componentsSeparatedByString:@"//@COMMON\n"];
    [src appendFormat:@"#line 1 \"%@\"\n%@", f, parts[0]];
    if (parts.count > 1) {
      int line = (int)[[parts[0] componentsSeparatedByString:@"\n"] count] + 1;
      [src appendFormat:@"#line 1 \"common.metal\"\n%@\n#line %d \"%@\"\n%@", common, line, f, parts[1]];
    }
    [src appendString:@"\n"];
  }
  return src;
}

static id<MTLRenderPipelineState> pso(Renderer* r, NSString* vs, NSString* fs, MTLPixelFormat fmt, int blend, NSError** error) {
  MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
  d.vertexFunction = [r->lib newFunctionWithName:vs];
  d.fragmentFunction = [r->lib newFunctionWithName:fs];
  d.colorAttachments[0].pixelFormat = fmt;
  if (blend) {   // 1: additive (bloom up, light), 2: max (ink), 3: premultiplied over (satin)
    auto* c = d.colorAttachments[0];
    c.blendingEnabled = YES;
    c.rgbBlendOperation = c.alphaBlendOperation = blend == 2 ? MTLBlendOperationMax : MTLBlendOperationAdd;
    c.sourceRGBBlendFactor = c.sourceAlphaBlendFactor = c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = MTLBlendFactorOne;
    if (blend == 3) c.destinationRGBBlendFactor = c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  }
  return [r->dev newRenderPipelineStateWithDescriptor:d error:error];
}

static Renderer::FormatPSO* formatPSO(Renderer* r, MTLPixelFormat f) {
  for (int i = 0; i < r->nFormats; i++) if (r->formats[i].f == f) return &r->formats[i];
  if (r->nFormats == 4) return nullptr;
  NSError* e = nil;
  Renderer::FormatPSO p{f, pso(r, @"fullscreen_vs", @"final_fs", f, 0, &e), pso(r, @"fullscreen_vs", @"blit_fs", f, 0, &e)};
  if (!p.finalPSO || !p.blitPSO) { NSLog(@"renderer: no pipeline for format %lu: %@", (unsigned long)f, e); return nullptr; }
  r->formats[r->nFormats] = p;
  return &r->formats[r->nFormats++];
}

// In-flight slots, ink buffers, and the block every frame completes with: GPU time,
// release the frame's camera image, free a slot. Frames on one queue complete in
// order, so doneSlot follows slot.
static void setupFrames(Renderer* r) {
  r->rp = [MTLRenderPassDescriptor new];
  r->rp.colorAttachments[0].storeAction = MTLStoreActionStore;
  r->rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  for (auto& b : r->inkBuf) b = [r->dev newBufferWithLength:RENDERER_MAX_SEG * 32 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
  for (auto& b : r->lookSeg) b = [r->dev newBufferWithLength:LOOK_MAX_SEG * 48 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
  for (auto& b : r->lookVert) b = [r->dev newBufferWithLength:LOOK_MAX_VERT * 24 options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
  r->inflight = dispatch_semaphore_create(kInFlight);
  r->completed = ^(id<MTLCommandBuffer> cb) {
    double t = cb.GPUEndTime - cb.GPUStartTime;
    if (r->gpuHandler) r->gpuHandler(t);
    int s = r->doneSlot; r->doneSlot = (s + 1) % kInFlight;
    if (r->camHeld[s]) { CFRelease(r->camHeld[s]); r->camHeld[s] = nullptr; }
    dispatch_semaphore_signal(r->inflight);
  };
}

Renderer* renderer_create(id<MTLDevice> device, NSString* shaderDir, NSError** error) {
  Renderer* r = new Renderer();
  r->dev = device;
  r->queue = [device newCommandQueue];
  NSString* src = shaderSource(shaderDir, error);
  if (!src) { delete r; return nullptr; }
  MTLCompileOptions* opt = [MTLCompileOptions new];
  // Fast arithmetic, but NaN must survive: FEEDBACK_FS scrubs NaN from the loop.
  opt.mathMode = MTLMathModeRelaxed;
  NSError* e = nil;
  r->lib = [device newLibraryWithSource:src options:opt error:&e];
  if (!r->lib) { if (error) *error = e; delete r; return nullptr; }
  if (e) NSLog(@"renderer: shader warnings:\n%@", e.localizedDescription);

  r->feedback = pso(r, @"fullscreen_vs", @"feedback_fs", kHDR, 0, error);
  r->raymarch = pso(r, @"fullscreen_vs", @"raymarch_fs", kHDR, 0, error);
  r->scene = pso(r, @"fullscreen_vs", @"scene_fs", kHDR, 0, error);
  r->down = pso(r, @"fullscreen_vs", @"down_fs", kHDR, 0, error);
  r->up = pso(r, @"fullscreen_vs", @"up_fs", kHDR, 1, error);
  r->lum = pso(r, @"fullscreen_vs", @"lum_fs", kHDR, 0, error);
  r->adapt = pso(r, @"fullscreen_vs", @"adapt_fs", kHDR, 0, error);
  r->copy = pso(r, @"fullscreen_vs", @"copy_fs", kHDR, 0, error);
  r->ink = pso(r, @"ink_vs", @"ink_fs", kHDR, 2, error);
  r->starStep = pso(r, @"fullscreen_vs", @"stars_step_fs", MTLPixelFormatRGBA32Float, 0, error);
  r->starDraw = pso(r, @"stars_vs", @"stars_fs", kSky, 1, error);
  r->starTrail = pso(r, @"fullscreen_vs", @"stars_trail_fs", kSky, 0, error);
  r->look = pso(r, @"look_vs", @"look_fs", kHDR, 1, error);
  r->stripAdd = pso(r, @"strip_vs", @"strip_fs", kHDR, 1, error);
  r->stripOver = pso(r, @"strip_vs", @"strip_fs", kHDR, 3, error);
  r->aura = pso(r, @"fullscreen_vs", @"aura_fs", kHDR, 1, error);
  if (!r->feedback || !r->raymarch || !r->scene || !r->down || !r->up || !r->lum || !r->adapt || !r->copy || !r->ink
      || !r->starStep || !r->starDraw || !r->starTrail || !r->look || !r->stripAdd || !r->stripOver || !r->aura
      || !formatPSO(r, kHDR) || !formatPSO(r, MTLPixelFormatRGBA8Unorm)) { delete r; return nullptr; }

  MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
  r->blackTex = [device newTextureWithDescriptor:d];
  const uint8_t black[4] = {0, 0, 0, 0};
  [r->blackTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:black bytesPerRow:4];
  CVMetalTextureCacheCreate(nullptr, nullptr, device, nullptr, &r->camCache);

  setupFrames(r);
  r->Ks = sdrBound();
  return r;
}

void renderer_destroy(Renderer* r) {
  if (!r) return;
  if (r->exp) { renderer_finish(r); while (r->exp->left == 0 && !r->exp->finished) usleep(500); renderer_destroy(r->exp->sub); delete r->exp; }
  for (int i = 0; i < kInFlight; i++) dispatch_semaphore_wait(r->inflight, DISPATCH_TIME_FOREVER);
  renderer_finish(r);
  if (r->camRef) CFRelease(r->camRef);
  for (auto& c : r->camHeld) if (c) CFRelease(c);
  if (r->camCache) CFRelease(r->camCache);
  for (int i = 0; i < kInFlight; i++) dispatch_semaphore_signal(r->inflight);
  delete r;
}

// ---------------------------------------------------------------- targets

// Every texture the renderer makes: sampled and drawn into.
static id<MTLTexture> makeTex(Renderer* r, int w, int h, MTLPixelFormat fmt, bool mips = false,
                              MTLStorageMode mode = MTLStorageModePrivate) {
  MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:mips];
  d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  d.storageMode = mode;
  return [r->dev newTextureWithDescriptor:d];
}
static id<MTLTexture> target(Renderer* r, int w, int h, bool mips) { return makeTex(r, w, h, kHDR, mips); }

// A pass drawing into the top-left w × h of t (0: all of it).
static id<MTLRenderCommandEncoder> pass(Renderer* r, id<MTLCommandBuffer> cb, id<MTLTexture> t, MTLLoadAction load,
                                        int w = 0, int h = 0) {
  r->rp.colorAttachments[0].texture = t;
  r->rp.colorAttachments[0].loadAction = load;
#ifdef FX_PASS_TIMING
  timeAttach(r, r->rp, t);
#endif
  id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:r->rp];
  if (w > 0 && h > 0 && (w != (int)t.width || h != (int)t.height))
    [e setViewport:(MTLViewport){0, 0, (double)w, (double)h, 0, 1}];
  return e;
}

// The used part of t when w × h of it is in use, for the shaders' TexK.
static simd_float4 partK(id<MTLTexture> t, int w, int h) {
  return simd_make_float4((float)w / t.width, (float)h / t.height, 0.5f / t.width, 0.5f / t.height);
}
static void setK(id<MTLRenderCommandEncoder> e, TexK k) { [e setFragmentBytes:&k length:sizeof k atIndex:3]; }

static void draw(id<MTLRenderCommandEncoder> e) { [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; }

#ifdef FX_PASS_TIMING
static Renderer* gTimeR = nullptr;
#endif
static void mipmaps(id<MTLCommandBuffer> cb, id<MTLTexture> t) {
#ifdef FX_PASS_TIMING
  MTLBlitPassDescriptor* bd = [MTLBlitPassDescriptor new];
  if (gTimeR) {
    Renderer* r = gTimeR; auto& names = r->tnames[r->slot]; const int i = (int)names.size(); names.push_back("mipmaps");
    bd.sampleBufferAttachments[0].sampleBuffer = r->tsb[r->slot];
    bd.sampleBufferAttachments[0].startOfEncoderSampleIndex = i * 2;
    bd.sampleBufferAttachments[0].endOfEncoderSampleIndex = i * 2 + 1;
  }
  id<MTLBlitCommandEncoder> b = gTimeR ? [cb blitCommandEncoderWithDescriptor:bd] : [cb blitCommandEncoder];
#else
  id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
#endif
  [b generateMipmapsForTexture:t];
  [b endEncoding];
}

static void clearTo(Renderer* r, id<MTLCommandBuffer> cb, id<MTLTexture> t) {
  [pass(r, cb, t, MTLLoadActionClear) endEncoding];
  if (t.mipmapLevelCount > 1) mipmaps(cb, t);
}

// Copies the used sw × sh of src into the top-left dw × dh of dst, scaling to fit.
static void copyInto(Renderer* r, id<MTLCommandBuffer> cb, id<MTLTexture> src, int sw, int sh,
                     id<MTLTexture> dst, int dw, int dh) {
  id<MTLRenderCommandEncoder> e = pass(r, cb, dst, MTLLoadActionDontCare, dw, dh);
  [e setRenderPipelineState:r->copy];
  CopyU u{{(float)dw, (float)dh}};
  [e setFragmentBytes:&u length:sizeof u atIndex:1];
  setK(e, {{partK(src, sw, sh)}});
  [e setFragmentTexture:src atIndex:0];
  draw(e);
  [e endEncoding];
  if (dst.mipmapLevelCount > 1) mipmaps(cb, dst);
}

// Allocates every render target at the window's largest size up front (the web makes
// the veil, undo copy and lattice buffer on demand, which here would mean allocations
// on the render thread mid-performance). A detail level change only rescales the echo.
bool renderer_resize(Renderer* r, int w, int h, float fs, int capW, int capH, int capFW, int capFH) {
  w = std::max(16, w); h = std::max(16, h);
  const int fw = std::max(16, (int)lroundf(w * fs)), fh = std::max(16, (int)lroundf(h * fs));
  capW = std::max(capW, w); capH = std::max(capH, h); capFW = std::max(capFW, fw); capFH = std::max(capFH, fh);
  const bool sameCap = r->sceneT && capW == r->capW && capH == r->capH && capFW == r->capFW && capFH == r->capFH;
  if (sameCap && w == r->W && h == r->H && fs == r->FS) return true;

  std::vector<int> bloomW, bloomH;
  for (int i = 0, bw = capW, bh = capH; i < 6 && bw > 8 && bh > 8; i++) {
    bw = std::max(1, bw >> 1); bh = std::max(1, bh >> 1);
    bloomW.push_back(std::max(1, w >> (i + 1))); bloomH.push_back(std::max(1, h >> (i + 1)));
  }

  if (sameCap) {
    // Same allocation: carry the echo over at its new size through the other buffer.
    id<MTLCommandBuffer> cb = [r->queue commandBuffer];
    copyInto(r, cb, r->fb[r->cur], r->FW, r->FH, r->fb[1 - r->cur], fw, fh);
    [cb commit];
    r->cur = 1 - r->cur;
  } else {
    id<MTLTexture> nfb[2] = {target(r, capFW, capFH, true), target(r, capFW, capFH, true)};
    id<MTLTexture> sc = target(r, capW, capH, false), veil = target(r, capW, capH, false), backup = target(r, capFW, capFH, false);
    id<MTLTexture> rm = target(r, capFW, capFH, false);   // full echo size at full detail (encodeFrame)
    std::vector<id<MTLTexture>> bloom;
    for (int i = 0, bw = capW, bh = capH; i < (int)bloomW.size(); i++) {
      bw = std::max(1, bw >> 1); bh = std::max(1, bh >> 1);
      bloom.push_back(target(r, bw, bh, false));
    }
    bool ok = nfb[0] && nfb[1] && sc && veil && backup && rm;
    for (auto& b : bloom) ok = ok && b;
    if (!r->lumCur) { r->lumCur = target(r, 1, 1, false); r->lumAd[0] = target(r, 1, 1, false); r->lumAd[1] = target(r, 1, 1, false); }
    if (!ok || !r->lumCur || !r->lumAd[0] || !r->lumAd[1]) return false;

    id<MTLCommandBuffer> cb = [r->queue commandBuffer];
    for (id<MTLTexture> t : {nfb[0], nfb[1], sc, rm}) clearTo(r, cb, t);
    if (r->W == 0) for (id<MTLTexture> t : {r->lumCur, r->lumAd[0], r->lumAd[1]}) clearTo(r, cb, t);
    for (auto& b : bloom) clearTo(r, cb, b);
    if (r->fb[r->cur]) copyInto(r, cb, r->fb[r->cur], r->FW, r->FH, nfb[0], fw, fh);
    [cb commit];

    r->fb[0] = nfb[0]; r->fb[1] = nfb[1]; r->cur = 0;
    r->sceneT = sc; r->veil = veil; r->backup = backup; r->rm = rm; r->bloom = bloom;
    r->capW = capW; r->capH = capH; r->capFW = capFW; r->capFH = capFH;
  }
  r->bloomW = bloomW; r->bloomH = bloomH;
  os_unfair_lock_lock(&r->lock);
  r->veilOn = false; r->veilAmt = 0; r->backupOn = false;
  os_unfair_lock_unlock(&r->lock);
  r->W = w; r->H = h; r->FW = fw; r->FH = fh; r->FS = fs;
  r->mips = true;   // the copies above built them
  return true;
}

// ---------------------------------------------------------------- state calls

static void queueOp(Renderer* r, Renderer::Op op) {
  os_unfair_lock_lock(&r->lock);
  if (op == Renderer::VEIL) r->veilOn = true;
  if (r->nOps < 16) r->ops[r->nOps++] = op;
  os_unfair_lock_unlock(&r->lock);
}

void renderer_clear(Renderer* r) { queueOp(r, Renderer::CLEAR); }
void renderer_restore(Renderer* r) { queueOp(r, Renderer::RESTORE); }

void renderer_start_veil(Renderer* r, float duration) {
  os_unfair_lock_lock(&r->lock);
  r->veilAmt = 1;
  r->veilDur = std::max(0.2f, duration);
  os_unfair_lock_unlock(&r->lock);
  queueOp(r, Renderer::VEIL);
}

void renderer_fade_veil(Renderer* r, float dt) {
  os_unfair_lock_lock(&r->lock);
  r->veilAmt = std::max(0.f, r->veilAmt - dt / r->veilDur);
  if (r->veilAmt == 0) r->veilOn = false;
  os_unfair_lock_unlock(&r->lock);
}

void renderer_set_detail(Renderer* r, int taps, int steps) { r->maxTaps = taps; r->rmSteps = steps; }

static void runOps(Renderer* r, id<MTLCommandBuffer> cb) {
  os_unfair_lock_lock(&r->lock);
  Renderer::Op ops[16]; int n = r->nOps; r->nOps = 0;
  for (int i = 0; i < n; i++) ops[i] = r->ops[i];
  os_unfair_lock_unlock(&r->lock);
  double now = CACurrentMediaTime();
  if (r->backupOn && now > r->backupUntil) r->backupOn = false;   // undo window
  if (!r->sceneT) return;
  for (int i = 0; i < n; i++) {
    if (ops[i] == Renderer::CLEAR) {
      copyInto(r, cb, r->fb[r->cur], r->FW, r->FH, r->backup, r->FW, r->FH);
      r->backupOn = true; r->backupUntil = now + 8;
      clearTo(r, cb, r->fb[0]); clearTo(r, cb, r->fb[1]);
      r->mips = true;
    } else if (ops[i] == Renderer::RESTORE) {
      if (r->backupOn) { copyInto(r, cb, r->backup, r->FW, r->FH, r->fb[r->cur], r->FW, r->FH); r->mips = true; }
    } else {
      copyInto(r, cb, r->sceneT, r->W, r->H, r->veil, r->W, r->H);   // the composite scene of the last frame
    }
  }
}

// ---------------------------------------------------------------- frame

// Chambers whose fold barely shrinks the echo: no mip levels, the scene reads level 0.
static bool noMips(int mode) { return mode == MODE_pool || mode == MODE_planet || mode == MODE_corridor; }

static void setCommon(CommonU& u, const Frame& f, int w, int h) {
  const Fold& fd = f.fold;
  u.uRes = simd_make_float2(w, h);
  u.uMode = fd.mode; u.uSym = fd.sym; u.uMirror = fd.mirror ? 1 : 0; u.uSpin = fd.spin;
  u.uCenter = simd_make_float2(fd.cx, fd.cy);
  u.uViewZoom = fd.vz;
  u.uWarp = simd_make_float4(fd.warpAmt, fd.warpScale, fd.warpT, 0);
  u.uKifsIters = fd.kIters;
  u.uKifs = simd_make_float2(fd.kScale, fd.kAngle);
  u.uKifsOff = simd_make_float2(fd.kOffX, fd.kOffY);
  u.uHyp = simd_make_float4(fd.hypC, fd.hypR, fd.hypAx, fd.hypAy);
  u.uDroste = simd_make_float2(fd.drL, fd.drPhase);
  u.uTile = fd.tile;
  u.uSpinCS = simd_make_float2(cos((double)fd.spin), sin((double)fd.spin));
  for (int i = 0; i < 10; i++) { double a = fd.kAngle / (1 + 0.45 * i); u.uKifsCS[i] = simd_make_float2(cos(a), sin(a)); }
  u.uPalA = simd_make_float3(f.pal.a[0], f.pal.a[1], f.pal.a[2]);
  u.uPalB = simd_make_float3(f.pal.b[0], f.pal.b[1], f.pal.b[2]);
  u.uPalC = simd_make_float3(f.pal.c[0], f.pal.c[1], f.pal.c[2]);
  u.uPalD = simd_make_float3(f.pal.d[0], f.pal.d[1], f.pal.d[2]);
}

// Decay × saturation × hue rotation about the gray axis, as one matrix (column-major).
static simd_float3x3 colorMat(float decay, float sat, float hue) {
  const double c = cos((double)hue), s = sin((double)hue), k = (1 - c) / 3, q = sqrt(1.0 / 3) * s;
  const double h[9] = {c + k, k - q, k + q, k + q, c + k, k - q, k - q, k + q, c + k};
  const double lr = 0.299 * (1 - sat), lg = 0.587 * (1 - sat), lb = 0.114 * (1 - sat);
  const double S[9] = {lr + sat, lr, lr, lg, lg + sat, lg, lb, lb, lb + sat};
  simd_float3x3 m;
  for (int col = 0; col < 3; col++)
    for (int row = 0; row < 3; row++) {
      double v = 0;
      for (int i = 0; i < 3; i++) v += S[i * 3 + row] * h[col * 3 + i];
      m.columns[col][row] = (float)(v * decay);
    }
  return m;
}

static simd_float3x3 rmRot(double a) {
  const double x = M_SQRT1_2, y = M_SQRT1_2, z = 0, c = cos(a), s = sin(a), t = 1 - c;
  const double m[9] = {t * x * x + c, t * x * y + s * z, t * x * z - s * y,
                       t * x * y - s * z, t * y * y + c, t * y * z + s * x,
                       t * x * z + s * y, t * y * z - s * x, t * z * z + c};
  simd_float3x3 o;
  for (int i = 0; i < 9; i++) o.columns[i / 3][i % 3] = (float)m[i];
  return o;
}

static id<MTLTexture> ensure(Renderer* r, id<MTLTexture> t, int w, int h, MTLPixelFormat fmt, MTLStorageMode mode) {
  if (t && (int)t.width == w && (int)t.height == h && t.pixelFormat == fmt) return t;
  return makeTex(r, w, h, fmt, false, mode);
}

static void finalPass(Renderer* r, id<MTLCommandBuffer> cb, id<MTLTexture> out, const FinalU& fu,
                      bool doBloom, bool guard) {
  Renderer::FormatPSO* p = formatPSO(r, out.pixelFormat);
  if (!p) return;
  id<MTLRenderCommandEncoder> e = pass(r, cb, out, MTLLoadActionDontCare);
  [e setRenderPipelineState:p->finalPSO];
  [e setFragmentBytes:&fu length:sizeof fu atIndex:1];
  const simd_float4 ks = partK(r->sceneT, r->W, r->H);
  setK(e, {{ks, doBloom ? partK(r->bloom[0], r->bloomW[0], r->bloomH[0]) : ks}});
  [e setFragmentTexture:r->sceneT atIndex:0];
  [e setFragmentTexture:doBloom ? r->bloom[0] : r->sceneT atIndex:1];
  [e setFragmentTexture:r->lumCur atIndex:2];
  [e setFragmentTexture:r->lumAd[r->lumIdx] atIndex:3];
  draw(e);
  [e endEncoding];
}

static NSData* pngFromRGBA8(const uint8_t* px, int w, int h) {
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, px, (size_t)w * h * 4, nullptr);
  CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault, dp, nullptr, false, kCGRenderingIntentDefault);
  NSMutableData* data = [NSMutableData data];
  CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, CFSTR("public.png"), 1, nullptr);
  CGImageDestinationAddImage(dst, img, nullptr);
  bool ok = CGImageDestinationFinalize(dst);
  CFRelease(dst); CGImageRelease(img); CGDataProviderRelease(dp); CGColorSpaceRelease(cs);
  return ok ? data : nil;
}

// Hand looks (web: drawLooks): segments and strips, over `copies` mirror images (the echo
// feed) or once (on top), into e's top-left w × h of target t. Light adds; satin ribbons
// on top are laid over. The look's batches were copied into this slot's buffers already.
static void drawLooks(Renderer* r, id<MTLRenderCommandEncoder> e, id<MTLTexture> t, const LookData& L, const Fold& fold,
                      int copies, int w, int h, float gain, bool onTop, int slot) {
  LookU u{};
  u.uRes = simd_make_float2(w, h);
  u.uGain = gain;
  u.uCopies = copies;
  u.uLookMirror = fold.mirror ? 1 : 0;
  u.uSym = std::max(1.f, fold.sym);
  u.uSpin = fold.spin;
  u.uFish = onTop ? 0 : fold.fish;
  u.uCenter = simd_make_float2(fold.cx, fold.cy);
  u.uT = L.time;
  const int nSeg = std::min(L.nSeg, (int)LOOK_MAX_SEG), nVert = std::min(L.nVert, (int)LOOK_MAX_VERT);
  if (nSeg > 0) {
    [e setRenderPipelineState:r->look];
    [e setVertexBytes:&u length:sizeof u atIndex:1];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    [e setVertexBuffer:r->lookSeg[slot] offset:0 atIndex:2];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:(NSUInteger)nSeg * copies];
  }
  if (nVert > 0) {
    [e setVertexBytes:&u length:sizeof u atIndex:1];
    [e setVertexBuffer:r->lookVert[slot] offset:0 atIndex:2];
    for (int i = 0; i < std::min(L.nStrip, (int)LOOK_MAX_STRIP); i++) {
      const int first = L.stripFirst[i], count = L.stripCount[i];
      if (first < 0 || count <= 0 || first + count > nVert) continue;
      u.uKind = L.stripKind[i];
      [e setRenderPipelineState:u.uKind == 0 && onTop ? r->stripOver : r->stripAdd];
      [e setFragmentBytes:&u length:sizeof u atIndex:1];
      [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:first vertexCount:count instanceCount:copies];
    }
  }
  if (onTop && L.auraN > 0) {
    u.uBreath = L.breath;
    u.uHands = std::min(L.auraN, 2);
    for (int i = 0; i < 2; i++) u.uHand[i] = simd_make_float4(L.auraHand[i * 4], L.auraHand[i * 4 + 1], L.auraHand[i * 4 + 2], L.auraHand[i * 4 + 3]);
    for (int i = 0; i < 42; i++) u.uPts[i] = simd_make_float2(L.auraPts[i * 2], L.auraPts[i * 2 + 1]);
    // Only the box around the hands (picture space to pixels; GL's y grows upward, Metal's down).
    auto toX = [&](float x) { return (int)floorf((x * h / w * 0.5f + 0.5f) * w + 0.5f); };
    auto toY = [&](float y) { return (int)floorf((y * 0.5f + 0.5f) * h + 0.5f); };
    const int x0 = std::max(0, toX(L.box[0])), y0 = std::max(0, toY(L.box[1])), x1 = std::min(w, toX(L.box[2])), y1 = std::min(h, toY(L.box[3]));
    if (x1 > x0 && y1 > y0) {
      [e setRenderPipelineState:r->aura];
      [e setFragmentBytes:&u length:sizeof u atIndex:1];
      [e setScissorRect:(MTLScissorRect){(NSUInteger)x0, (NSUInteger)(h - y1), (NSUInteger)(x1 - x0), (NSUInteger)(y1 - y0)}];
      draw(e);
      [e setScissorRect:(MTLScissorRect){0, 0, t.width, t.height}];
    }
  }
}

// Encodes one frame into cb. out may be nil (no final pass); snapW/H > 0 adds an
// 8-bit SDR copy of the final pass for a pending snapshot.
static void encodeFrame(Renderer* r, id<MTLCommandBuffer> cb, const Frame& f, id<MTLTexture> out, float headroom,
                        int snapW, int snapH) {
  const Params& P = f.P;
  const Fold& fold = f.fold;
  r->frameNo++;
  const int slot = r->slot;

  // 1. raymarched fractal source
  const bool useRm = P.gen == GEN_fractal3d;
  // Native: at full detail the fractal is raymarched at the echo's own size, not half of it:
  // its facets lose their staircase edges, for about 1.2 ms more at 3024 x 1964.
  const int rmDiv = r->maxTaps >= 4 ? 1 : 2;
  const int rmW = std::max(16, r->FW / rmDiv), rmH = std::max(16, r->FH / rmDiv);
  if (useRm) {
    CommonU cu; setCommon(cu, f, rmW, rmH);
    RaymarchU u{};
    u.uRmRot = rmRot(0.35 + 0.25 * sin(f.rmT * 0.07));
    u.uRmScale = P.rmScale; u.uRmZ = f.rmZ; u.uRmT = f.rmT; u.uRmSteps = r->rmSteps;
    // The Mathematics shapes: c, the slice and the surface flight's camera come from the engine.
    u.uRmQ = simd_make_float4(f.rmQ[0], f.rmQ[1], f.rmQ[2], f.rmQ[3]);
    u.uRmCamP = simd_make_float4(f.rmCam[0], f.rmCam[1], f.rmCam[2], f.rmCam[3]);
    u.uRmCamF = simd_make_float4(f.rmCam[4], f.rmCam[5], f.rmCam[6], f.rmCam[7]);
    u.uRmCamU = simd_make_float4(f.rmCam[8], f.rmCam[9], f.rmCam[10], f.rmSurf ? 1.f : 0.f);
    u.uRmShape = P.rmShape; u.uRmLook = P.rmLook;
    u.uRmSlice = f.rmSlice; u.uRmDetail = P.rmDetail;
    u.uRmPad = P.foldInLoop ? 1.f : 0.f;   // the feedback pass folds into one wedge: draw only that
    u.uRmYaw = f.rmView[0]; u.uRmPitch = f.rmView[1]; u.uRmZoom = f.rmView[2];
    id<MTLRenderCommandEncoder> e = pass(r, cb, r->rm, MTLLoadActionDontCare, rmW, rmH);
    [e setRenderPipelineState:r->raymarch];
    [e setFragmentBytes:&cu length:sizeof cu atIndex:0];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    draw(e);
    [e endEncoding];
  }

  // 1b. the sky places: every point one step, then drawn once into the wedge textures
  // (soft, with its trails carried on; and sharp), which the scene pass folds out.
  const bool stars = P.stars > 0;
  StarsU su{};
  bool starsNew = false;
  if (stars && !r->star[0]) {
    // Positions need full floats: halves would step visibly.
    for (auto& t : r->star) t = makeTex(r, kStarsW, kStarsH, MTLPixelFormatRGBA32Float);
    r->starCur = 0; starsNew = true;
  } else if (!stars && r->star[0]) {
    r->star[0] = r->star[1] = nil;            // frames still in flight keep their own references
    r->skySharp = r->skySoft[0] = r->skySoft[1] = nil;
    r->skyKey[0] = 0;
  }
  const float sym = std::max(2.f, fold.sym);
  SkyLayout skyL{}, skyS{};
  if (stars && r->star[0] && r->star[1]) {
    // The wedge textures, sized by the render and the mirrors only (sky_layout.h).
    int tw, th, sw, sh;
    sky_tex_size(sym, fold.mirror, r->W, r->H, 1.0f, 4096, tw, th);
    sky_tex_size(sym, fold.mirror, r->W, r->H, 0.5f, 4096, sw, sh);
    const int key[4] = {tw, th, sw, sh};
    if (!r->skySharp || memcmp(key, r->skyKey, sizeof key) != 0) {
      r->skySharp = makeTex(r, tw, th, kSky); r->skySoft[0] = makeTex(r, sw, sh, kSky); r->skySoft[1] = makeTex(r, sw, sh, kSky);
      memcpy(r->skyKey, key, sizeof key);
      r->skyCur = 0; r->skyPrevOk = false;
    }
    skyL = sky_layout(sym, fold.mirror, r->W, r->H, f.starView[0], tw);
    skyS = sky_layout(sym, fold.mirror, r->W, r->H, f.starView[0], sw);

    su.sCores = std::min(f.starCores, 8); su.sAuto = std::min(f.starAuto, su.sCores);
    su.sKind = P.starKind; su.sTime = f.starTime; su.sPulse = f.starPulse;
    su.sBlast = simd_make_float4(f.starBlast[0], f.starBlast[1], f.starBlast[2], 0);
    su.sView = simd_make_float4(f.starView[0], f.starView[1], f.starView[2], 0);
    su.sSym = sym; su.sMirror = fold.mirror ? 1 : 0;
    for (int i = 0; i < su.sCores; i++) {
      su.sCore[i] = simd_make_float4(f.starCore[i * 4], f.starCore[i * 4 + 1], f.starCore[i * 4 + 2], f.starCore[i * 4 + 3]);
      su.sVel[i] = simd_make_float4(f.starVel[i * 2], f.starVel[i * 2 + 1], 0, 0);
    }
    su.sPalA = simd_make_float3(f.pal.a[0], f.pal.a[1], f.pal.a[2]); su.sPalB = simd_make_float3(f.pal.b[0], f.pal.b[1], f.pal.b[2]);
    su.sPalC = simd_make_float3(f.pal.c[0], f.pal.c[1], f.pal.c[2]); su.sPalD = simd_make_float3(f.pal.d[0], f.pal.d[1], f.pal.d[2]);
    su.sDim = simd_make_float2(kStarsW, kStarsH);
    su.sReset = f.starsReset || starsNew ? 1 : 0;   // fresh textures hold nothing yet: seed every point
    su.sDt = std::min(f.dt, 0.034f);          // a long frame must not fling the inner orbits apart
    su.sSeed = (float)(r->frameNo % 65536);
    // Trails keep light / (1 - decay): scale back so brightness holds at any trail length.
    su.sBright = 0.16f * P.stars * std::max(1 - f.decay, 0.004f);
    su.sPalShift = f.palPhase;
    const float zs = std::min(std::max(sqrtf(f.starView[0]), 0.7f), 2.2f);
    su.sSize = std::max(1.5f, r->FH / 480.f) * zs;
    su.sHeadBright = 0.75f * P.stars;
    // Screen pixels per 1000-high pixel: sharp points keep the same core at any resolution.
    su.sPx = std::max(1.f, r->H / 1000.f);
    su.sEchoKeep = 1;
    su.sReach = (float)std::min(2.6, sky_corner(r->W, r->H) / std::max(f.starView[0], 1e-3f));
    su.sHeadSize = std::max(3.f, 4.5f * su.sPx) * zs; // the sprite: just room for the core and its halo

    id<MTLRenderCommandEncoder> se = pass(r, cb, r->star[1 - r->starCur], MTLLoadActionDontCare);
    [se setRenderPipelineState:r->starStep];
    [se setFragmentBytes:&su length:sizeof su atIndex:1];
    [se setFragmentTexture:r->star[r->starCur] atIndex:0];
    draw(se);
    [se endEncoding];
    r->starCur = 1 - r->starCur;

    // The soft light: last frame's, carried on (turned and zoomed as the echo is, faded),
    // then every point's added.
    su.sHead = 0;
    su.sRes = simd_make_float2(r->skySoft[0].width, r->skySoft[0].height);
    su.sBB = simd_make_float4(skyS.box[0], skyS.box[1], skyS.box[2], skyS.box[3]);
    su.sBBPrev = r->skyPrevOk ? simd_make_float4(r->skyBoxPrev[0], r->skyBoxPrev[1], r->skyBoxPrev[2], r->skyBoxPrev[3]) : su.sBB;
    su.sFb = simd_make_float4(cosf(f.fbRot), sinf(f.fbRot), std::max(f.fbZoom, 0.5f), r->skyPrevOk ? f.decay : 0.f);
    su.sTexDen = skyS.den; su.sTexK = skyS.k;
    se = pass(r, cb, r->skySoft[1 - r->skyCur], MTLLoadActionDontCare);
    [se setRenderPipelineState:r->starTrail];
    [se setFragmentBytes:&su length:sizeof su atIndex:1];
    [se setFragmentTexture:r->skySoft[r->skyCur] atIndex:0];
    draw(se);
    [se setRenderPipelineState:r->starDraw];
    [se setVertexBytes:&su length:sizeof su atIndex:1];
    [se setVertexTexture:r->star[r->starCur] atIndex:0];
    [se drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:star_share(kStarsW * kStarsH, su.sKind, false, su.sEchoKeep)];
    [se endEncoding];
    r->skyCur = 1 - r->skyCur;
    memcpy(r->skyBoxPrev, skyS.box, sizeof r->skyBoxPrev); r->skyPrevOk = true;

    // The sharp points, afresh each frame at the screen's own pixel density.
    su.sHead = 1;
    su.sRes = simd_make_float2(r->skySharp.width, r->skySharp.height);
    su.sBB = simd_make_float4(skyL.box[0], skyL.box[1], skyL.box[2], skyL.box[3]);
    su.sTexDen = skyL.den; su.sTexK = skyL.k;
    se = pass(r, cb, r->skySharp, MTLLoadActionClear);
    [se setRenderPipelineState:r->starDraw];
    [se setVertexBytes:&su length:sizeof su atIndex:1];
    [se setVertexTexture:r->star[r->starCur] atIndex:0];
    [se drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:star_share(kStarsW * kStarsH, su.sKind, true, su.sEchoKeep)];
    [se endEncoding];
  }

  // 2. feedback
  // Drawn 32 pixels past the part in use (when there is room), so the mip levels the
  // scene reads near its edges hold the picture carried on rather than stale pixels.
  id<MTLTexture> src = r->fb[r->cur], dst = r->fb[1 - r->cur];
  id<MTLRenderCommandEncoder> e = pass(r, cb, dst, MTLLoadActionDontCare,
                                       std::min((int)dst.width, r->FW + 32), std::min((int)dst.height, r->FH + 32));
  {
    CommonU cu; setCommon(cu, f, r->FW, r->FH);
    FeedbackU u{};
    // Only folds with F(F(p)) = F(p) can fold the echoes; the fractal fold compounds light.
    u.uFoldLoop = P.foldInLoop && fold.mode != 2;
    u.uColorMat = colorMat(f.decay, P.fbSat, f.hue);
    u.uFbRot = f.fbRot;
    u.uFbRotCS = simd_make_float2(cos((double)f.fbRot), sin((double)f.fbRot));
    u.uFbZoom = f.fbZoom; u.uFbSwirl = f.fbSwirl; u.uFbWarp = f.fbWarp; u.uFbWarpT = f.fbWarpT;
    u.uSharpen = P.fbSharpen * std::min(f.n, 2.f);
    u.uDt = f.dt; u.uDream = f.dream;
    u.uDreamC = simd_make_float2(f.dreamX, f.dreamY);
    u.uRipN = std::min(f.ripN, 4);
    for (int i = 0; i < u.uRipN; i++) u.uRip[i] = simd_make_float4(f.rip[i * 4], f.rip[i * 4 + 1], f.rip[i * 4 + 2], f.rip[i * 4 + 3]);
    u.uGen = P.gen; u.uGenK = P.genMix * 1.85f; u.uGenScale = P.genScale; u.uGenT = f.genT; u.uGenRadius = P.genRadius;
    u.uCamK = P.camMix; u.uCamZoom = P.camZoom; u.uCamColorize = P.camColorize;
    id<MTLTexture> cam = r->blackTex;
    os_unfair_lock_lock(&r->lock);
    u.uCamOn = f.camOn && r->camReady;
    u.uCamAspect = r->camAspect;
    if (u.uCamOn) { cam = CVMetalTextureGetTexture(r->camRef); r->camHeld[slot] = (CVMetalTextureRef)CFRetain(r->camRef); }
    os_unfair_lock_unlock(&r->lock);
    [e setRenderPipelineState:r->feedback];
    [e setFragmentBytes:&cu length:sizeof cu atIndex:0];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    setK(e, {{partK(src, r->FW, r->FH), simd_make_float4(1, 1, 0, 0), partK(r->rm, rmW, rmH)}, r->mips ? 16.f : 0.f});
    [e setFragmentTexture:src atIndex:0];
    [e setFragmentTexture:cam atIndex:1];
    [e setFragmentTexture:useRm ? r->rm : cam atIndex:2];
    draw(e);
    [e setViewport:(MTLViewport){0, 0, (double)r->FW, (double)r->FH, 0, 1}];
  }

  // 3. ink, max-blended into the same target
  const int inkCount = std::min(f.inkCount, (int)RENDERER_MAX_SEG);
  if (inkCount > 0 && f.ink) {
    memcpy(r->inkBuf[slot].contents, f.ink, (size_t)inkCount * 32);
    const int copies = std::max(1, f.inkCopies);
    InkU u{simd_make_float2(r->FW, r->FH), copies, fold.mirror ? 1 : 0, fold.sym, fold.spin, P.inkGlow,
           simd_make_float2(fold.cx, fold.cy)};
    [e setRenderPipelineState:r->ink];
    [e setVertexBytes:&u length:sizeof u atIndex:1];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    [e setVertexBuffer:r->inkBuf[slot] offset:0 atIndex:2];
    [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:(NSUInteger)inkCount * copies];
  }
  // 3a. the hands' look, fed a little into the echoes at every mirror image. Its batches
  // go into this slot's buffers once, for this draw and the one on top (4b).
  const LookData* looks = f.looks;
  if (looks) {
    if (looks->nSeg > 0) memcpy(r->lookSeg[slot].contents, looks->seg, (size_t)std::min(looks->nSeg, (int)LOOK_MAX_SEG) * 48);
    if (looks->nVert > 0) memcpy(r->lookVert[slot].contents, looks->vert, (size_t)std::min(looks->nVert, (int)LOOK_MAX_VERT) * 24);
    if (looks->feed > 0 && f.inkCopies > 1 && f.sys.handLookFeed)
      drawLooks(r, e, dst, *looks, fold, f.inkCopies, r->FW, r->FH, looks->feed, false, slot);
  }
  [e endEncoding];
  r->cur = 1 - r->cur;

  // 3b. mipmaps on the feedback texture, only when a fold will actually minify it
  // Chambers that barely shrink the picture skip them (the scene then reads level 0 only).
  const bool seamless = fold.mode == 0 || (fold.mode == 1 && fold.mirror);
  r->mips = !noMips(fold.mode) && (fold.mode >= 2 || fold.vz * 0.65f * r->FS > 1);
  if (r->mips) mipmaps(cb, r->fb[r->cur]);

  // 4. scene (supersampled display fold + filigree + palette remap + optical veil)
  {
    os_unfair_lock_lock(&r->lock);
    const bool veilOn = r->veilOn; const float va = r->veilAmt;
    os_unfair_lock_unlock(&r->lock);
    CommonU cu; setCommon(cu, f, r->W, r->H);
    SceneU u{};
    u.uTaps = seamless ? 1 : fold.mode == 1 ? std::min(2, r->maxTaps) : r->maxTaps;
    // Native: at full detail, pearls and Julia take eight taps. Their fold lines are finer
    // than a pixel, and four taps leave them sparkling (36-tap reference: pixels off by
    // more than 24/255 fall from 7.2% to 5.5% on pearls, 0.50% to 0.05% on Julia); every
    // other chamber already matches the reference at four.
    if (u.uTaps == 4 && (fold.mode == MODE_pearls || fold.mode == MODE_julia)) u.uTaps = 8;
    u.uSheen = P.sheen;
    u.uSheenL = simd_make_float2(0.6 * cos((double)f.sheenT), 0.6 * sin((double)f.sheenT));
    u.uFisheye = fold.fish; u.uFbScale = r->FS; u.uPalMap = P.paletteMap; u.uPalShift = f.palPhase;
    u.uVeil = veilOn ? va * va * (3 - 2 * va) : 0;
    // The sky places, folded out of their wedge (scene.metal, skyLight). Their light
    // shifts a little against the hand, so the sky sits behind the paint.
    const bool sky = stars && r->skySharp;
    u.uSky = sky ? 1 : 0; u.uSkyMirror = fold.mirror ? 1 : 0;
    u.uSkyView = simd_make_float4(f.starView[1], f.starView[2], 1.f / std::max(f.starView[0], 1e-3f), sym);
    u.uSkyAt = simd_make_float4(f.starView[3], f.starView[4], -0.035f * f.starLook[0], -0.035f * f.starLook[1]);
    u.uSkyBox = simd_make_float4(skyL.box[0], skyL.box[1], skyL.box[2], skyL.box[3]);
    u.uSkyLensN = sky ? std::min(f.starLensN, 8) : 0;
    for (int i = 0; i < u.uSkyLensN; i++) u.uSkyLens[i] = simd_make_float4(f.starLens[i * 3], f.starLens[i * 3 + 1], f.starLens[i * 3 + 2], 0);
    e = pass(r, cb, r->sceneT, MTLLoadActionDontCare, r->W, r->H);
    [e setRenderPipelineState:r->scene];
    [e setFragmentBytes:&cu length:sizeof cu atIndex:0];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    setK(e, {{partK(r->fb[r->cur], r->FW, r->FH), partK(r->veil, r->W, r->H)}, r->mips ? 16.f : 0.f});
    [e setFragmentTexture:r->fb[r->cur] atIndex:0];
    [e setFragmentTexture:veilOn ? r->veil : r->fb[r->cur] atIndex:1];
    [e setFragmentTexture:sky ? r->skySharp : r->blackTex atIndex:2];
    [e setFragmentTexture:sky ? r->skySoft[r->skyCur] : r->blackTex atIndex:3];
    draw(e);
    // 4b. the hands' look on top of the picture
    if (looks) drawLooks(r, e, r->sceneT, *looks, fold, 1, r->W, r->H, LOOK_ON_TOP, true, slot);
    [e endEncoding];
  }

  // 5. bloom downsample chain; its alpha channel also averages scene brightness
  const int nb = (int)r->bloom.size();
  const bool doBloom = P.bloom > 0 && nb > 0;
  const bool guard = f.sys.flashGuard && nb > 0;
  if (doBloom || guard) {
    id<MTLTexture> s = r->sceneT;
    int sw = r->W, sh = r->H;
    for (int i = 0; i < nb; i++) {
      id<MTLTexture> d = r->bloom[i];
      const int dw = r->bloomW[i], dh = r->bloomH[i];
      DownU u{simd_make_float2(1.f / sw, 1.f / sh), simd_make_float2(dw, dh), P.bloomThreshold, i == 0};
      e = pass(r, cb, d, MTLLoadActionDontCare, dw, dh);
      [e setRenderPipelineState:r->down];
      [e setFragmentBytes:&u length:sizeof u atIndex:1];
      setK(e, {{partK(s, sw, sh)}});
      [e setFragmentTexture:s atIndex:0];
      draw(e);
      [e endEncoding];
      s = d; sw = dw; sh = dh;
    }
  }

  // 5b. flash guard: mean brightness → 1×1, eased adaptation
  if (guard) {
    e = pass(r, cb, r->lumCur, MTLLoadActionDontCare);
    [e setRenderPipelineState:r->lum];
    setK(e, {{partK(r->bloom[nb - 1], r->bloomW[nb - 1], r->bloomH[nb - 1])}});
    [e setFragmentTexture:r->bloom[nb - 1] atIndex:0];
    draw(e);
    [e endEncoding];
    AdaptU u{r->lumReset ? 1.f : 1.f - expf(-f.dt / 0.4f)};
    e = pass(r, cb, r->lumAd[1 - r->lumIdx], MTLLoadActionDontCare);
    [e setRenderPipelineState:r->adapt];
    [e setFragmentBytes:&u length:sizeof u atIndex:1];
    [e setFragmentTexture:r->lumAd[r->lumIdx] atIndex:0];
    [e setFragmentTexture:r->lumCur atIndex:1];
    draw(e);
    [e endEncoding];
    r->lumIdx = 1 - r->lumIdx;
    r->lumReset = false;
  } else {
    r->lumReset = true;
  }

  // 5c. bloom upsample
  if (doBloom) {
    for (int i = nb - 1; i > 0; i--) {
      id<MTLTexture> sm = r->bloom[i], d = r->bloom[i - 1];
      const int smw = r->bloomW[i], smh = r->bloomH[i], dw = r->bloomW[i - 1], dh = r->bloomH[i - 1];
      UpU u{simd_make_float2(0.5f / smw, 0.5f / smh), simd_make_float2(dw, dh)};
      e = pass(r, cb, d, MTLLoadActionLoad, dw, dh);
      [e setRenderPipelineState:r->up];
      [e setFragmentBytes:&u length:sizeof u atIndex:1];
      setK(e, {{partK(sm, smw, smh)}});
      [e setFragmentTexture:sm atIndex:0];
      draw(e);
      [e endEncoding];
    }
  }

  // 6. final
  FinalU fu{};
  fu.uGuard = guard; fu.uCap = f.sys.brightCap;
  fu.uBloom = doBloom ? P.bloom * 0.22f : 0;
  fu.uChroma = P.chroma; fu.uGrain = P.grain; fu.uVignette = P.vignette; fu.uExposure = P.exposure;
  fu.uFrame = r->frameNo;
  fu.uKs = r->Ks;
  fu.uBloomMeanK = 1 + nb * fu.uBloom;
  if (out) {
    const float peak = linToSrgb(std::min(std::max(headroom, 1.f), 4.f));
    fu.uPeak = peak > 1.0005f ? peak : 1;
    // Headroom drifts with screen brightness: step it down to 0.05s so the bound (about 1 ms) is refit rarely.
    fu.uPeak = std::max(1.f, floorf(fu.uPeak * 20) / 20);
    if (fu.uPeak != r->peakFor) { r->peakFor = fu.uPeak; r->G = fu.uPeak > 1 ? hdrBound(fu.uPeak) : 0; }
    fu.uG = r->G;
    fu.uLinearOut = 1;
    fu.uOut = simd_make_float2(out.width, out.height);
    r->outW = (int)out.width; r->outH = (int)out.height;
    if (r->capture && (!r->capWants || r->capWants())) {
      r->capTex = ensure(r, r->capTex, (int)out.width, (int)out.height, kHDR, MTLStorageModePrivate);
      finalPass(r, cb, r->capTex, fu, doBloom, guard);
      Renderer::FormatPSO* p = formatPSO(r, out.pixelFormat);
      if (p) {
        e = pass(r, cb, out, MTLLoadActionDontCare);
        [e setRenderPipelineState:p->blitPSO];
        [e setFragmentTexture:r->capTex atIndex:0];
        draw(e);
        [e endEncoding];
      }
      if (r->capHandler) r->capHandler(cb, r->capTex);
    } else {
      finalPass(r, cb, out, fu, doBloom, guard);
    }
  }
  if (snapW > 0) {
    // The web's still: its final pass on an 8-bit sRGB canvas.
    r->snapTex = ensure(r, r->snapTex, snapW, snapH, MTLPixelFormatRGBA8Unorm, MTLStorageModeShared);
    fu.uPeak = 1; fu.uG = 0; fu.uLinearOut = 0;
    fu.uOut = simd_make_float2(snapW, snapH);
    finalPass(r, cb, r->snapTex, fu, doBloom, guard);
  }
}

static void submit(Renderer* r, const Frame& f, id<MTLTexture> out, id<MTLDrawable> drawable, float headroom,
                   int snapW, int snapH, void (^snapDone)(NSData*)) {
  @autoreleasepool {
    dispatch_semaphore_wait(r->inflight, DISPATCH_TIME_FOREVER);
    id<MTLCommandBuffer> cb = [r->queue commandBuffer];
    runOps(r, cb);
    if (!snapDone) snapW = 0;
#ifdef FX_PASS_TIMING
    r->tnames[r->slot].clear();
    gTimeR = r;
    if (r->tsb[r->slot] == nil) { MTLRenderPassDescriptor* d = [MTLRenderPassDescriptor new]; timeAttach(r, d, nil); r->tnames[r->slot].clear(); }
    encodeFrame(r, cb, f, out, headroom, snapW, snapH);
    r->rp.colorAttachments[0].texture = nil;
    r->rp.sampleBufferAttachments[0].sampleBuffer = nil;
    gTimeR = nullptr;
    {
      const int ts = r->slot; id<MTLCounterSampleBuffer> sb = r->tsb[ts]; std::vector<std::string> names = r->tnames[ts];
      [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        NSData* d = [sb resolveCounterRange:NSMakeRange(0, 512)];
        const MTLCounterResultTimestamp* t = (const MTLCounterResultTimestamp*)d.bytes;
        static int dumped = 0;
        if (getenv("FX_TIMELINE") && ++dumped == 200) {
          const uint64_t z = (uint64_t)(c.GPUStartTime * 1e9);
          printf("    cb %.3f..%.3f ms\n", 0.0, (c.GPUEndTime - c.GPUStartTime) * 1e3);
          auto f = [&](uint64_t v) { return v == MTLCounterErrorValue || v == 0 ? -1.0 : ((double)v - (double)z) * 1e-6; };
          for (size_t i = 0; i < names.size(); i++)
            printf("    %-13s vtx %.3f..%.3f  frag %.3f..%.3f\n", names[i].c_str(), f(t[256 + i * 2].timestamp), f(t[256 + i * 2 + 1].timestamp), f(t[i * 2].timestamp), f(t[i * 2 + 1].timestamp));
        }
        std::map<std::string, double> frame;
        // Each pass is charged the time from where the previous one ended (or it began,
        // if later) to its own end: the frame partitioned, overlaps counted once.
        uint64_t prevEnd = 0, first = 0, last = 0;
        for (size_t i = 0; i < names.size(); i++) {
          uint64_t a = t[i * 2].timestamp, b = t[i * 2 + 1].timestamp;
          if (a == MTLCounterErrorValue || b == MTLCounterErrorValue || b < a) continue;
          if (!first) first = a;
          const uint64_t from = std::max(a, prevEnd);
          if (b > from) frame[names[i]] += (b - from) * 1e-6;
          prevEnd = std::max(prevEnd, b); last = std::max(last, b);
        }
        frame["(span)"] = (last - first) * 1e-6;
        frame["(cb)"] = (c.GPUEndTime - c.GPUStartTime) * 1e3;
        std::lock_guard<std::mutex> l(gPassM);
        for (auto& [k, v] : frame) { gPassMs[k].first += v; gPassMs[k].second++; }
      }];
    }
#else
    encodeFrame(r, cb, f, out, headroom, snapW, snapH);
#endif
    r->slot = (r->slot + 1) % kInFlight;
    if (drawable) [cb presentDrawable:drawable];
    [cb addCompletedHandler:r->completed];
    if (snapW > 0) {
      id<MTLTexture> t = r->snapTex;
      [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
        std::vector<uint8_t> px((size_t)snapW * snapH * 4);
        [t getBytes:px.data() bytesPerRow:(size_t)snapW * 4 fromRegion:MTLRegionMake2D(0, 0, snapW, snapH) mipmapLevel:0];
        snapDone(pngFromRGBA8(px.data(), snapW, snapH));
      }];
    }
    [cb commit];
  }
}

void renderer_render(Renderer* r, const Frame& f, id<MTLTexture> target, id<MTLDrawable> drawable, float headroom) {
  if (!r->sceneT) return;
  os_unfair_lock_lock(&r->lock);
  void (^snap)(NSData*) = r->snapDone; r->snapDone = nil;
  os_unfair_lock_unlock(&r->lock);
  submit(r, f, target, drawable, headroom, target ? (int)target.width : r->outW, target ? (int)target.height : r->outH, snap);
}

// ---------------------------------------------------------------- capture, stills

void renderer_set_capture(Renderer* r, bool on) { r->capture = on; if (!on) r->capTex = nil; }
id<MTLTexture> renderer_capture_texture(Renderer* r) { return r->capture ? r->capTex : nil; }
void renderer_set_frame_hook(Renderer* r, void (^h)(id<MTLCommandBuffer>, id<MTLTexture>)) { r->capHandler = h; }
void renderer_set_capture_filter(Renderer* r, bool (^f)(void)) { r->capWants = f; }

void renderer_snapshot(Renderer* r, void (^done)(NSData*)) {
  os_unfair_lock_lock(&r->lock);
  r->snapDone = done;
  os_unfair_lock_unlock(&r->lock);
}

// A renderer sharing r's device, queue and pipelines, with no targets yet.
static Renderer* forkRenderer(Renderer* r) {
  Renderer* s = new Renderer();
  s->dev = r->dev; s->queue = r->queue; s->lib = r->lib;
  s->feedback = r->feedback; s->raymarch = r->raymarch; s->scene = r->scene; s->down = r->down; s->up = r->up;
  s->lum = r->lum; s->adapt = r->adapt; s->copy = r->copy; s->ink = r->ink;
  s->starStep = r->starStep; s->starDraw = r->starDraw; s->starTrail = r->starTrail;
  s->look = r->look; s->stripAdd = r->stripAdd; s->stripOver = r->stripOver; s->aura = r->aura;
  for (int i = 0; i < r->nFormats; i++) s->formats[i] = r->formats[i];
  s->nFormats = r->nFormats;
  s->blackTex = r->blackTex;
  s->maxTaps = r->maxTaps; s->rmSteps = r->rmSteps; s->Ks = r->Ks;
  os_unfair_lock_lock(&r->lock);   // the camera picture as it is now, held still
  if (r->camRef) { s->camRef = (CVMetalTextureRef)CFRetain(r->camRef); s->camReady = r->camReady; s->camAspect = r->camAspect; }
  os_unfair_lock_unlock(&r->lock);
  setupFrames(s);
  return s;
}

static void endExport(Renderer* r) {
  renderer_destroy(r->exp->sub);   // its last frame has completed: nothing waits
  delete r->exp; r->exp = nullptr;
}

bool renderer_export_begin(Renderer* r, int longEdge, int frames, void (^done)(NSData*, int, int)) {
  if (r->exp) {
    if (r->exp->left > 0) return false;
    renderer_finish(r);   // the last one's final frame: finished on the shared queue
    while (!r->exp->finished) usleep(500);
    endExport(r);
  }
  if (frames < 1 || !r->sceneT) return false;
  const int max = std::min(renderer_max_size(r), 16384);
  const double aspect = r->outW > 0 ? (double)r->outW / r->outH : (double)r->W / std::max(1, r->H);
  int w = aspect >= 1 ? longEdge : (int)lround(longEdge * aspect);
  int h = aspect >= 1 ? (int)lround(longEdge / aspect) : longEdge;
  const double k = std::min(1.0, (double)max / std::max(w, h));
  w = (int)lround(w * k); h = (int)lround(h * k);
  Renderer* s = forkRenderer(r);
  if (!renderer_resize(s, w, h, r->FS)) { renderer_destroy(s); done(nil, w, h); return true; }
  // Carry on from the live picture: its echo, light adaptation and sky points.
  id<MTLCommandBuffer> cb = [r->queue commandBuffer];
  copyInto(r, cb, r->fb[r->cur], r->FW, r->FH, s->fb[s->cur], s->FW, s->FH);
  id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
  [b copyFromTexture:r->lumAd[r->lumIdx] toTexture:s->lumAd[0]];
  if (r->star[0]) {
    for (auto& t : s->star) t = makeTex(s, kStarsW, kStarsH, MTLPixelFormatRGBA32Float);
    [b copyFromTexture:r->star[r->starCur] toTexture:s->star[0]];
  }
  [b endEncoding];
  [cb commit];
  s->lumIdx = 0; s->lumReset = r->lumReset; s->starCur = 0; s->frameNo = r->frameNo;
  r->exp = new Renderer::Export();
  r->exp->sub = s; r->exp->left = frames; r->exp->w = w; r->exp->h = h; r->exp->done = done;
  return true;
}

bool renderer_export_active(Renderer* r) {
  if (r->exp && r->exp->finished) endExport(r);
  return r->exp && r->exp->left > 0;
}

void renderer_export_step(Renderer* r, const Frame& f) {
  if (!r->exp || r->exp->left <= 0) return;
  Renderer::Export* x = r->exp;
  const bool last = --x->left == 0;
  void (^snap)(NSData*) = nil;
  if (last) {
    void (^done)(NSData*, int, int) = x->done; const int w = x->w, h = x->h;
    snap = ^(NSData* png) { done(png, w, h); x->finished = true; };
  }
  submit(x->sub, f, nil, nil, 1, x->w, x->h, snap);
}

void renderer_export_hires(Renderer* r, int longEdge, int frames, const Frame* (^step)(float), void (^done)(NSData*, int, int)) {
  if (!renderer_export_begin(r, longEdge, frames, done)) { done(nil, 0, 0); return; }
  while (renderer_export_active(r)) renderer_export_step(r, *step(1 / 60.f));
  renderer_finish(r);
  if (r->exp) { while (!r->exp->finished) usleep(500); endExport(r); }
}

// ---------------------------------------------------------------- camera, timing, sizes

void renderer_upload_camera(Renderer* r, CVPixelBufferRef pb) {
  if (!pb || !r->camCache) return;
  const size_t w = CVPixelBufferGetWidth(pb), h = CVPixelBufferGetHeight(pb);
  CVMetalTextureRef ref = nullptr;
  if (CVMetalTextureCacheCreateTextureFromImage(nullptr, r->camCache, pb, nullptr, MTLPixelFormatBGRA8Unorm, w, h, 0, &ref) != kCVReturnSuccess || !ref) return;
  os_unfair_lock_lock(&r->lock);
  CVMetalTextureRef old = r->camRef;
  r->camRef = ref;
  r->camAspect = (float)w / std::max<size_t>(1, h);
  r->camReady = true;
  os_unfair_lock_unlock(&r->lock);
  if (old) CFRelease(old);
  CVMetalTextureCacheFlush(r->camCache, 0);
}

void renderer_finish(Renderer* r) {
  id<MTLCommandBuffer> cb = [r->queue commandBuffer];
  [cb commit];
  [cb waitUntilCompleted];
}

void renderer_set_gpu_time_handler(Renderer* r, void (^h)(double)) { r->gpuHandler = h; }
void renderer_size(Renderer* r, int* w, int* h) { *w = r->W; *h = r->H; }
int renderer_max_size(Renderer*) { return 16384; }   // MTLGPUFamilyApple* 2D texture limit
