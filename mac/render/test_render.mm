// Offscreen test for the Metal renderer. Builds every pipeline, renders each chamber
// with scripted ink, writes PNGs and a side-by-side sheet against thumbs/, checks
// orientation, HDR vs SDR, the flash guard under HDR, the hands' looks, and measures GPU time.
//
//   clang++ -std=c++20 -O2 -c mac/render/test_looks_core.cpp -o mac/build/test_looks_core.o
//   clang++ -std=c++20 -O2 -c mac/core/looks.cpp -o mac/build/test_looks.o
//   clang++ -std=c++17 -O2 -fobjc-arc mac/render/test_render.mm mac/render/renderer.mm \
//     mac/build/test_looks_core.o mac/build/test_looks.o \
//     -framework Metal -framework QuartzCore -framework CoreVideo -framework ImageIO \
//     -framework CoreGraphics -framework Foundation -o mac/build/test_render
//   mac/build/test_render            (run from the repo root)
//
// The tiny engine (mini_engine.h) stands in for core/ so the renderer can be tested alone.
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>
#include <cmath>
#include <mach/mach_time.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "renderer.h"
#ifdef FX_PASS_TIMING
void renderer_pass_times_print();
void renderer_pass_times_reset();
#endif

#include "mini_engine.h"
using Engine = MiniEngine;

// ---------------------------------------------------------------- helpers

static id<MTLTexture> makeTarget(id<MTLDevice> dev, int w, int h) {
  MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
  d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  d.storageMode = MTLStorageModePrivate;
  return [dev newTextureWithDescriptor:d];
}

// Reads an rgba16Float texture back as floats.
static std::vector<float> readback(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLTexture> t) {
  const int w = (int)t.width, h = (int)t.height;
  id<MTLBuffer> buf = [dev newBufferWithLength:(size_t)w * h * 8 options:MTLResourceStorageModeShared];
  id<MTLCommandBuffer> cb = [q commandBuffer];
  id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
  [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
            toBuffer:buf destinationOffset:0 destinationBytesPerRow:(size_t)w * 8 destinationBytesPerImage:(size_t)w * h * 8];
  [b endEncoding]; [cb commit]; [cb waitUntilCompleted];
  std::vector<float> out((size_t)w * h * 4);
  const _Float16* src = (const _Float16*)buf.contents;
  for (size_t i = 0; i < out.size(); i++) out[i] = (float)src[i];
  return out;
}

static NSData* snapshotSync(Renderer* r, Engine& e, id<MTLTexture> target, float headroom) {
  __block NSData* png = nil;
  dispatch_semaphore_t s = dispatch_semaphore_create(0);
  renderer_snapshot(r, ^(NSData* d) { png = d; dispatch_semaphore_signal(s); });
  renderer_render(r, e.step(r, 1 / 60.0), target, nil, headroom);
  dispatch_semaphore_wait(s, DISPATCH_TIME_FOREVER);
  return png;
}

static CGImageRef loadPNG(NSString* path) {
  CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], nullptr);
  if (!src) return nullptr;
  CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
  CFRelease(src);
  return img;
}

static float lum709(const float* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }
static float s2l(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }

// Bounds as computed in renderer.mm (kept in step by recomputing here).
static float tmSdr(float m) { return m > 0.7f ? 0.7f + 0.3f * (1 - expf(-(m - 0.7f) * 3.333f)) : m; }

static std::map<std::string, Params> loadChambers(std::vector<std::string>& order) {
  std::map<std::string, Params> out;
  FILE* p = popen("node mac/render/chambers.mjs index.html", "r");
  if (!p) return out;
  char slug[64], key[64], val[64];
  while (fscanf(p, "%63s %63s %63s", slug, key, val) == 3) {
    if (!out.count(slug)) { out[slug] = Params(); order.push_back(slug); }
    char* end; double v = strtod(val, &end);
    if (!params_set(out[slug], key, *end ? 0 : v, val)) printf("  unknown param %s=%s\n", key, val);
  }
  pclose(p);
  return out;
}

// ---------------------------------------------------------------- hand looks

// test_looks_core.cpp: the real looks (mac/core/looks.cpp) on a scripted open hand.
const LookData* test_look_build(int look, double dt, double t, double aspect, float cx, float cy, float size, int nHands);

static void lookSeg(LookData& L, float ax, float ay, float bx, float by, float r, float g, float b, float w, float alpha, float sharp, float glow) {
  float* s = L.seg + L.nSeg++ * 12;
  const float v[12] = {ax, ay, bx, by, r, g, b, w, alpha, sharp, glow, 0};
  memcpy(s, v, sizeof v);
}
static void lookV(LookData& L, float x, float y, float a, float b, float c, float e) {
  float* v = L.vert + L.nVert++ * 6;
  v[0] = x; v[1] = y; v[2] = a; v[3] = b; v[4] = c; v[5] = e;
}
static void lookStrip(LookData& L, int first, int kind) {
  L.stripFirst[L.nStrip] = first; L.stripCount[L.nStrip] = L.nVert - first; L.stripKind[L.nStrip] = kind; L.nStrip++;
}
static void lookClear(LookData& L) {
  L.nSeg = L.nVert = L.nStrip = L.auraN = 0; L.feed = 0; L.time = 1.5f; L.breath = 0.6f;
  L.box[0] = L.box[1] = 1e9f; L.box[2] = L.box[3] = -1e9f;
}

// Everything at once, in its own part of the screen: a point (upper right), a blue line
// (lower left), a satin ribbon (top left, across a white line it must lay over), an aurora
// curtain (lower right), an aura around a hand (centre left).
static void lookHandmade(LookData& L) {
  lookClear(L);
  lookSeg(L, 0.5f, 0.4f, 0.5f, 0.4f, 3, 1, 0.3f, 0.07f, 1, 4, 0.4f);
  lookSeg(L, -1.2f, -0.75f, -0.6f, -0.75f, 0.3f, 0.6f, 3, 0.03f, 1, 6, 0.3f);
  lookSeg(L, -1.2f, 0.65f, -1.2f, 0.95f, 4, 4, 4, 0.02f, 1, 6, 0);     // under the ribbon
  int first = L.nVert;
  for (int j = 0; j < 40; j++) {   // a band from x = -1.45 to -0.25 at y = 0.8, twisting once
    const float u = j / 39.f, x = -1.45f + 1.2f * u, cs = cosf(u * 3.14159f), w = 0.08f * (0.18f + 0.82f * fabsf(cs));
    const float hw = 0.3f + 0.4f * u + 255;
    lookV(L, x, 0.8f + w, -1, u * 0.5f, cs, hw);
    lookV(L, x, 0.8f - w, 1, u * 0.5f, cs, hw);
  }
  lookStrip(L, first, 0);
  first = L.nVert;
  for (int j = 0; j < 48; j++) {   // a curtain from x = 0.3 to 1.3, hanging from y = -0.2 to -0.85
    const float u = j / 47.f, x = 0.3f + u;
    lookV(L, x, -0.2f, u, 0, 0.5f, 1);
    lookV(L, x + 0.05f, -0.85f, u, 1, 0.5f, 1);
  }
  lookStrip(L, first, 1);
  // An aura: the scripted open hand, wrist at (-0.5, -0.45), size 0.3.
  static const float tmpl[21][2] = {
    {0, 0}, {-0.45f, 0.2f}, {-0.8f, 0.45f}, {-1.05f, 0.7f}, {-1.25f, 0.9f},
    {-0.35f, 0.95f}, {-0.45f, 1.45f}, {-0.5f, 1.75f}, {-0.55f, 2.0f},
    {0, 1}, {0, 1.55f}, {0, 1.9f}, {0, 2.15f},
    {0.32f, 0.95f}, {0.4f, 1.45f}, {0.45f, 1.75f}, {0.5f, 1.98f},
    {0.6f, 0.82f}, {0.75f, 1.2f}, {0.85f, 1.45f}, {0.93f, 1.65f}};
  const float S = 0.3f, m = S * 1.4f;
  for (int j = 0; j < 21; j++) {
    const float x = -0.5f + tmpl[j][0] * S, y = -0.45f + tmpl[j][1] * S;
    L.auraPts[j * 2] = x; L.auraPts[j * 2 + 1] = y;
    L.box[0] = fminf(L.box[0], x - m); L.box[1] = fminf(L.box[1], y - m);
    L.box[2] = fmaxf(L.box[2], x + m); L.box[3] = fmaxf(L.box[3], y + m);
  }
  const float* p = L.auraPts;
  L.auraHand[0] = (p[0] + p[10] + p[18] + p[26] + p[34]) / 5; L.auraHand[1] = (p[1] + p[11] + p[19] + p[27] + p[35]) / 5;
  L.auraHand[2] = S; L.auraHand[3] = 1;
  L.auraN = 1;
}

// Renders f; writes the 8-bit still to path (if given); returns the capture texture's pixels.
static std::vector<float> renderLook(Renderer* r, id<MTLDevice> dev, id<MTLCommandQueue> q, const Frame& f, id<MTLTexture> tgt, NSString* path) {
  __block NSData* png = nil;
  dispatch_semaphore_t s = dispatch_semaphore_create(0);
  if (path) renderer_snapshot(r, ^(NSData* d) { png = d; dispatch_semaphore_signal(s); });
  renderer_render(r, f, tgt, nil, 1);
  if (path) { dispatch_semaphore_wait(s, DISPATCH_TIME_FOREVER); [png writeToFile:path atomically:YES]; }
  renderer_finish(r);
  return readback(dev, q, renderer_capture_texture(r));
}

// Picture space (y up, height -1..1) to pixel (row 0 on top).
static void toPx(float x, float y, int w, int h, float& px, float& py) { px = (x * h / w * 0.5f + 0.5f) * w; py = (0.5f - y * 0.5f) * h; }
static float peakNear(const std::vector<float>& px, int w, int h, float cx, float cy, int rad) {
  float m = 0;
  for (int y = std::max(0, (int)cy - rad); y <= std::min(h - 1, (int)cy + rad); y++)
    for (int x = std::max(0, (int)cx - rad); x <= std::min(w - 1, (int)cx + rad); x++) m = fmaxf(m, lum709(&px[(y * w + x) * 4]));
  return m;
}
static float meanBox(const std::vector<float>& px, int w, int h, float x0, float y0, float x1, float y1) {   // picture space box
  float ax, ay, bx, by; toPx(x0, y1, w, h, ax, ay); toPx(x1, y0, w, h, bx, by);
  double s = 0; int n = 0;
  for (int y = std::max(0, (int)ay); y < std::min(h, (int)by); y++) for (int x = std::max(0, (int)ax); x < std::min(w, (int)bx); x++) { s += lum709(&px[(y * w + x) * 4]); n++; }
  return n ? (float)(s / n) : 0;
}

static bool looksTest(id<MTLDevice> dev, id<MTLCommandQueue> q, NSString* shaders, NSString* outDir, std::map<std::string, Params>& chambers) {
  NSError* err = nil;
  bool ok = true;
  static LookData L;
  const int w = 480, h = 300;
  auto calm = [](Engine& e) {
    Params P; P.mode = MODE_none; P.fbDecay = 1; P.fbZoom = 1; P.fbHueShift = 0; P.bloom = 0; P.chroma = 0;
    P.vignette = 0; P.grain = 0; P.breath = 0; P.sheen = 0; P.fbSharpen = 0; e.init(P); e.script = false; e.S.flashGuard = false;
  };
  auto path = [&](const char* n) { return [outDir stringByAppendingPathComponent:@(n)]; };

  // a. One point at (0.5, 0.4) on black, on top only: right of centre, above it. Again with
  // targets allocated larger than the render (a lower detail level drawing into their top-left).
  for (int capped = 0; capped < 2; capped++) {
    Renderer* r = renderer_create(dev, shaders, &err);
    renderer_resize(r, w, h, 1, capped ? w * 2 : 0, capped ? h * 2 : 0, capped ? w * 2 : 0, capped ? h * 2 : 0); renderer_set_capture(r, true);
    Engine e; calm(e);
    id<MTLTexture> tgt = makeTarget(dev, w, h);
    lookClear(L); lookSeg(L, 0.5f, 0.4f, 0.5f, 0.4f, 3, 1, 0.3f, 0.07f, 1, 4, 0.4f);
    Frame f = e.step(r, 1 / 60.0); f.inkCount = 0; f.looks = &L;
    auto px = renderLook(r, dev, q, f, tgt, path("looks_point.png"));
    double sx = 0, sy = 0, sw = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { float v = lum709(&px[(y * w + x) * 4]); sx += v * x; sy += v * y; sw += v; }
    float ex, ey; toPx(0.5f, 0.4f, w, h, ex, ey);
    printf("looks: a point at picture (0.5, 0.4)%s: light centred at (%.1f, %.1f) of %dx%d, expected (%.1f, %.1f) (right of and above the centre)\n",
           capped ? " (targets at twice the size)" : "", sx / sw, sy / sw, w, h, ex, ey);
    if (sw <= 0 || fabs(sx / sw - ex) > 3 || fabs(sy / sw - ey) > 3) { printf("  FAIL: look misplaced\n"); ok = false; }
    renderer_destroy(r);
  }

  // b. Fed into the echoes over 12 mirror images (six-fold, mirrored): a point at 20 degrees
  // must appear at +-20 degrees + k 60, and nowhere between.
  {
    Renderer* r = renderer_create(dev, shaders, &err);
    renderer_resize(r, w, h, 1, w * 2, h * 2, w * 2, h * 2); renderer_set_capture(r, true);
    Engine e; calm(e);
    id<MTLTexture> tgt = makeTarget(dev, w, h);
    const float R = 0.55f, a0 = 20 * M_PI / 180;
    lookClear(L); lookSeg(L, R * cosf(a0), R * sinf(a0), R * cosf(a0), R * sinf(a0), 3, 1, 0.3f, 0.045f, 1, 4, 0.4f);
    L.feed = 0.5f;
    Frame f = e.step(r, 1 / 60.0); f.inkCount = 0; f.looks = &L;
    f.fold.mode = MODE_none; f.fold.sym = 6; f.fold.mirror = true; f.fold.spin = 0; f.fold.cx = f.fold.cy = 0; f.fold.fish = 0;
    f.inkCopies = 12; f.sys.handLookFeed = true;
    auto px = renderLook(r, dev, q, f, tgt, path("looks_mirrors.png"));
    float on = 1e9, off = 0, own = 0;
    for (int k = 0; k < 6; k++) for (int sgn = -1; sgn <= 1; sgn += 2) {
      const float a = (60 * k + sgn * 20) * M_PI / 180; float x, y; toPx(R * cosf(a), R * sinf(a), w, h, x, y);
      const float v = peakNear(px, w, h, x, y, 3);
      if (k == 0 && sgn == 1) own = v; else on = fminf(on, v);
    }
    for (int k = 0; k < 6; k++) { const float a = 60 * k * M_PI / 180; float x, y; toPx(R * cosf(a), R * sinf(a), w, h, x, y); off = fmaxf(off, peakNear(px, w, h, x, y, 3)); }
    printf("looks: fed into 12 mirror images: the point's own place %.3f, dimmest mirror image %.3f, brightest gap between them %.4f\n", own, on, off);
    if (!(on > 0.05f && on > 10 * off && own > on)) { printf("  FAIL: mirror images of the look missing or misplaced\n"); ok = false; }
    // The same with the feed switched off in Settings: only the point itself.
    f.sys.handLookFeed = false; renderer_clear(r);
    px = renderLook(r, dev, q, f, tgt, nil);
    float x, y; toPx(R * cosf(-a0), R * sinf(-a0), w, h, x, y);
    const float mir = peakNear(px, w, h, x, y, 3);
    printf("looks: feed off in Settings: mirror image %.4f (expect ~0)\n", mir);
    if (mir > 0.01f) { printf("  FAIL: handLookFeed ignored\n"); ok = false; }
    renderer_destroy(r);
  }

  // c. Everything at once on black, on top: each part where it was put, the aura only in its box.
  {
    Renderer* r = renderer_create(dev, shaders, &err);
    renderer_resize(r, w, h, 1, w * 2, h * 2, w * 2, h * 2); renderer_set_capture(r, true);   // a part of larger targets, as the ladder does
    Engine e; calm(e);
    id<MTLTexture> tgt = makeTarget(dev, w, h);
    lookHandmade(L);
    Frame f = e.step(r, 1 / 60.0); f.inkCount = 0; f.looks = &L;
    auto px = renderLook(r, dev, q, f, tgt, path("looks_all_black.png"));
    const float rib = meanBox(px, w, h, -1.3f, 0.74f, -0.6f, 0.86f), cur = meanBox(px, w, h, 0.5f, -0.75f, 1.1f, -0.3f);
    const float aura = meanBox(px, w, h, -0.6f, -0.5f, -0.4f, -0.2f), empty = meanBox(px, w, h, 0.0f, 0.05f, 0.3f, 0.25f);
    const float outside = meanBox(px, w, h, -1.55f, -0.4f, -1.1f, -0.1f);   // left of the aura's box, clear of everything
    printf("looks: mean light in the ribbon %.3f, curtain %.3f, aura %.3f; empty places %.4f, %.4f\n", rib, cur, aura, empty, outside);
    if (!(rib > 0.05f * LOOK_ON_TOP && cur > 0.02f * LOOK_ON_TOP && aura > 0.02f * LOOK_ON_TOP && empty < 0.002f && outside < 0.002f)) { printf("  FAIL: a strip or the aura misplaced\n"); ok = false; }
    // The satin is laid over the white line under it (premultiplied over), not added to it.
    float lx, ly; toPx(-1.2f, 0.8f, w, h, lx, ly);
    const float* c = &px[((int)ly * w + (int)lx) * 4];
    printf("looks: the white line under the ribbon reads (%.2f %.2f %.2f)\n", c[0], c[1], c[2]);
    renderer_destroy(r);
  }

  // d. Over a chamber: the hand-made look on top (feed off), then with the feed into its
  // mirror images; and every real look (looks.cpp) on two scripted hands, fed as the app does.
  {
    const int W = 1120, H = 704;
    for (int pass = 0; pass < 7; pass++) {
      Renderer* r = renderer_create(dev, shaders, &err);
      renderer_resize(r, W, H, 1);
      Engine e; e.init(chambers["mandala"]);
      id<MTLTexture> tgt = makeTarget(dev, W, H);
      renderer_set_capture(r, true);
      Frame f;
      for (int i = 0; i < 180; i++) {
        f = e.step(r, 1 / 60.0);
        if (i >= 60) {
          if (pass < 2) { lookHandmade(L); L.time = i / 60.f; L.feed = pass == 1 ? 0.02f : 0; f.looks = &L; }
          else f.looks = test_look_build(pass - 1, 1 / 60.0, i / 60.0, (double)W / H, 0.32f, 0.72f, 0.42f, 2);
        }
        if (i < 179) renderer_render(r, f, tgt, nil, 1);
      }
      char name[64];
      if (pass < 2) snprintf(name, sizeof name, "looks_mandala_%s.png", pass ? "feed" : "top");
      else snprintf(name, sizeof name, "looks_real_%d.png", pass - 1);
      renderLook(r, dev, q, f, tgt, path(name));
      printf("looks: %s (copies %d, feed %.4f, %d segments, %d strip vertices, %d auras)\n", name, f.inkCopies,
             f.looks ? f.looks->feed : 0, f.looks ? f.looks->nSeg : 0, f.looks ? f.looks->nVert : 0, f.looks ? f.looks->auraN : 0);
      renderer_destroy(r);
    }
  }
  return ok;
}

// GPU time of the looks at 3024 x 1964 over a mirrored chamber: each real look on two
// large hands against the same frames without one.
static bool looksTiming(id<MTLDevice> dev, NSString* shaders, std::map<std::string, Params>& chambers) {
  if (getenv("FX_TIME_ONLY") && !strstr(getenv("FX_TIME_ONLY"), "looks")) return true;
  NSError* err = nil;
  const int w = 3024, h = 1964;
  id<MTLTexture> tgt = makeTarget(dev, w, h);
  const char* names[6] = {"none", "constellation", "neon", "aurora", "ribbons", "aura"};
  const float size = getenv("FX_LOOK_SIZE") ? atof(getenv("FX_LOOK_SIZE")) : 0.5f;   // a hand's size, wrist to middle knuckle, in screen heights / 2
  printf("GPU time per frame with the hands' looks at %dx%d over mandala (two hands of size %.2f, feed on):\n", w, h, size);
  double base = 0;
  for (int look = 0; look <= 5; look++) {
    Renderer* r = renderer_create(dev, shaders, &err);
    renderer_resize(r, w, h, 0.75f, w, h, (int)lround(w * 0.75), (int)lround(h * 0.75));
    renderer_set_detail(r, 4, 76);
    __block double sum = 0; __block int cnt = 0; __block bool measuring = false;
    renderer_set_gpu_time_handler(r, ^(double s) { if (measuring) { sum += s; cnt++; } });
    Engine e; e.init(chambers["mandala"]);
    int copies = 0, nSeg = 0, nVert = 0;
    for (int i = 0; i < 360; i++) {
      if (i == 120) {
        measuring = true;
#ifdef FX_PASS_TIMING
        renderer_finish(r); renderer_pass_times_reset();
#endif
      }
      Frame f = e.step(r, 1 / 60.0);
      if (look) f.looks = test_look_build(look, 1 / 60.0, i / 60.0, (double)w / h, 0.3f, 0.75f, size, 2);
      copies = f.inkCopies; if (f.looks) { nSeg = f.looks->nSeg; nVert = f.looks->nVert; }
      renderer_render(r, f, tgt, nil, 4);
    }
    renderer_finish(r);
    renderer_destroy(r);
    const double ms = sum / cnt * 1e3;
    if (!look) base = ms;
    printf("  %-13s mean %.3f ms (%+.3f ms vs none; copies %d, %d segments, %d strip vertices)\n", names[look], ms, ms - base, copies, nSeg, nVert);
#ifdef FX_PASS_TIMING
    renderer_pass_times_print();
#endif
  }
  return true;
}

int main() {
  @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> q = [dev newCommandQueue];
    NSString* shaders = @"mac/render/shaders";
    NSString* outDir = getenv("FX_OUT") ? @(getenv("FX_OUT")) : @"mac/build/render_test";
    [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES attributes:nil error:nil];
    printf("device: %s\n", dev.name.UTF8String);

    NSError* err = nil;
    double t0 = CACurrentMediaTime();
    Renderer* probe = renderer_create(dev, shaders, &err);
    if (!probe) { printf("SHADER/PIPELINE ERROR:\n%s\n", err.localizedDescription.UTF8String); return 1; }
    printf("compiled all shaders and pipelines in %.2f s\n", CACurrentMediaTime() - t0);
    renderer_destroy(probe);

    std::vector<std::string> order;
    auto chambers = loadChambers(order);
    printf("chambers from index.html: %zu\n", order.size());
    if (order.empty()) return 1;
    int fails = 0;
    // FX_LOOKS=1: only the hands' looks (4c) and their GPU time.
    if (getenv("FX_LOOKS")) {
      if (!looksTest(dev, q, shaders, outDir, chambers)) fails++;
      setenv("FX_TIME_ONLY", "looks", 1);
      looksTiming(dev, shaders, chambers);
      printf(fails ? "FAILURES: %d\n" : "all checks passed\n", fails);
      return fails ? 1 : 0;
    }

    // ---- 1. orientation: a dot drawn once at top-left (p-space y up) must stay there.
    {
      Renderer* r = renderer_create(dev, shaders, &err);
      const int w = 320, h = 200;
      renderer_resize(r, w, h, 1);
      Engine e; Params P; P.mode = MODE_none; P.fbDecay = 1; P.fbZoom = 1; P.fbHueShift = 0; P.bloom = 0; P.chroma = 0;
      P.vignette = 0; P.grain = 0; P.breath = 0; P.sheen = 0; P.fbSharpen = 0; e.init(P); e.script = false;
      e.S.flashGuard = false;
      id<MTLTexture> tgt = makeTarget(dev, w, h);
      renderer_set_capture(r, true);
      float cyFirst = 0, cyLast = 0;
      for (int i = 0; i < 61; i++) {
        Frame f = e.step(r, 1 / 60.0);
        float seg[8] = {-1.0f, 0.5f, -1.0f, 0.5f, 1, 1, 1, 0.05f};
        if (i == 0) { f.ink = seg; f.inkCount = 1; f.inkCopies = 1; } else f.inkCount = 0;
        renderer_render(r, f, tgt, nil, 1);
        if (i == 0 || i == 60) {
          renderer_finish(r);
          auto px = readback(dev, q, renderer_capture_texture(r));
          double sx = 0, sy = 0, sw = 0;
          for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { float v = px[(y * w + x) * 4]; sx += v * x; sy += v * y; sw += v; }
          (i == 0 ? cyFirst : cyLast) = sy / sw;
          printf("orientation frame %d: bright centroid at (%.0f, %.0f) of %dx%d (expect left, upper quarter: ~(60, 50))\n", i + 1, sx / sw, sy / sw, w, h);
        }
      }
      if (cyFirst > h / 2 || cyLast > h / 2) { printf("  FAIL: picture upside down\n"); fails++; }
      renderer_destroy(r);
    }

    // ---- 2. every chamber, 300 frames with ink, PNG + sheet against the web thumbnails.
    // FX_W, FX_H, FX_FS, FX_STAPS: render the stills at another size, echo scale or tap count.
    const int W = getenv("FX_W") ? atoi(getenv("FX_W")) : 1120, H = getenv("FX_H") ? atoi(getenv("FX_H")) : 704;   // 4x the 280x176 thumbnails
    const float stillFs = getenv("FX_FS") ? atof(getenv("FX_FS")) : 1;
    const int stillTaps = getenv("FX_STAPS") ? atoi(getenv("FX_STAPS")) : 4;
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef sheet = CGBitmapContextCreate(nullptr, 560, 176 * order.size(), 8, 0, srgb, kCGImageAlphaNoneSkipLast);
    int row = 0;
    // For working on a place's look: FX_ONLY=galaxy,nebula renders just those and stops;
    // FX_FRAMES sets how long each runs (default 299).
    const char* only = getenv("FX_ONLY");
    const int nFrames = getenv("FX_FRAMES") ? atoi(getenv("FX_FRAMES")) : 299;
    for (const std::string& slug : order) {
      if (only && !strstr(only, slug.c_str())) continue;
      if (getenv("FX_TIME_ONLY")) break;   // timing only: skip the sheet
      Renderer* r = renderer_create(dev, shaders, &err);
      renderer_resize(r, W, H, stillFs);
      renderer_set_detail(r, stillTaps, 76);
      Engine e; e.init(chambers[slug]);
      id<MTLTexture> tgt = makeTarget(dev, W, H);
      for (int i = 0; i < nFrames; i++) renderer_render(r, e.step(r, 1 / 60.0), tgt, nil, 1);
      NSData* png = snapshotSync(r, e, tgt, 1);
      NSString* path = [outDir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.png", slug.c_str()]];
      [png writeToFile:path atomically:YES];
      CGImageRef thumb = loadPNG([NSString stringWithFormat:@"thumbs/%s.png", slug.c_str()]), ours = loadPNG(path);
      const CGFloat y = 176 * (order.size() - 1 - row);
      if (thumb) CGContextDrawImage(sheet, CGRectMake(0, y, 280, 176), thumb);
      if (ours) CGContextDrawImage(sheet, CGRectMake(280, y, 280, 176), ours);
      if (thumb) CGImageRelease(thumb);
      if (ours) CGImageRelease(ours);
      printf("%-13s %s (%lu bytes)\n", slug.c_str(), png ? "ok" : "NO PNG", (unsigned long)png.length);
      if (!png) fails++;
      row++;
      renderer_destroy(r);
    }
    if (!getenv("FX_TIME_ONLY")) {
      CGImageRef img = CGBitmapContextCreateImage(sheet);
      NSURL* url = [NSURL fileURLWithPath:[outDir stringByAppendingPathComponent:@"sheet.png"]];
      CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
      CGImageDestinationAddImage(d, img, nullptr); CGImageDestinationFinalize(d);
      CFRelease(d); CGImageRelease(img); CGContextRelease(sheet);
      printf("sheet: web thumbnail | Metal, one chamber per row -> %s/sheet.png\n", outDir.UTF8String);
    }
    if (only) return fails;
    if (getenv("FX_TIME_ONLY")) goto timing;

    // ---- 3. HDR: identical runs, last frame at headroom 1 and 4.
    {
      const char* slug = "mandala";
      const int w = 560, h = 352;
      Renderer* ra = renderer_create(dev, shaders, &err); Renderer* rb = renderer_create(dev, shaders, &err);
      Engine ea, eb; ea.init(chambers[slug]); eb.init(chambers[slug]);
      renderer_resize(ra, w, h, 1); renderer_resize(rb, w, h, 1);
      id<MTLTexture> ta = makeTarget(dev, w, h), tb = makeTarget(dev, w, h);
      renderer_set_capture(ra, true); renderer_set_capture(rb, true);
      for (int i = 0; i < 239; i++) { renderer_render(ra, ea.step(ra, 1 / 60.0), ta, nil, 1); renderer_render(rb, eb.step(rb, 1 / 60.0), tb, nil, 4); }
      NSData* png = snapshotSync(ra, ea, ta, 1);
      renderer_render(rb, eb.step(rb, 1 / 60.0), tb, nil, 4);
      renderer_finish(ra); renderer_finish(rb);
      auto A = readback(dev, q, renderer_capture_texture(ra)), B = readback(dev, q, renderer_capture_texture(rb));
      const float bins[5] = {0, 0.05f, 0.2f, 0.45f, 1e9f};
      double maxd[4] = {}, cnt[4] = {}; double la = 0, lb = 0; float peakA = 0, peakB = 0;
      for (int i = 0; i < w * h; i++) {
        const float* a = &A[i * 4]; const float* b = &B[i * 4];
        float m = fmaxf(a[0], fmaxf(a[1], a[2]));
        int k = 0; while (m >= bins[k + 1]) k++;
        float d = fmaxf(fabsf(a[0] - b[0]), fmaxf(fabsf(a[1] - b[1]), fabsf(a[2] - b[2])));
        maxd[k] = fmax(maxd[k], d); cnt[k]++;
        la += lum709(a); lb += lum709(b);
        peakA = fmaxf(peakA, m); peakB = fmaxf(peakB, fmaxf(b[0], fmaxf(b[1], b[2])));
      }
      printf("HDR (%s, frame 240): mean linear luminance headroom1 %.4f, headroom4 %.4f; peak %.3f vs %.3f\n", slug, la / (w * h), lb / (w * h), peakA, peakB);
      const char* names[4] = {"shadows  <0.05", "mids  0.05-0.2", "upper 0.2-0.45", "highlights >0.45"};
      for (int k = 0; k < 4; k++) printf("  SDR-linear %s: %6.0f px, max |HDR-SDR| %.5f\n", names[k], cnt[k], maxd[k]);
      if (maxd[0] > 1e-3 || maxd[1] > 1e-3) { printf("  FAIL: shadows/mid-tones moved\n"); fails++; }
      // The 8-bit still is the headroom-1 frame, quantised.
      CGImageSourceRef s = CGImageSourceCreateWithData((__bridge CFDataRef)png, nullptr);
      CGImageRef img = CGImageSourceCreateImageAtIndex(s, 0, nullptr);
      std::vector<uint8_t> px((size_t)w * h * 4);
      CGContextRef c = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, srgb, kCGImageAlphaNoneSkipLast);
      CGContextDrawImage(c, CGRectMake(0, 0, w, h), img);
      double md = 0;
      for (int i = 0; i < w * h; i++) for (int ch = 0; ch < 3; ch++) md = fmax(md, fabs(px[i * 4 + ch] / 255.0 - (A[i * 4 + ch] <= 0.0031308f ? A[i * 4 + ch] * 12.92 : 1.055 * pow(A[i * 4 + ch], 1 / 2.4) - 0.055)));
      printf("  8-bit still vs headroom-1 frame (sRGB-encoded): max diff %.4f (1/255 = 0.0039)\n", md);
      if (md > 2.5 / 255) { printf("  FAIL: still differs from the screen\n"); fails++; }
      CGContextRelease(c); CGImageRelease(img); CFRelease(s);
      renderer_destroy(ra); renderer_destroy(rb);
    }

    // ---- 4. flash guard under HDR: a sudden blast of white ink on a calm frame, in
    // lockstep at headroom 1 and 4. HDR must never show a brighter screen mean than
    // the larger of the SDR frame and the ceiling bound Ks * exposure * cap.
    {
      float Ks = 0; for (int i = 1; i <= 4000; i++) { float m = i * 0.004f; Ks = fmaxf(Ks, s2l(fminf(tmSdr(m), 1)) / m); }
      const int w = 480, h = 300;
      for (int guardOn = 1; guardOn >= 0; guardOn--) {
        Renderer* r[2]; Engine e[2]; id<MTLTexture> tgt[2];
        const float head[2] = {1, 4};
        for (int k = 0; k < 2; k++) {
          r[k] = renderer_create(dev, shaders, &err);
          renderer_resize(r[k], w, h, 1);
          e[k].init(chambers["mandala"]); e[k].S.flashGuard = guardOn;
          tgt[k] = makeTarget(dev, w, h);
          renderer_set_capture(r[k], true);
        }
        const double bound = Ks * e[0].P.exposure * e[0].S.brightCap;
        std::vector<float> blast;
        for (int k = 0; k < 400; k++) { float y = -1 + k * 0.005f; float sg[8] = {-2, y, 2, y, 8, 8, 8, 0.02f}; blast.insert(blast.end(), sg, sg + 8); }
        double before[2] = {}, peak[2] = {}, worst = 0;
        for (int i = 0; i < 160; i++) {
          double mean[2];
          for (int k = 0; k < 2; k++) {
            Frame f = e[k].step(r[k], 1 / 60.0);
            if (i >= 120 && i < 126) { f.ink = blast.data(); f.inkCount = 400; f.inkCopies = 1; }
            renderer_render(r[k], f, tgt[k], nil, head[k]);
            mean[k] = 0;
            if (i < 110) continue;
            renderer_finish(r[k]);
            auto px = readback(dev, q, renderer_capture_texture(r[k]));
            for (int j = 0; j < w * h; j++) mean[k] += lum709(&px[j * 4]);
            mean[k] /= w * h;
            if (i == 119) before[k] = mean[k];
            if (i >= 120) peak[k] = fmax(peak[k], mean[k]);
          }
          if (i >= 110) worst = fmax(worst, mean[1] - fmax(mean[0], bound));
        }
        printf("flash, guard %s: screen mean linear luminance before -> peak after a white blast: SDR %.4f -> %.4f, HDR(4) %.4f -> %.4f; "
               "worst HDR excess over max(SDR, bound %.4f): %+.4f\n", guardOn ? "on " : "off", before[0], peak[0], before[1], peak[1], bound, worst);
        if (guardOn && worst > 1e-3) { printf("  FAIL: HDR brighter than the guard allows\n"); fails++; }
        for (auto x : r) renderer_destroy(x);
      }
    }

    // ---- 4b. hi-res export and camera (top half red must land on the top half).
    {
      Renderer* r = renderer_create(dev, shaders, &err);
      renderer_resize(r, 560, 352, 1);
      __block Engine e; Params P; P.mode = MODE_none; P.camMix = 1; P.camColorize = 0; P.fbHueShift = 0; P.bloom = 0; P.chroma = 0; P.vignette = 0;
      e.init(P); e.script = false; e.S.flashGuard = false;
      CVPixelBufferRef pb = nullptr;
      NSDictionary* a = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES};
      CVPixelBufferCreate(nullptr, 640, 480, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)a, &pb);
      CVPixelBufferLockBaseAddress(pb, 0);
      uint8_t* base = (uint8_t*)CVPixelBufferGetBaseAddress(pb); size_t bpr = CVPixelBufferGetBytesPerRow(pb);
      for (int y = 0; y < 480; y++) for (int x = 0; x < 640; x++) { uint8_t* q2 = base + y * bpr + x * 4; q2[0] = y < 240 ? 0 : 255; q2[1] = 0; q2[2] = y < 240 ? 255 : 0; q2[3] = 255; }
      CVPixelBufferUnlockBaseAddress(pb, 0);
      renderer_upload_camera(r, pb);
      CVPixelBufferRelease(pb);
      id<MTLTexture> tgt = makeTarget(dev, 560, 352);
      renderer_set_capture(r, true);
      for (int i = 0; i < 30; i++) { Frame f = e.step(r, 1 / 60.0); f.camOn = true; renderer_render(r, f, tgt, nil, 1); }
      renderer_finish(r);
      auto px = readback(dev, q, renderer_capture_texture(r));
      const float* top = &px[(40 * 560 + 280) * 4]; const float* bot = &px[(310 * 560 + 280) * 4];
      printf("camera: top rgb (%.2f %.2f %.2f), bottom rgb (%.2f %.2f %.2f) (expect top red, bottom blue)\n", top[0], top[1], top[2], bot[0], bot[1], bot[2]);
      if (!(top[0] > top[2] && bot[2] > bot[0])) { printf("  FAIL: camera flipped\n"); fails++; }
      __block NSData* png = nil; __block int ew = 0, eh = 0;
      dispatch_semaphore_t sem = dispatch_semaphore_create(0);
      renderer_export_hires(r, 3840, 60, ^const Frame*(float dt) { static Frame f; f = e.step(r, dt); f.camOn = true; return &f; },
                            ^(NSData* d, int w, int h) { png = d; ew = w; eh = h; dispatch_semaphore_signal(sem); });
      dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
      int sw, sh; renderer_size(r, &sw, &sh);
      [png writeToFile:[outDir stringByAppendingPathComponent:@"hires.png"] atomically:YES];
      printf("hi-res export: %dx%d PNG %lu bytes; renderer back at %dx%d\n", ew, eh, (unsigned long)png.length, sw, sh);
      if (!png || ew != 3840 || sw != 560) { printf("  FAIL: export\n"); fails++; }
      renderer_destroy(r);
    }

    // ---- 4c. the hands' looks: where the light lands, on top and fed into the echoes.
    if (!looksTest(dev, q, shaders, outDir, chambers)) fails++;

    timing:
    if (!looksTiming(dev, shaders, chambers)) fails++;
    // ---- 5. GPU time at 3024 x 1964, 4 taps; echo scale 0.75 as the web picks on this screen.
    {
      const int w = 3024, h = 1964;
      id<MTLTexture> tgt = makeTarget(dev, w, h);
      printf("GPU time per frame at %dx%d, taps 4, echo 0.75 (headroom 4):\n", w, h);
      const char* tonly = getenv("FX_TIME_ONLY");   // FX_TIME_ONLY=aurora,galaxy: time just these
      for (const std::string& slug : order) {
        if (tonly && !strstr(tonly, slug.c_str())) continue;
        Renderer* r = renderer_create(dev, shaders, &err);
        // FX_Q / FX_TAPS: a detail level's render scale and taps (echo scale as the engine picks it).
        const double q = getenv("FX_Q") ? atof(getenv("FX_Q")) : 1, dens = 2 * q;
        renderer_resize(r, (int)lround(w * q), (int)lround(h * q), dens > 1.5 ? (float)std::max(0.6, 1.5 / dens) : 1.f, w, h, (int)lround(w * 0.75), (int)lround(h * 0.75));
        renderer_set_detail(r, getenv("FX_TAPS") ? atoi(getenv("FX_TAPS")) : 4, 76);
        __block double sum = 0, mx = 0; __block int cnt = 0; __block bool measuring = false;
        renderer_set_gpu_time_handler(r, ^(double s) { if (measuring) { sum += s; mx = fmax(mx, s); cnt++; if (getenv("FX_SPIKES") && s > atof(getenv("FX_SPIKES")) * 1e-3) printf("    spike %.2f ms at measured frame %d\n", s * 1e3, cnt); } });   // FX_SPIKES=5: list frames over 5 ms
        Engine e; e.init(chambers[slug]);
        const bool sky = chambers[slug].stars > 0;
        e.stress = sky;
        for (int i = 0; i < 240; i++) { if (i == 120) {
          measuring = true;
#ifdef FX_PASS_TIMING
          renderer_finish(r); renderer_pass_times_reset();
#endif
        } renderer_render(r, e.step(r, 1 / 60.0), tgt, nil, 4);
          // FX_PACE=1: one frame per 1/120 s, as the live app runs (frames then do not
          // overlap on the GPU, and its clocks settle as they do on screen).
          if (getenv("FX_PACE")) { static double next = 0; double now = CACurrentMediaTime(); if (next < now) next = now; next += 1 / 120.0; while (CACurrentMediaTime() < next) usleep(200); }
        }
        renderer_destroy(r);
        printf("  %-13s mean %.2f ms, max %.2f ms (%d frames)%s\n", slug.c_str(), sum / cnt * 1e3, mx * 1e3, cnt, sky ? "  [8 fingers]" : "");
#ifdef FX_PASS_TIMING
        renderer_pass_times_print();
#endif
        // The sky places must leave room inside a 120 Hz frame (8.3 ms) at this size even
        // with every finger down: the whole frame, not just their own passes.
        if (sky && (sum / cnt > 5.0e-3 || mx > 7.5e-3)) { printf("  FAIL: %s over the 120 Hz budget (mean 5.0 ms, max 7.5 ms)\n", slug.c_str()); fails++; }
      }
    }
    CGColorSpaceRelease(srgb);
    printf(fails ? "FAILURES: %d\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
  }
}
