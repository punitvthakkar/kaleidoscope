// GLES 3 port of the web Renderer. Pass order, uniforms and conditions follow
// index.html's Renderer.render line by line; comments mark native differences, which
// are the Mac's (mac/render/renderer.mm): targets allocated once at the largest size
// the detail ladder uses (each level renders into their top-left part), clear /
// restore / veil queued to the start of the next frame, GPU time from timer queries,
// stills read back without a stall, and the final pass split out so a film can draw it
// again.
#include "../renderer.h"
#include "../gl_util.h"
#include "debug.h"
#include "../../../mac/render/sky_layout.h"
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <cstddef>
#include <utility>

#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fx", __VA_ARGS__)

namespace {

// sRGB transfer, extended past 1 the usual way (same curve, any magnitude).
float srgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
float linToSrgb(float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f; }
// FINAL_FS tone curves on the max channel (see the patch in android/tools/make_shaders.mjs).
float tmSdr(float m) { return m > 0.7f ? 0.7f + 0.3f * (1 - expf(-(m - 0.7f) * 3.333f)) : m; }
float tmTo(float m, float W) { return m > 0.7f ? 0.7f + (W - 0.7f) * (1 - expf(-(m - 0.7f) / (W - 0.7f))) : m; }

// Bounds for the flash guard's HDR limit, as renderer.mm: per channel value x, SDR
// shows at most Ks * x and the HDR shoulder adds at most G * x of linear light.
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

double nowSec() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Camera light: SurfaceTexture images are external textures, which allow only
// clamp-to-edge; the web samples its camera with mirrored repeat. Each new camera
// image is copied (upright, as the web's video texture) into a small 2D texture that
// FEEDBACK_FS samples unchanged.
const char* kCamVS = "#version 300 es\nvoid main() {\n  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
                     "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n}";
const char* kCamFS = "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\nprecision highp float;\n"
                     "uniform samplerExternalOES uSrc;\nuniform mat4 uM;\nuniform vec2 uOut;\nout vec4 o;\n"
                     "void main() {\n  vec2 uv = gl_FragCoord.xy / uOut;\n"
                     "  o = texture(uSrc, (uM * vec4(uv.x, 1.0 - uv.y, 0.0, 1.0)).xy);\n}";

// Every uniform any program uses; each program looks them up once, at link time.
// u*Pic: the part of a target in use (index.html picUV / picUVM).
#define FX_UNIFORMS(X)                                                                                     \
  X(uRes) X(uMode) X(uSym) X(uMirror) X(uSpin) X(uCenter) X(uViewZoom) X(uWarp) X(uKifsIters) X(uKifs)     \
  X(uKifsOff) X(uHyp) X(uDroste) X(uTile) X(uSpinCS) X(uKifsCS) X(uPalA) X(uPalB) X(uPalC) X(uPalD)        \
  X(uRmRot) X(uRmScale) X(uRmZ) X(uRmT) X(uRmSteps) X(uPrev) X(uCam) X(uRM) X(uFoldLoop) X(uColorMat)     \
  X(uFbRot) X(uFbRotCS) X(uFbZoom) X(uFbSwirl) X(uFbWarp) X(uFbWarpT) X(uSharpen) X(uDt) X(uDream)        \
  X(uDreamC) X(uRipN) X(uRip) X(uGen) X(uGenK) X(uGenScale) X(uGenT) X(uGenRadius) X(uCamOn) X(uCamK)     \
  X(uCamZoom) X(uCamAspect) X(uCamColorize) X(uCopies) X(uInkMirror) X(uGlow) X(uFB) X(uVeilTex) X(uTaps) \
  X(uSheen) X(uSheenL) X(uFisheye) X(uSky) X(uSkyMirror) X(uSkyView) X(uSkyAt) X(uSkyBox) X(uSkyLensN)    \
  X(uSkyLens) X(uSkySharp) X(uSkySoft) X(uFbScale) X(uPalMap) X(uPalShift) X(uVeil) X(uThreshold) X(uSrc) \
  X(uTexel) X(uOut) X(uPrefilter) X(uCur) X(uK) X(uScene) X(uBloomTex) X(uLumCur) X(uLumAdapt) X(uGuard)  \
  X(uCap) X(uBloom) X(uChroma) X(uGrain) X(uVignette) X(uExposure) X(uFrame) X(uPeak) X(uKs) X(uG)        \
  X(uBloomMeanK) X(uOutMode) X(uM) X(uState) X(uPrevPic) X(uRMPic) X(uFBPic) X(uVeilPic)         \
  X(uScenePic) X(uBloomPic) X(uSrcPic) X(uGain) X(uLookMirror) X(uFish) X(uKind) X(uT) X(uBreath) X(uHands)  \
  X(uPts) X(uHand) X(uRmQ) X(uRmShape) X(uRmLook) X(uRmSlice) X(uRmDetail) X(uRmZoom) X(uRmYaw) X(uRmPitch) \
  X(uRmPad) X(uRmCamP) X(uRmCamF) X(uRmCamU)

struct Loc {
#define X(n) GLint n = -1;
  FX_UNIFORMS(X)
#undef X
};

struct Prog { GLuint p = 0; Loc u; };

// A texture with a framebuffer per mip level. w x h is its storage; vw x vh the part
// in use, which the spare uniforms tell the shaders.
struct Target {
  GLuint tex = 0, fbo = 0; int w = 0, h = 0, vw = 0, vh = 0, levels = 1;
  std::vector<GLuint> mipFbo;   // [0] is fbo
};

// Everything the final pass needs, kept from the last frame so a film can redraw it.
struct FinalState {
  bool valid = false, guard = false, doBloom = false;
  float cap = 0.3f, bloom = 0, chroma = 0, grain = 0, vignette = 0, exposure = 1, bloomMeanK = 1;
  unsigned frame = 0;
};

// The sky places' uniforms: the std140 block StarsU that android/tools/make_stars.mjs
// writes into the translated shaders, laid out byte for byte as the Metal struct in
// mac/render/shaders/uniforms.h (whose float3 are padded to 16 bytes, as vec4 here).
struct alignas(16) StarsUA {
  float sCore[8][4], sVel[8][4], sBlast[4], sView[4], sPal[4][4];
  float sBB[4], sBBPrev[4], sFb[4];
  float sRes[2], sDim[2];
  int sCores, sAuto, sReset, sKind, sMirror, sHead;
  float sDt, sTime, sSeed, sBright, sPalShift, sSize;
  float sSym, sPulse, sHeadBright, sHeadSize, sPx;
  float sTexDen, sTexK, sEchoKeep, sReach;
};
static_assert(offsetof(StarsUA, sBB) == 352 && offsetof(StarsUA, sRes) == 400 && offsetof(StarsUA, sCores) == 416
              && offsetof(StarsUA, sDt) == 440 && offsetof(StarsUA, sTexDen) == 484 && sizeof(StarsUA) == 512,
              "StarsUA must match the std140 block (and the Metal StarsU)");
const int kStarsW = 512, kStarsH = 384;   // as mac/render/renderer.mm: one point per texel

// One picture's targets and the state that goes with them: the live picture's, or an
// export's, swapped in while the export renders.
struct RenderSet {
  Target fb[2]; int cur = 0;
  Target sceneT, rm, veil, backup, lumCur, lumAd[2];
  std::vector<Target> bloom; int nb = 0;   // allocated levels; levels the current size uses
  int lumIdx = 0; bool lumReset = true;
  int W = 0, H = 0, FW = 0, FH = 0; float FS = 1;
  int capW = 0, capH = 0, capFW = 0, capFH = 0;   // allocated sizes
  int resW = 0, resH = 0, resFW = 0, resFH = 0;   // renderer_reserve
  enum Op { CLEAR, RESTORE, VEIL } ops[16]; int nOps = 0;
  float veilAmt = 0, veilDur = 1.4f; bool veilOn = false;
  bool backupOn = false; double backupUntil = 0;
  // The sky places' state textures exist only while a sky is open.
  GLuint starTex[2] = {}, starFbo[2] = {}; int starCur = 0;
  // The wedge textures the points are drawn into (sky_layout.h): sharp, and soft with
  // its trails ping-ponged. The scene pass folds them out.
  Target skySharp, skySoft[2]; int skyCur = 0, skyKey[4] = {};
  float skyBoxPrev[4] = {}; bool skyPrevOk = false;
  SkyLayout skyL{};
  FinalState last;
};

struct ExportJob {
  RenderSet set;
  int w = 0, h = 0, left = 0;
  std::function<const Frame&(float)> step;
  RendererPixels done;
};

struct Readback { GLuint pbo = 0; GLsync fence = nullptr; int w = 0, h = 0; RendererPixels done; };

}  // namespace

struct Renderer : RenderSet {
  Caps caps;
  GLenum hdrInternal = GL_RGBA8;
  Prog feedback, raymarch, scene, down, up, fin, copy, lum, adapt, ink, cam;
  GLuint vao = 0, inkVao = 0, inkBuf = 0;
  // The hands' look (index.html drawLooks): segments, strips and the aura. Off for good
  // if its shaders do not build; the rest of the picture is unaffected.
  Prog look, strip, aura;
  bool looksOk = false;
  GLuint lookVao = 0, lookBuf = 0, stripVao = 0, stripBuf = 0;
  Target camT;
  unsigned frameNo = 0;
  int maxTaps = 4, rmSteps = 76;
  float kifsCS[20] = {};
  int targetW = 0, targetH = 0;   // the last frame's final pass

  RendererPixels snapDone;
  std::vector<Readback> reads;
  std::unique_ptr<ExportJob> job;

  GLuint camTex = 0; float camM[16] = {}; float camAspect = 16.f / 9.f; bool camFresh = false;

  enum { NQ = 4 };
  GLuint q[NQ] = {}; bool qPending[NQ] = {}; int qNext = 0, qOld = 0; bool qOpen = false;
  double gpuSec = 0;

  float Ks = 0, peakFor = -1, G = 0;

  // The sky places (stars.metal, translated at build time). Off for good if the device
  // cannot render to 32-bit float targets or the shaders do not build.
  Prog starStep, starDraw, starTrail;
  bool starsOk = false;
  GLuint starUbo[2] = {}; GLint starUboSize = 0;   // [0] the step and the soft light, [1] the sharp points: each written once a frame
};

static RenderSet& setOf(Renderer* r) { return *r; }

// ---------------------------------------------------------------- setup

static bool readFile(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[8192]; size_t n;
  out.clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return true;
}

static void setErr(char* err, int errLen, const std::string& s) {
  if (!err || errLen <= 0) return;
  snprintf(err, (size_t)errLen, "%s", s.c_str());
}

static bool link(Prog& pr, const std::string& vs, const std::string& fs, const char* name, std::string& log) {
  std::string l;
  GLuint v = gl_compile(GL_VERTEX_SHADER, vs.c_str(), &l);
  if (!v) { log = std::string(name) + ": " + l; return false; }
  GLuint f = gl_compile(GL_FRAGMENT_SHADER, fs.c_str(), &l);
  if (!f) { log = std::string(name) + ": " + l; glDeleteShader(v); return false; }
  pr.p = glCreateProgram();
  glAttachShader(pr.p, v); glAttachShader(pr.p, f);
  glLinkProgram(pr.p);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(pr.p, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint n = 0; glGetProgramiv(pr.p, GL_INFO_LOG_LENGTH, &n);
    std::string ll(std::max(1, n), '\0');
    glGetProgramInfoLog(pr.p, n, nullptr, ll.data());
    log = std::string(name) + " link: " + ll.c_str();
    return false;
  }
#define X(n) pr.u.n = glGetUniformLocation(pr.p, #n);
  FX_UNIFORMS(X)
#undef X
  return true;
}

static int levelsFor(int w, int h) { int n = 1; while ((w | h) >> n) n++; return n; }

// The one texture builder: immutable storage, a framebuffer per level, cleared to black.
static bool makeTex(Target& t, int w, int h, GLenum format, GLenum wrap, bool mips) {
  while (glGetError() != GL_NO_ERROR) {}
  t.w = t.vw = w; t.h = t.vh = h;
  t.levels = mips ? levelsFor(w, h) : 1;
  glGenTextures(1, &t.tex);
  glBindTexture(GL_TEXTURE_2D, t.tex);
  glTexStorage2D(GL_TEXTURE_2D, t.levels, format, w, h);
  if (glGetError() != GL_NO_ERROR) return false;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
  t.mipFbo.assign((size_t)t.levels, 0);
  glGenFramebuffers(t.levels, t.mipFbo.data());
  t.fbo = t.mipFbo[0];
  glClearColor(0, 0, 0, 1);
  for (int l = 0; l < t.levels; l++) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.mipFbo[(size_t)l]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, l);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    glClear(GL_COLOR_BUFFER_BIT);
  }
  return glGetError() == GL_NO_ERROR;
}

// A render target in the picture's format (web: target()).
static bool makeTarget(Renderer* r, Target& t, int w, int h, bool hdr, GLenum wrap, bool mips) {
  return makeTex(t, w, h, hdr ? r->hdrInternal : GL_RGBA8, wrap, mips);
}

// A wedge texture: light only, 4 bytes a texel, filtered, clamped.
static bool skyTarget(Target& t, int w, int h) { return makeTex(t, w, h, GL_R11F_G11F_B10F, GL_CLAMP_TO_EDGE, false); }

static void freeT(Target& t) {
  if (!t.mipFbo.empty()) glDeleteFramebuffers((GLsizei)t.mipFbo.size(), t.mipFbo.data());
  else if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
  if (t.tex) glDeleteTextures(1, &t.tex);
  t = Target();
}

// The web's 1x1 black camera texture, also the stand-in for an unused sampler.
static void blackCam(Renderer* r) {
  glGenTextures(1, &r->camT.tex);
  glBindTexture(GL_TEXTURE_2D, r->camT.tex);
  const uint8_t black[4] = {0, 0, 0, 0};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
  r->camT.w = r->camT.h = r->camT.vw = r->camT.vh = 1;
  glBindTexture(GL_TEXTURE_2D, 0);
}

Renderer* renderer_create(const Caps& caps, const char* shaderDir, char* err, int errLen) {
  Renderer* r = new Renderer();
  r->caps = caps;
  // The web: RGBA16F when float targets render (EXT_color_buffer_float), else RGBA8.
  if (caps.colorHalfFloat || caps.colorFloat) r->hdrInternal = GL_RGBA16F;

  const std::string dir = shaderDir;
  const char* files[] = {"fullscreen.vert", "feedback.frag", "raymarch.frag", "scene.frag", "down.frag", "up.frag",
                         "final.frag", "copy.frag", "lum.frag", "adapt.frag", "ink.vert", "ink.frag"};
  std::string src[12];
  for (int i = 0; i < 12; i++) {
    if (!readFile(dir + "/" + files[i], src[i])) {
      setErr(err, errLen, "renderer: cannot read " + dir + "/" + files[i]);
      delete r; return nullptr;
    }
  }
  const std::string& VS = src[0];
  std::string log;
  const bool ok = link(r->feedback, VS, src[1], "feedback", log) && link(r->raymarch, VS, src[2], "raymarch", log)
      && link(r->scene, VS, src[3], "scene", log) && link(r->down, VS, src[4], "down", log)
      && link(r->up, VS, src[5], "up", log) && link(r->fin, VS, src[6], "final", log)
      && link(r->copy, VS, src[7], "copy", log) && link(r->lum, VS, src[8], "lum", log)
      && link(r->adapt, VS, src[9], "adapt", log) && link(r->ink, src[10], src[11], "ink", log)
      && (!caps.externalEssl3 || link(r->cam, kCamVS, kCamFS, "camera", log));
  if (!ok) { setErr(err, errLen, log); renderer_destroy(r); return nullptr; }

  glGenVertexArrays(1, &r->vao);
  glGenVertexArrays(1, &r->inkVao);
  glBindVertexArray(r->inkVao);
  glGenBuffers(1, &r->inkBuf);
  glBindBuffer(GL_ARRAY_BUFFER, r->inkBuf);
  glBufferData(GL_ARRAY_BUFFER, RENDERER_MAX_SEG * 32, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (const void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 32, (const void*)16);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  // The hands' look: optional, like the sky places.
  {
    std::string lv, lf, sv, sf, af, llog;
    if (readFile(dir + "/look.vert", lv) && readFile(dir + "/look.frag", lf) && readFile(dir + "/strip.vert", sv)
        && readFile(dir + "/strip.frag", sf) && readFile(dir + "/aura.frag", af)
        && link(r->look, lv, lf, "look", llog) && link(r->strip, sv, sf, "strip", llog) && link(r->aura, VS, af, "aura", llog)) {
      // Segments: 12 floats each (LookData::seg), one instance per segment and mirror copy.
      glGenVertexArrays(1, &r->lookVao);
      glBindVertexArray(r->lookVao);
      glGenBuffers(1, &r->lookBuf);
      glBindBuffer(GL_ARRAY_BUFFER, r->lookBuf);
      glBufferData(GL_ARRAY_BUFFER, LOOK_MAX_SEG * 48, nullptr, GL_DYNAMIC_DRAW);
      for (int a = 0; a < 3; a++) {
        glEnableVertexAttribArray((GLuint)a);
        glVertexAttribPointer((GLuint)a, 4, GL_FLOAT, GL_FALSE, 48, (const void*)(intptr_t)(a * 16));
      }
      // Strips: 6 floats a vertex (LookData::vert), xy then four by kind.
      glGenVertexArrays(1, &r->stripVao);
      glBindVertexArray(r->stripVao);
      glGenBuffers(1, &r->stripBuf);
      glBindBuffer(GL_ARRAY_BUFFER, r->stripBuf);
      glBufferData(GL_ARRAY_BUFFER, LOOK_MAX_VERT * 24, nullptr, GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, (const void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (const void*)8);
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      r->looksOk = true;
      __android_log_print(ANDROID_LOG_INFO, "fx", "renderer: hand looks on (look, strip, aura linked)");
    } else LOGE("renderer: hand looks off: %s", llog.empty() ? "look shaders missing" : llog.c_str());
  }

  blackCam(r);

  if (caps.timerQuery) glGenQueries(Renderer::NQ, r->q);
  r->Ks = sdrBound();

  // The sky places: optional. Without them the other chambers run as before.
  if (caps.colorFloat) {
    std::string st, sv, sf, slog;
    std::string str;
    if (readFile(dir + "/stars_step.frag", st) && readFile(dir + "/stars.vert", sv) && readFile(dir + "/stars.frag", sf)
        && readFile(dir + "/stars_trail.frag", str)
        && link(r->starStep, VS, st, "stars_step", slog) && link(r->starDraw, sv, sf, "stars", slog)
        && link(r->starTrail, VS, str, "stars_trail", slog)) {
      // The block's size as the driver lays it out: the buffer must be at least that big
      // (a smaller one is undefined behaviour), so it is the larger of the two.
      GLint need = 0;
      for (Prog* p : {&r->starStep, &r->starDraw, &r->starTrail}) {
        const GLuint bi = glGetUniformBlockIndex(p->p, "StarsU");
        if (bi == GL_INVALID_INDEX) continue;
        glUniformBlockBinding(p->p, bi, 0);
        GLint n = 0; glGetActiveUniformBlockiv(p->p, bi, GL_UNIFORM_BLOCK_DATA_SIZE, &n);
        need = std::max(need, n);
      }
      r->starUboSize = std::max<GLint>(need, (GLint)sizeof(StarsUA));
      __android_log_print(ANDROID_LOG_INFO, "fx", "renderer: sky places on (StarsU %d bytes, buffer %d)", need, r->starUboSize);
      glGenBuffers(2, r->starUbo);
      for (GLuint b : r->starUbo) { glBindBuffer(GL_UNIFORM_BUFFER, b); glBufferData(GL_UNIFORM_BUFFER, r->starUboSize, nullptr, GL_DYNAMIC_DRAW); }
      glBindBuffer(GL_UNIFORM_BUFFER, 0);
      r->starsOk = true;
    } else LOGE("renderer: sky places off: %s", slog.empty() ? "stars shaders missing" : slog.c_str());
  } else LOGE("renderer: sky places off: no GL_EXT_color_buffer_float");
  glBindTexture(GL_TEXTURE_2D, 0);
  return r;
}

static void starsFree(RenderSet& s) {
  if (s.starFbo[0]) glDeleteFramebuffers(2, s.starFbo);
  if (s.starTex[0]) glDeleteTextures(2, s.starTex);
  s.starFbo[0] = s.starFbo[1] = s.starTex[0] = s.starTex[1] = 0;
  for (Target* t : {&s.skySharp, &s.skySoft[0], &s.skySoft[1]}) freeT(*t);
  s.skyKey[0] = 0; s.skyPrevOk = false;
}

static void freeSet(RenderSet& s) {
  for (Target* t : {&s.fb[0], &s.fb[1], &s.sceneT, &s.rm, &s.veil, &s.backup, &s.lumCur, &s.lumAd[0], &s.lumAd[1]})
    freeT(*t);
  for (auto& b : s.bloom) freeT(b);
  s.bloom.clear();
  starsFree(s);
}

void renderer_destroy(Renderer* r) {
  if (!r) return;
  for (Prog* p : {&r->feedback, &r->raymarch, &r->scene, &r->down, &r->up, &r->fin, &r->copy, &r->lum, &r->adapt, &r->ink, &r->cam})
    if (p->p) glDeleteProgram(p->p);
  freeSet(setOf(r));
  if (r->job) freeSet(r->job->set);
  for (Readback& rb : r->reads) { glDeleteSync(rb.fence); glDeleteBuffers(1, &rb.pbo); }
  freeT(r->camT);
  if (r->vao) glDeleteVertexArrays(1, &r->vao);
  if (r->inkVao) glDeleteVertexArrays(1, &r->inkVao);
  if (r->inkBuf) glDeleteBuffers(1, &r->inkBuf);
  if (r->caps.timerQuery) glDeleteQueries(Renderer::NQ, r->q);
  for (Prog* p : {&r->starStep, &r->starDraw, &r->starTrail, &r->look, &r->strip, &r->aura}) if (p->p) glDeleteProgram(p->p);
  for (GLuint v : {r->lookVao, r->stripVao}) if (v) glDeleteVertexArrays(1, &v);
  for (GLuint b : {r->lookBuf, r->stripBuf}) if (b) glDeleteBuffers(1, &b);
  if (r->starUbo[0]) glDeleteBuffers(2, r->starUbo);
  delete r;
}

// ---------------------------------------------------------------- targets

static void use(const Prog& p) { glUseProgram(p.p); }
static void tex(int unit, GLuint t) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); }
// A target to a sampler, with the share of it beyond the part in use (web: spare()).
// pic: index.html's *Pic uniform (picUV): the picture and texture sizes, 0 when it fills t.
static void bindT(int unit, GLint sampler, GLint pic, const Target& t) {
  tex(unit, t.tex);
  glUniform1i(sampler, unit);
  if (t.vw == t.w && t.vh == t.h) glUniform4f(pic, 0, 0, 0, 0);
  else glUniform4f(pic, (float)t.vw, (float)t.vh, (float)t.w, (float)t.h);
}
static void draw() { glDrawArrays(GL_TRIANGLES, 0, 3); }
// The target's old contents are not needed: a tiled GPU then never loads them.
static void invalidate() {
  const GLenum att = GL_COLOR_ATTACHMENT0;
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &att);
}

// Mip levels of the part in use only, each a 2:1 filtered blit of the one above: level
// l holds max(1, vw >> l) x max(1, vh >> l), at least what picUVM reads there.
static void mips(const Target& t) {
  int w = t.vw, h = t.vh;
  for (int l = 0; l + 1 < t.levels; l++) {
    const int nw = std::max(1, w >> 1), nh = std::max(1, h >> 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, t.mipFbo[(size_t)l]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.mipFbo[(size_t)l + 1]);
    glBlitFramebuffer(0, 0, w, h, 0, 0, nw, nh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    w = nw; h = nh;
  }
}

static void clearT(const Target& t) {
  glClearColor(0, 0, 0, 1);
  for (GLuint f : t.mipFbo) { glBindFramebuffer(GL_FRAMEBUFFER, f); glClear(GL_COLOR_BUFFER_BIT); }
}

// src (its part in use) resampled into the top-left vw x vh of dst. COPY_FS reads
// gl_FragCoord / uOut, so uOut is stretched to cover only src's part.
static void copyInto(Renderer* r, const Target& src, Target& dst, int vw, int vh) {
  dst.vw = std::max(1, std::min(vw, dst.w)); dst.vh = std::max(1, std::min(vh, dst.h));
  glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
  glViewport(0, 0, dst.vw, dst.vh);
  use(r->copy);
  tex(0, src.tex);
  glUniform1i(r->copy.u.uSrc, 0);
  glUniform2f(r->copy.u.uOut, (float)dst.vw * src.w / src.vw, (float)dst.vh * src.h / src.vh);
  draw();
  if (dst.levels > 1) mips(dst);
}

// Allocates the size-dependent targets at cw x ch (render) and cfw x cfh (echo). The
// web creates the veil, undo copy and lattice buffer on demand; here the veil and undo
// copy are made when one starts (renderer_start_veil, renderer_clear), between frames.
// Everything but the current feedback image is freed first, so the peak is one set
// plus that image (it is resampled into the new set, then freed).
static bool build(Renderer* r, int cw, int ch, int cfw, int cfh) {
  Target keep = std::move(r->fb[r->cur]);
  r->fb[r->cur] = Target();
  for (Target* t : {&r->fb[0], &r->fb[1], &r->sceneT, &r->rm}) freeT(*t);
  for (auto& b : r->bloom) freeT(b);
  r->bloom.clear();
  r->capW = r->capH = r->capFW = r->capFH = 0;
  bool ok = makeTarget(r, r->fb[0], cfw, cfh, true, GL_MIRRORED_REPEAT, true)
         && makeTarget(r, r->fb[1], cfw, cfh, true, GL_MIRRORED_REPEAT, true)
         && makeTarget(r, r->sceneT, cw, ch, true, GL_CLAMP_TO_EDGE, false)
         && makeTarget(r, r->rm, std::max(16, cfw >> 1), std::max(16, cfh >> 1), true, GL_MIRRORED_REPEAT, false);
  int bw = cw, bh = ch;
  for (int i = 0; ok && i < 6 && bw > 8 && bh > 8; i++) {
    bw = std::max(1, bw >> 1); bh = std::max(1, bh >> 1);
    r->bloom.emplace_back();
    ok = makeTarget(r, r->bloom.back(), bw, bh, true, GL_CLAMP_TO_EDGE, false);
  }
  if (ok && !r->lumCur.tex) {
    ok = makeTarget(r, r->lumCur, 1, 1, true, GL_CLAMP_TO_EDGE, false)
      && makeTarget(r, r->lumAd[0], 1, 1, true, GL_CLAMP_TO_EDGE, false)
      && makeTarget(r, r->lumAd[1], 1, 1, true, GL_CLAMP_TO_EDGE, false);
    if (!ok) { freeT(r->lumCur); freeT(r->lumAd[0]); freeT(r->lumAd[1]); }
  }
  if (!ok) {   // nothing allocated stays; the feedback image is kept for another try
    for (Target* t : {&r->fb[0], &r->fb[1], &r->sceneT, &r->rm}) freeT(*t);
    for (auto& b : r->bloom) freeT(b);
    r->bloom.clear();
    r->cur = 0; r->fb[0] = std::move(keep);
    return false;
  }
  if (keep.tex) copyInto(r, keep, r->fb[0], keep.vw, keep.vh);   // resample: nothing resets
  freeT(keep);
  r->cur = 0;
  r->capW = cw; r->capH = ch; r->capFW = cfw; r->capFH = cfh;
  return true;
}

void renderer_reserve(Renderer* r, int capW, int capH, int capEchoW, int capEchoH) {
  r->resW = capW; r->resH = capH; r->resFW = capEchoW; r->resFH = capEchoH;
}

// Targets are allocated at the reserved size (or the request, if larger) and
// reallocated only when that changes; a smaller render uses their top-left part.
bool renderer_resize(Renderer* r, int w, int h, float fs) {
  w = std::max(16, w); h = std::max(16, h);
  const int fw = std::max(16, (int)lroundf(w * fs)), fh = std::max(16, (int)lroundf(h * fs));
  const int cw = std::max(w, r->resW), ch = std::max(h, r->resH), cfw = std::max(fw, r->resFW), cfh = std::max(fh, r->resFH);
  const bool same = cw == r->capW && ch == r->capH && cfw == r->capFW && cfh == r->capFH && r->sceneT.tex;
  if (same && w == r->W && h == r->H && fs == r->FS) return true;
  if (!same) {
    if (r->caps.maxTexture > 0 && (cw > r->caps.maxTexture || ch > r->caps.maxTexture)) return false;
    const int ow = r->capW, oh = r->capH, ofw = r->capFW, ofh = r->capFH;
    glBindVertexArray(r->vao);
    glDisable(GL_BLEND);
    const bool ok = build(r, cw, ch, cfw, cfh);
    if (!ok && ow > 0 && !build(r, ow, oh, ofw, ofh)) freeT(r->fb[0]);   // back to the old size, or empty
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) return false;
  }
  r->W = w; r->H = h; r->FW = fw; r->FH = fh; r->FS = fs;
  int bw = w, bh = h, n = 0;
  while (n < 6 && n < (int)r->bloom.size() && bw > 8 && bh > 8) { bw = std::max(1, bw >> 1); bh = std::max(1, bh >> 1); n++; }
  r->nb = n;
  return true;
}

// ---------------------------------------------------------------- state calls

static void queueOp(Renderer* r, RenderSet::Op op) {
  if (op == RenderSet::VEIL) r->veilOn = true;
  if (r->nOps < 16) r->ops[r->nOps++] = op;
}

// The undo copy and the veil exist only while in use; made here, between frames.
static bool ensure(Renderer* r, Target& t, int w, int h, GLenum wrap) {
  if (t.tex && t.w >= w && t.h >= h) return true;
  freeT(t);
  if (makeTarget(r, t, w, h, true, wrap, false)) return true;
  freeT(t);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return false;
}

void renderer_clear(Renderer* r) {
  if (r->sceneT.tex) ensure(r, r->backup, r->FW, r->FH, GL_MIRRORED_REPEAT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  queueOp(r, RenderSet::CLEAR);
}
void renderer_restore(Renderer* r) { queueOp(r, RenderSet::RESTORE); }

void renderer_start_veil(Renderer* r, float duration) {
  if (r->sceneT.tex) ensure(r, r->veil, r->W, r->H, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  r->veilAmt = 1;
  r->veilDur = std::max(0.2f, duration);
  queueOp(r, RenderSet::VEIL);
}

void renderer_fade_veil(Renderer* r, float dt) {
  r->veilAmt = std::max(0.f, r->veilAmt - dt / r->veilDur);
  if (r->veilAmt == 0) { r->veilOn = false; if (r->veil.tex) freeT(r->veil); }
}

void renderer_set_detail(Renderer* r, int taps, int steps) { r->maxTaps = taps; r->rmSteps = steps; }

// Queued calls run at the start of the next frame, in order (as the Mac).
static void runOps(Renderer* r) {
  const int n = r->nOps; r->nOps = 0;
  const double now = nowSec();
  if (r->backupOn && now > r->backupUntil) { r->backupOn = false; freeT(r->backup); }   // undo window
  if (!r->sceneT.tex) return;
  for (int i = 0; i < n; i++) {
    if (r->ops[i] == RenderSet::CLEAR) {
      if (r->backup.tex) {
        copyInto(r, r->fb[r->cur], r->backup, r->fb[r->cur].vw, r->fb[r->cur].vh);
        r->backupOn = true; r->backupUntil = now + 8;
      }
      clearT(r->fb[0]); clearT(r->fb[1]);
    } else if (r->ops[i] == RenderSet::RESTORE) {
      if (r->backupOn && r->backup.tex) copyInto(r, r->backup, r->fb[r->cur], r->FW, r->FH);
    } else if (r->veil.tex) {
      copyInto(r, r->sceneT, r->veil, r->sceneT.vw, r->sceneT.vh);   // the composite scene of the last frame
    }
  }
}

// ---------------------------------------------------------------- timing

// Collects finished timer queries without waiting; a disjoint event voids them all.
static void pollQueries(Renderer* r) {
  if (!r->caps.timerQuery) return;
  GLint disjoint = 0;
  glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
  if (disjoint) {
    for (bool& p : r->qPending) p = false;
    r->qOld = r->qNext;
    return;
  }
  while (r->qPending[r->qOld]) {
    GLuint avail = 0;
    glGetQueryObjectuiv(r->q[r->qOld], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) break;
    GLuint ns = 0;
    glGetQueryObjectuiv(r->q[r->qOld], GL_QUERY_RESULT, &ns);
    r->gpuSec = ns * 1e-9;
    r->qPending[r->qOld] = false;
    r->qOld = (r->qOld + 1) % Renderer::NQ;
  }
}

// ---------------------------------------------------------------- frame

static void setCommon(Renderer* r, const Prog& pr, const Frame& f, int w, int h) {
  const Fold& fd = f.fold;
  const Loc& u = pr.u;
  glUniform2f(u.uRes, (float)w, (float)h);
  glUniform1i(u.uMode, fd.mode);
  glUniform1f(u.uSym, fd.sym);
  glUniform1i(u.uMirror, fd.mirror ? 1 : 0);
  glUniform1f(u.uSpin, fd.spin);
  glUniform2f(u.uCenter, fd.cx, fd.cy);
  glUniform1f(u.uViewZoom, fd.vz);
  glUniform4f(u.uWarp, fd.warpAmt, fd.warpScale, fd.warpT, 0);
  glUniform1f(u.uKifsIters, fd.kIters);
  glUniform2f(u.uKifs, fd.kScale, fd.kAngle);
  glUniform2f(u.uKifsOff, fd.kOffX, fd.kOffY);
  glUniform4f(u.uHyp, fd.hypC, fd.hypR, fd.hypAx, fd.hypAy);
  glUniform2f(u.uDroste, fd.drL, fd.drPhase);
  glUniform1f(u.uTile, fd.tile);
  glUniform2f(u.uSpinCS, (float)cos((double)fd.spin), (float)sin((double)fd.spin));
  if (fd.mode == 2 && u.uKifsCS >= 0) {
    for (int i = 0; i < 10; i++) { double a = fd.kAngle / (1 + 0.45 * i); r->kifsCS[i * 2] = (float)cos(a); r->kifsCS[i * 2 + 1] = (float)sin(a); }
    glUniform2fv(u.uKifsCS, 10, r->kifsCS);
  }
  glUniform3fv(u.uPalA, 1, f.pal.a);
  glUniform3fv(u.uPalB, 1, f.pal.b);
  glUniform3fv(u.uPalC, 1, f.pal.c);
  glUniform3fv(u.uPalD, 1, f.pal.d);
}

// Decay x saturation x hue rotation about the gray axis, as one matrix (column-major).
static void colorMat(float out[9], float decay, float sat, float hue) {
  const double c = cos((double)hue), s = sin((double)hue), k = (1 - c) / 3, q = sqrt(1.0 / 3) * s;
  const double h[9] = {c + k, k - q, k + q, k + q, c + k, k - q, k - q, k + q, c + k};
  const double lr = 0.299 * (1 - sat), lg = 0.587 * (1 - sat), lb = 0.114 * (1 - sat);
  const double S[9] = {lr + sat, lr, lr, lg, lg + sat, lg, lb, lb, lb + sat};
  for (int col = 0; col < 3; col++)
    for (int row = 0; row < 3; row++) {
      double v = 0;
      for (int i = 0; i < 3; i++) v += S[i * 3 + row] * h[col * 3 + i];
      out[col * 3 + row] = (float)(v * decay);
    }
}

static void rmRot(float o[9], double a) {
  const double x = M_SQRT1_2, y = M_SQRT1_2, z = 0, c = cos(a), s = sin(a), t = 1 - c;
  const double m[9] = {t * x * x + c, t * x * y + s * z, t * x * z - s * y,
                       t * x * y - s * z, t * y * y + c, t * y * z + s * x,
                       t * x * z + s * y, t * y * z - s * x, t * z * z + c};
  for (int i = 0; i < 9; i++) o[i] = (float)m[i];
}

// Copies a new external camera image into camT (see kCamFS). False if there is none.
static bool cameraCopy(Renderer* r) {
  if (!r->camTex || !r->cam.p) return false;
  const int h = 360, w = std::max(16, (int)lroundf(h * r->camAspect));
  if (r->camT.w != w || r->camT.h != h || !r->camT.fbo) {
    freeT(r->camT);
    if (!makeTarget(r, r->camT, w, h, false, GL_MIRRORED_REPEAT, false)) { freeT(r->camT); blackCam(r); return false; }
    r->camFresh = true;
  }
  if (!r->camFresh) return true;   // the image already copied is still the newest
  r->camFresh = false;
  glBindFramebuffer(GL_FRAMEBUFFER, r->camT.fbo);
  invalidate();
  glViewport(0, 0, w, h);
  use(r->cam);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, r->camTex);
  glUniform1i(r->cam.u.uSrc, 0);
  glUniformMatrix4fv(r->cam.u.uM, 1, GL_FALSE, r->camM);
  glUniform2f(r->cam.u.uOut, (float)w, (float)h);
  draw();
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  return true;
}

// ---------------------------------------------------------------- sky places
// As mac/render/renderer.mm pass 1b: every point one step, then drawn once into the
// wedge textures (soft with its trails, and sharp), which the scene pass folds out.
#ifdef FX_DEBUG
// Debug only (intent extra fx.skydbg, a bit mask): 1 skips the soft light, 2 the sharp
// points, 8 logs what the step wrote once a second.
int gSkyDebug = 0;
void renderer_sky_debug(int mask) { gSkyDebug = mask; }
#define SKYDBG(bit) (gSkyDebug & (bit))
#else
#define SKYDBG(bit) 0
#endif

// The two state textures (6 MB), made when a sky opens. Positions need full floats:
// halves would step visibly. False (and the skies off) if the device refuses them.
static bool starsAlloc(Renderer* r) {
  while (glGetError() != GL_NO_ERROR) {}
  glGenTextures(2, r->starTex);
  glGenFramebuffers(2, r->starFbo);
  for (int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, r->starTex[i]);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, kStarsW, kStarsH);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, r->starFbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->starTex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { starsFree(setOf(r)); return false; }
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  r->starCur = 0;
  return glGetError() == GL_NO_ERROR;
}

static void starsUpload(GLuint ubo, GLint size, const StarsUA& su) {
  glBindBuffer(GL_UNIFORM_BUFFER, ubo);
  glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);   // orphan: never waits on a frame in flight
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof su, &su);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// The first `count` points drawn into whatever wedge texture is bound, once each, added
// (stars.metal; the scene pass reads what spills over a wall from across it).
static void starsPoints(Renderer* r, GLuint ubo, int count) {
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE);
  use(r->starDraw);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
  tex(0, r->starTex[r->starCur]); glUniform1i(r->starDraw.u.uState, 0);
  glBindVertexArray(r->vao);
  glDrawArrays(GL_POINTS, 0, count);
  glDisable(GL_BLEND);
}

// 1b. False if no sky is open (and its textures then freed).
static bool starsFrame(Renderer* r, const Frame& f) {
  const Params& P = f.P;
  const Fold& fold = f.fold;
  const bool want = P.stars > 0 && r->starsOk;
  bool fresh = false;
  if (want && !r->starTex[0]) {
    if (!starsAlloc(r)) { r->starsOk = false; LOGE("renderer: sky places off: no RGBA32F target"); return false; }
    fresh = true;
  } else if (!want && r->starTex[0]) starsFree(setOf(r));
  if (!want) return false;

  // The wedge textures, sized by the render and the mirrors only (sky_layout.h). A
  // phone draws the sharp one at nine tenths of the screen's density: the difference
  // cannot be seen at this pixel density, and it is a fifth fewer pixels to fill.
  const float sym = std::max(2.f, fold.sym);
  int tw, th, sw, sh;
  sky_tex_size(sym, fold.mirror, r->W, r->H, 0.9f, std::min(4096, r->caps.maxTexture), tw, th);
  sky_tex_size(sym, fold.mirror, r->W, r->H, 0.45f, std::min(4096, r->caps.maxTexture), sw, sh);
  const int key[4] = {tw, th, sw, sh};
  if (!r->skySharp.tex || memcmp(key, r->skyKey, sizeof key) != 0) {
    for (Target* t : {&r->skySharp, &r->skySoft[0], &r->skySoft[1]}) freeT(*t);
    if (!skyTarget(r->skySharp, tw, th) || !skyTarget(r->skySoft[0], sw, sh) || !skyTarget(r->skySoft[1], sw, sh)) {
      starsFree(setOf(r)); r->starsOk = false; LOGE("renderer: sky places off: no R11F_G11F_B10F target"); return false;
    }
    memcpy(r->skyKey, key, sizeof key);
    r->skyCur = 0; r->skyPrevOk = false;
  }
  // How many points are alive: all at the full drawable size, fewer as the engine's
  // ladder lowers the render size. The ladder trades pixels for frame rate, but a sky's
  // cost is in its points, so a hot phone gives up stars here rather than frames. The
  // first `rows` rows of the state texture move and are drawn; the rest wait.
  const float area = (float)r->W * r->H / std::max(1.f, (float)r->caps.displayW * r->caps.displayH);
  const float frac = std::min(1.f, std::max(0.35f, sqrtf(area) * 1.15f));
  const int rows = std::max(32, std::min(kStarsH, (int)lroundf(kStarsH * frac)));
  const int alive = kStarsW * rows;
  const SkyLayout L = sky_layout(sym, fold.mirror, r->W, r->H, f.starView[0], tw);
  const SkyLayout Ls = sky_layout(sym, fold.mirror, r->W, r->H, f.starView[0], sw);
  r->skyL = L;

  StarsUA su{};
  su.sCores = std::min(f.starCores, 8); su.sAuto = std::min(f.starAuto, su.sCores);
  su.sKind = P.starKind; su.sTime = f.starTime; su.sPulse = f.starPulse;
  for (int i = 0; i < 3; i++) su.sBlast[i] = f.starBlast[i];
  for (int i = 0; i < 3; i++) su.sView[i] = f.starView[i];
  su.sSym = sym; su.sMirror = fold.mirror ? 1 : 0;
  for (int i = 0; i < su.sCores; i++) {
    for (int j = 0; j < 4; j++) su.sCore[i][j] = f.starCore[i * 4 + j];
    su.sVel[i][0] = f.starVel[i * 2]; su.sVel[i][1] = f.starVel[i * 2 + 1];
  }
  const float* pal[4] = {f.pal.a, f.pal.b, f.pal.c, f.pal.d};
  for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) su.sPal[i][j] = pal[i][j];
  su.sDim[0] = kStarsW; su.sDim[1] = kStarsH;
  su.sReset = f.starsReset || fresh ? 1 : 0;      // fresh textures hold nothing yet: seed every point
  su.sDt = std::min(f.dt, 0.034f);                // a long frame must not fling the inner orbits apart
  su.sSeed = (float)(r->frameNo % 65536);
  su.sBright = 0.16f * P.stars * std::max(1 - f.decay, 0.004f) * kStarsH / rows;   // fewer points, each brighter: the soft light keeps its glow
  su.sPalShift = f.palPhase;
  const float zs = std::min(std::max(sqrtf(f.starView[0]), 0.7f), 2.2f);
  su.sSize = std::max(1.5f, r->FH / 480.f) * zs;
  su.sHeadBright = 0.75f * P.stars;
  su.sPx = std::max(1.f, r->H / 1000.f);
  su.sHeadSize = std::max(3.f, 4.5f * su.sPx) * zs;
  // A phone draws half the points into the soft light (each twice as bright): it is
  // coarse and blurred by its trails, so the difference does not show.
  su.sEchoKeep = 0.5f;
  su.sReach = (float)std::min(2.6, sky_corner(r->W, r->H) / std::max(f.starView[0], 1e-3f));
  // Step and soft light share one block, the sharp points the other.
  su.sHead = 0;
  su.sRes[0] = (float)sw; su.sRes[1] = (float)sh;
  memcpy(su.sBB, Ls.box, sizeof su.sBB);
  memcpy(su.sBBPrev, r->skyPrevOk ? r->skyBoxPrev : Ls.box, sizeof su.sBBPrev);
  su.sFb[0] = cosf(f.fbRot); su.sFb[1] = sinf(f.fbRot); su.sFb[2] = std::max(f.fbZoom, 0.5f); su.sFb[3] = r->skyPrevOk ? f.decay : 0.f;
  su.sTexDen = Ls.den; su.sTexK = Ls.k;
  starsUpload(r->starUbo[0], r->starUboSize, su);
  StarsUA sh2 = su;
  sh2.sHead = 1;
  sh2.sRes[0] = (float)tw; sh2.sRes[1] = (float)th;
  memcpy(sh2.sBB, L.box, sizeof sh2.sBB);
  sh2.sTexDen = L.den; sh2.sTexK = L.k;
  starsUpload(r->starUbo[1], r->starUboSize, sh2);

  // Every point one step.
  glBindFramebuffer(GL_FRAMEBUFFER, r->starFbo[1 - r->starCur]);
  glViewport(0, 0, kStarsW, rows);
  use(r->starStep);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->starUbo[0]);
  tex(0, r->starTex[r->starCur]); glUniform1i(r->starStep.u.uPrev, 0);
  draw();
  r->starCur = 1 - r->starCur;
#ifdef FX_DEBUG
  if (SKYDBG(8)) {
    static int n = 0;
    if (n++ % 60 == 0) {
      float px[8] = {};
      glReadPixels(0, 0, 2, 1, GL_RGBA, GL_FLOAT, px);
      LOGE("skydbg kind=%d sym=%.1f mir=%d cores=%d zoom=%.2f sharp=%dx%d soft=%dx%d | %.3f %.3f %.3f %.3f",
           su.sKind, su.sSym, su.sMirror, su.sCores, su.sView[0], tw, th, sw, sh, px[0], px[1], px[2], px[3]);
    }
  }
#endif

  // The soft light: last frame's carried on, then every point's added.
  const Target& sdst = r->skySoft[1 - r->skyCur];
  glBindFramebuffer(GL_FRAMEBUFFER, sdst.fbo);
  glViewport(0, 0, sw, sh);
  use(r->starTrail);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->starUbo[0]);
  tex(0, r->skySoft[r->skyCur].tex); glUniform1i(r->starTrail.u.uPrev, 0);
  draw();
  if (!SKYDBG(1)) starsPoints(r, r->starUbo[0], star_share(alive, su.sKind, false, su.sEchoKeep));
  r->skyCur = 1 - r->skyCur;
  memcpy(r->skyBoxPrev, Ls.box, sizeof r->skyBoxPrev); r->skyPrevOk = true;

  // The sharp points, afresh at the screen's own density.
  glBindFramebuffer(GL_FRAMEBUFFER, r->skySharp.fbo);
  invalidate();
  glViewport(0, 0, tw, th);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (!SKYDBG(2)) starsPoints(r, r->starUbo[1], star_share(alive, su.sKind, true, su.sEchoKeep));
  return true;
}

// Passes 1 to 5c of the web's render (everything before the final pass).
// The hands' look batches into their buffers, once a frame (both draws read them).
// Orphaned first, as the ink's: the driver never waits for last frame's draws.
static void uploadLooks(Renderer* r, const LookData& L) {
  const int nSeg = std::min(L.nSeg, (int)LOOK_MAX_SEG), nVert = std::min(L.nVert, (int)LOOK_MAX_VERT);
  if (nSeg > 0) {
    glBindBuffer(GL_ARRAY_BUFFER, r->lookBuf);
    glBufferData(GL_ARRAY_BUFFER, LOOK_MAX_SEG * 48, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nSeg * 48, L.seg);
  }
  if (nVert > 0) {
    glBindBuffer(GL_ARRAY_BUFFER, r->stripBuf);
    glBufferData(GL_ARRAY_BUFFER, LOOK_MAX_VERT * 24, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nVert * 24, L.vert);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// index.html drawLooks: segments and strips over `copies` mirror images (the echo feed)
// or once (on top), into the bound target's w x h part. Light adds; satin ribbons on
// top are laid over; the aura (on top only) is a fullscreen pass scissored to the hands.
static void drawLooks(Renderer* r, const Frame& f, const LookData& L, int copies, int w, int h, float gain, bool onTop) {
  const Fold& fold = f.fold;
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  auto mirrors = [&](const Loc& u) {
    glUniform2f(u.uRes, (float)w, (float)h);
    glUniform1f(u.uGain, gain);
    glUniform1i(u.uCopies, copies);
    glUniform1i(u.uLookMirror, fold.mirror ? 1 : 0);
    glUniform1f(u.uSym, std::max(1.f, fold.sym));
    glUniform1f(u.uSpin, fold.spin);
    glUniform1f(u.uFish, onTop ? 0.f : fold.fish);
    glUniform2f(u.uCenter, fold.cx, fold.cy);
  };
  const int nSeg = std::min(L.nSeg, (int)LOOK_MAX_SEG), nVert = std::min(L.nVert, (int)LOOK_MAX_VERT);
  if (nSeg > 0) {
    const Prog& pr = r->look;
    use(pr); mirrors(pr.u);
    glBindVertexArray(r->lookVao);
    for (GLuint a = 0; a < 3; a++) glVertexAttribDivisor(a, (GLuint)copies);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, nSeg * copies);
  }
  if (nVert > 0) {
    const Prog& pr = r->strip;
    use(pr); mirrors(pr.u);
    glUniform1f(pr.u.uT, L.time);
    glBindVertexArray(r->stripVao);
    for (int i = 0; i < std::min(L.nStrip, (int)LOOK_MAX_STRIP); i++) {
      const int first = L.stripFirst[i], count = std::min(L.stripCount[i], nVert - first), kind = L.stripKind[i];
      if (first < 0 || count <= 0) continue;
      glUniform1i(pr.u.uKind, kind);
      if (kind == 0 && onTop) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); else glBlendFunc(GL_ONE, GL_ONE);
      glDrawArraysInstanced(GL_TRIANGLE_STRIP, first, count, copies);
    }
  }
  if (onTop && L.auraN > 0) {
    const Prog& pr = r->aura;
    const Loc& u = pr.u;
    glBindVertexArray(r->vao);
    glBlendFunc(GL_ONE, GL_ONE);
    use(pr);
    const int n = std::min(L.auraN, 2);
    glUniform2f(u.uRes, (float)w, (float)h);
    glUniform1f(u.uT, L.time);
    glUniform1f(u.uBreath, L.breath);
    glUniform1f(u.uGain, gain);
    glUniform1i(u.uHands, n);
    glUniform2fv(u.uPts, 42, L.auraPts);
    glUniform4fv(u.uHand, 2, L.auraHand);
    // Only the box around the hands (picture space to pixels; JS Math.round).
    auto toX = [&](float x) { return (int)floorf((x * h / w * 0.5f + 0.5f) * w + 0.5f); };
    auto toY = [&](float y) { return (int)floorf((y * 0.5f + 0.5f) * h + 0.5f); };
    const int x0 = std::max(0, toX(L.box[0])), y0 = std::max(0, toY(L.box[1]));
    const int x1 = std::min(w, toX(L.box[2])), y1 = std::min(h, toY(L.box[3]));
    if (x1 > x0 && y1 > y0) {
      glEnable(GL_SCISSOR_TEST);
      glScissor(x0, y0, x1 - x0, y1 - y0);
      draw();
      glDisable(GL_SCISSOR_TEST);
    }
  }
  glBlendFunc(GL_ONE, GL_ONE);
  glDisable(GL_BLEND);
  glBindVertexArray(r->vao);
}

static void encodeFrame(Renderer* r, const Frame& f) {
  const Params& P = f.P;
  const Fold& fold = f.fold;
  r->frameNo++;
  glBindVertexArray(r->vao);
  glDisable(GL_BLEND);

  // 1. raymarched fractal source
  const bool useRm = P.gen == GEN_fractal3d;
  if (useRm) {
    const Prog& pr = r->raymarch;
    Target& rm = r->rm;
    rm.vw = std::min(rm.w, std::max(16, r->FW >> 1)); rm.vh = std::min(rm.h, std::max(16, r->FH >> 1));
    glBindFramebuffer(GL_FRAMEBUFFER, rm.fbo);
    invalidate();
    glViewport(0, 0, rm.vw, rm.vh);
    use(pr);
    setCommon(r, pr, f, rm.vw, rm.vh);
    float m[9]; rmRot(m, 0.35 + 0.25 * sin(f.rmT * 0.07));
    glUniformMatrix3fv(pr.u.uRmRot, 1, GL_FALSE, m);
    glUniform1f(pr.u.uRmScale, P.rmScale);
    glUniform1f(pr.u.uRmZ, f.rmZ);
    glUniform1f(pr.u.uRmT, f.rmT);
    glUniform1i(pr.u.uRmSteps, r->rmSteps);
    // The maths places (as mac/render/renderer.mm): c, the slice, the hand's camera, the flight.
    glUniform4f(pr.u.uRmQ, f.rmQ[0], f.rmQ[1], f.rmQ[2], f.rmQ[3]);
    glUniform4f(pr.u.uRmCamP, f.rmCam[0], f.rmCam[1], f.rmCam[2], f.rmCam[3]);
    glUniform4f(pr.u.uRmCamF, f.rmCam[4], f.rmCam[5], f.rmCam[6], f.rmCam[7]);
    glUniform4f(pr.u.uRmCamU, f.rmCam[8], f.rmCam[9], f.rmCam[10], f.rmSurf ? 1.f : 0.f);
    glUniform1i(pr.u.uRmShape, P.rmShape); glUniform1i(pr.u.uRmLook, P.rmLook);
    glUniform1f(pr.u.uRmSlice, f.rmSlice); glUniform1f(pr.u.uRmDetail, P.rmDetail);
    glUniform1f(pr.u.uRmPad, P.foldInLoop ? 1.f : 0.f);   // the feedback pass folds into one wedge: draw only that
    glUniform1f(pr.u.uRmYaw, f.rmView[0]); glUniform1f(pr.u.uRmPitch, f.rmView[1]); glUniform1f(pr.u.uRmZoom, f.rmView[2]);
    draw();
  }

  // camera light (native: copied from the external texture, see kCamFS)
  const bool camOn = f.camOn && cameraCopy(r);

  // 1b. the sky places: every point one step, drawn once into the wedge textures
  const bool stars = starsFrame(r, f);

  // 2. feedback
  const Target& src = r->fb[r->cur];
  Target& dst = r->fb[1 - r->cur];
  {
    const Prog& pr = r->feedback;
    const Loc& u = pr.u;
    dst.vw = r->FW; dst.vh = r->FH;
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    invalidate();
    glViewport(0, 0, r->FW, r->FH);
    use(pr);
    setCommon(r, pr, f, r->FW, r->FH);
    bindT(0, u.uPrev, u.uPrevPic, src);
    tex(1, r->camT.tex); glUniform1i(u.uCam, 1);
    bindT(2, u.uRM, u.uRMPic, useRm ? r->rm : r->camT);
    // Only folds with F(F(p)) = F(p) can fold the echoes; the fractal fold compounds light.
    glUniform1i(u.uFoldLoop, P.foldInLoop && fold.mode != 2 ? 1 : 0);
    float cm[9]; colorMat(cm, f.decay, P.fbSat, f.hue);
    glUniformMatrix3fv(u.uColorMat, 1, GL_FALSE, cm);
    glUniform1f(u.uFbRot, f.fbRot);
    glUniform2f(u.uFbRotCS, (float)cos((double)f.fbRot), (float)sin((double)f.fbRot));
    glUniform1f(u.uFbZoom, f.fbZoom);
    glUniform1f(u.uFbSwirl, f.fbSwirl);
    glUniform1f(u.uFbWarp, f.fbWarp);
    glUniform1f(u.uFbWarpT, f.fbWarpT);
    glUniform1f(u.uSharpen, P.fbSharpen * std::min(f.n, 2.f));
    glUniform1f(u.uDt, f.dt);
    glUniform1f(u.uDream, f.dream);
    glUniform2f(u.uDreamC, f.dreamX, f.dreamY);
    const int ripN = f.rip ? std::min(f.ripN, 4) : 0;
    glUniform1i(u.uRipN, ripN);
    if (ripN > 0) glUniform4fv(u.uRip, ripN, f.rip);
    glUniform1i(u.uGen, P.gen);
    glUniform1f(u.uGenK, P.genMix * 1.85f);
    glUniform1f(u.uGenScale, P.genScale);
    glUniform1f(u.uGenT, f.genT);
    glUniform1f(u.uGenRadius, P.genRadius);
    glUniform1i(u.uCamOn, camOn ? 1 : 0);
    glUniform1f(u.uCamK, P.camMix);
    glUniform1f(u.uCamZoom, P.camZoom);
    glUniform1f(u.uCamAspect, r->camAspect);
    glUniform1f(u.uCamColorize, P.camColorize);
    draw();
  }

  // 3. ink, max-blended into the same target
  const int inkCount = f.ink ? std::min(f.inkCount, (int)RENDERER_MAX_SEG) : 0;
  if (inkCount > 0) {
    const Prog& pr = r->ink;
    const Loc& u = pr.u;
    use(pr);
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    glBindVertexArray(r->inkVao);
    glBindBuffer(GL_ARRAY_BUFFER, r->inkBuf);
    glBufferData(GL_ARRAY_BUFFER, RENDERER_MAX_SEG * 32, nullptr, GL_DYNAMIC_DRAW);   // orphan: no stall
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)inkCount * 32, f.ink);
    const int copies = std::max(1, f.inkCopies);
    glVertexAttribDivisor(0, (GLuint)copies);
    glVertexAttribDivisor(1, (GLuint)copies);
    glUniform2f(u.uRes, (float)r->FW, (float)r->FH);
    glUniform1i(u.uCopies, copies);
    glUniform1i(u.uInkMirror, fold.mirror ? 1 : 0);
    glUniform1f(u.uSym, fold.sym);
    glUniform1f(u.uSpin, fold.spin);
    glUniform2f(u.uCenter, fold.cx, fold.cy);
    glUniform1f(u.uGlow, P.inkGlow);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, inkCount * copies);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(r->vao);
  }
  // 3a. the hands' look, fed a little into the echoes at every mirror image
  const LookData* looks = r->looksOk ? f.looks : nullptr;
  if (looks) uploadLooks(r, *looks);
  if (looks && looks->feed > 0 && f.inkCopies > 1 && f.sys.handLookFeed)
    drawLooks(r, f, *looks, f.inkCopies, r->FW, r->FH, looks->feed, false);
  r->cur = 1 - r->cur;

  // 3b. mipmaps on the feedback texture, only when a fold will actually minify it
  const bool seamless = fold.mode == 0 || (fold.mode == 1 && fold.mirror);
  if (fold.mode >= 2 || fold.vz * 0.65f * r->FS > 1) mips(r->fb[r->cur]);

  // 4. scene (supersampled display fold + filigree + palette remap + optical veil)
  const bool veilOn = r->veilOn && r->veil.tex;
  {
    const Prog& pr = r->scene;
    const Loc& u = pr.u;
    r->sceneT.vw = r->W; r->sceneT.vh = r->H;
    glBindFramebuffer(GL_FRAMEBUFFER, r->sceneT.fbo);
    invalidate();
    glViewport(0, 0, r->W, r->H);
    use(pr);
    setCommon(r, pr, f, r->W, r->H);
    bindT(0, u.uFB, u.uFBPic, r->fb[r->cur]);
    bindT(1, u.uVeilTex, u.uVeilPic, veilOn ? r->veil : r->fb[r->cur]);
    glUniform1i(u.uTaps, seamless ? 1 : fold.mode == 1 ? std::min(2, r->maxTaps) : r->maxTaps);
    glUniform1f(u.uSheen, P.sheen);
    glUniform2f(u.uSheenL, (float)(0.6 * cos((double)f.sheenT)), (float)(0.6 * sin((double)f.sheenT)));
    glUniform1f(u.uFisheye, fold.fish);
    // The sky places, folded out of their wedge (SCENE_FS, skyLight); their light shifts
    // a little against the hand, so the sky sits behind the paint.
    glUniform1i(u.uSky, stars ? 1 : 0);
    glUniform1i(u.uSkyMirror, fold.mirror ? 1 : 0);
    glUniform4f(u.uSkyView, f.starView[1], f.starView[2], 1.f / std::max(f.starView[0], 1e-3f), std::max(2.f, fold.sym));
    glUniform4f(u.uSkyAt, f.starView[3], f.starView[4], -0.035f * f.starLook[0], -0.035f * f.starLook[1]);
    glUniform4f(u.uSkyBox, r->skyL.box[0], r->skyL.box[1], r->skyL.box[2], r->skyL.box[3]);
    const int nl = stars ? std::min(f.starLensN, 8) : 0;
    glUniform1i(u.uSkyLensN, nl);
    if (nl > 0) {
      float lens[32] = {};
      for (int i = 0; i < nl; i++) { lens[i * 4] = f.starLens[i * 3]; lens[i * 4 + 1] = f.starLens[i * 3 + 1]; lens[i * 4 + 2] = f.starLens[i * 3 + 2]; }
      glUniform4fv(u.uSkyLens, nl, lens);
    }
    tex(2, stars ? r->skySharp.tex : r->camT.tex); glUniform1i(u.uSkySharp, 2);
    tex(3, stars ? r->skySoft[r->skyCur].tex : r->camT.tex); glUniform1i(u.uSkySoft, 3);
    glUniform1f(u.uFbScale, r->FS);
    glUniform1f(u.uPalMap, P.paletteMap);
    glUniform1f(u.uPalShift, f.palPhase);
    const float va = r->veilAmt;
    glUniform1f(u.uVeil, veilOn ? va * va * (3 - 2 * va) : 0);
    draw();
  }
  // 4b. the hands' look on top of the picture
  if (looks) drawLooks(r, f, *looks, 1, r->W, r->H, LOOK_ON_TOP, true);

  // 5. bloom downsample chain; its alpha channel also averages scene brightness
  const int nb = r->nb;
  const bool doBloom = P.bloom > 0 && nb > 0;
  const bool guard = f.sys.flashGuard && nb > 0;
  if (doBloom || guard) {
    const Prog& pr = r->down;
    const Loc& u = pr.u;
    use(pr);
    glUniform1f(u.uThreshold, P.bloomThreshold);
    const Target* s = &r->sceneT;
    for (int i = 0; i < nb; i++) {
      Target& d = r->bloom[i];
      d.vw = std::max(1, s->vw >> 1); d.vh = std::max(1, s->vh >> 1);
      glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
      invalidate();
      glViewport(0, 0, d.vw, d.vh);
      bindT(0, u.uSrc, u.uSrcPic, *s);
      glUniform2f(u.uTexel, 1.f / s->w, 1.f / s->h);
      glUniform2f(u.uOut, (float)d.vw, (float)d.vh);
      glUniform1i(u.uPrefilter, i == 0 ? 1 : 0);
      draw();
      s = &d;
    }
  }

  // 5b. flash guard: mean brightness -> 1x1, eased adaptation
  if (guard) {
    const Prog* pr = &r->lum;
    glBindFramebuffer(GL_FRAMEBUFFER, r->lumCur.fbo);
    glViewport(0, 0, 1, 1);
    use(*pr);
    bindT(0, pr->u.uSrc, pr->u.uSrcPic, r->bloom[nb - 1]);
    draw();
    pr = &r->adapt;
    glBindFramebuffer(GL_FRAMEBUFFER, r->lumAd[1 - r->lumIdx].fbo);
    use(*pr);
    tex(0, r->lumAd[r->lumIdx].tex); glUniform1i(pr->u.uPrev, 0);
    tex(1, r->lumCur.tex); glUniform1i(pr->u.uCur, 1);
    glUniform1f(pr->u.uK, r->lumReset ? 1.f : 1.f - expf(-f.dt / 0.4f));
    draw();
    r->lumIdx = 1 - r->lumIdx;
    r->lumReset = false;
  } else {
    r->lumReset = true;
  }

  // 5c. bloom upsample (blended into what is there: never invalidated)
  if (doBloom) {
    const Prog& pr = r->up;
    const Loc& u = pr.u;
    use(pr);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = nb - 1; i > 0; i--) {
      const Target& sm = r->bloom[i];
      const Target& d = r->bloom[i - 1];
      glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
      glViewport(0, 0, d.vw, d.vh);
      bindT(0, u.uSrc, u.uSrcPic, sm);
      glUniform2f(u.uTexel, 0.5f / sm.w, 0.5f / sm.h);
      glUniform2f(u.uOut, (float)d.vw, (float)d.vh);
      draw();
    }
    glDisable(GL_BLEND);
  }

  FinalState& fs = r->last;
  fs.valid = true; fs.guard = guard; fs.doBloom = doBloom;
  fs.cap = f.sys.brightCap;
  fs.bloom = doBloom ? P.bloom * 0.22f : 0;
  fs.chroma = P.chroma; fs.grain = P.grain; fs.vignette = P.vignette; fs.exposure = P.exposure;
  fs.bloomMeanK = 1 + nb * fs.bloom;
  fs.frame = r->frameNo;
}

// 6. final, into the bound framebuffer at w x h.
static void finalPass(Renderer* r, int w, int h, Output out, float headroom) {
  const FinalState& fs = r->last;
  if (!fs.valid || !r->sceneT.tex) return;
  float peak = 1;
  if (out == OUT_HLG) {
    const float p = linToSrgb(std::min(std::max(headroom, 1.f), 4.f));
    peak = p > 1.0005f ? p : 1;
  }
  if (peak != r->peakFor) { r->peakFor = peak; r->G = peak > 1 ? hdrBound(peak) : 0; }
  const Prog& pr = r->fin;
  const Loc& u = pr.u;
  glBindVertexArray(r->vao);
  glDisable(GL_BLEND);
  glViewport(0, 0, w, h);
  use(pr);
  bindT(0, u.uScene, u.uScenePic, r->sceneT);
  bindT(1, u.uBloomTex, u.uBloomPic, fs.doBloom ? r->bloom[0] : r->sceneT);
  tex(2, r->lumCur.tex); glUniform1i(u.uLumCur, 2);
  tex(3, r->lumAd[r->lumIdx].tex); glUniform1i(u.uLumAdapt, 3);
  glUniform1i(u.uGuard, fs.guard ? 1 : 0);
  glUniform1f(u.uCap, fs.cap);
  glUniform2f(u.uOut, (float)w, (float)h);
  glUniform1f(u.uBloom, fs.bloom);
  glUniform1f(u.uChroma, fs.chroma);
  glUniform1f(u.uGrain, fs.grain);
  glUniform1f(u.uVignette, fs.vignette);
  glUniform1f(u.uExposure, fs.exposure);
  glUniform1ui(u.uFrame, fs.frame);
  glUniform1f(u.uPeak, peak);
  glUniform1f(u.uKs, r->Ks);
  glUniform1f(u.uG, r->G);
  glUniform1f(u.uBloomMeanK, fs.bloomMeanK);
  glUniform1i(u.uOutMode, out == OUT_HLG ? 1 : 0);
  draw();
}

// ---------------------------------------------------------------- PNG

static void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}
static void chunk(std::vector<uint8_t>& png, const char* type, const uint8_t* data, size_t n) {
  be32(png, (uint32_t)n);
  const size_t at = png.size();
  png.insert(png.end(), type, type + 4);
  if (n) png.insert(png.end(), data, data + n);
  be32(png, (uint32_t)crc32(0, png.data() + at, (uInt)(n + 4)));
}

std::vector<uint8_t> renderer_encode_png(const uint8_t* px, int w, int h) {
  if (!px || w <= 0 || h <= 0) return {};
  const size_t row = (size_t)w * 3 + 1;
  std::vector<uint8_t> raw(row * h);
  for (int y = 0; y < h; y++) {
    const uint8_t* s = px + (size_t)(h - 1 - y) * w * 4;
    uint8_t* d = raw.data() + row * y;
    d[0] = 1;   // filter: Sub
    for (int x = 0; x < w; x++)
      for (int c = 0; c < 3; c++) d[1 + x * 3 + c] = (uint8_t)(s[x * 4 + c] - (x ? s[(x - 1) * 4 + c] : 0));
  }
  uLongf zn = compressBound((uLong)raw.size());
  std::vector<uint8_t> z(zn);
  if (compress2(z.data(), &zn, raw.data(), (uLong)raw.size(), 3) != Z_OK) return {};
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});   // 8-bit RGB
  chunk(png, "IHDR", ihdr.data(), ihdr.size());
  chunk(png, "IDAT", z.data(), zn);
  chunk(png, "IEND", nullptr, 0);
  return png;
}

// ---------------------------------------------------------------- stills

// The web's still: its final pass on an 8-bit sRGB canvas, read into a buffer the GPU
// fills on its own time; pollReads hands it out once the fence has passed.
static void readStill(Renderer* r, int w, int h, RendererPixels done) {
  Target snap;   // kept only while one is wanted: an 8-bit target the size of the still
  if (w <= 0 || h <= 0 || !makeTarget(r, snap, w, h, false, GL_CLAMP_TO_EDGE, false)) {
    freeT(snap); glBindFramebuffer(GL_FRAMEBUFFER, 0); done({}, 0, 0); return;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, snap.fbo);
  finalPass(r, w, h, OUT_SDR, 1);
  Readback rb;
  rb.w = w; rb.h = h; rb.done = std::move(done);
  glGenBuffers(1, &rb.pbo);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo);
  glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, nullptr, GL_STREAM_READ);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  rb.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  freeT(snap);
  r->reads.push_back(std::move(rb));
}

static void pollReads(Renderer* r) {
  for (size_t i = 0; i < r->reads.size();) {
    const GLenum s = glClientWaitSync(r->reads[i].fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (s == GL_TIMEOUT_EXPIRED) { i++; continue; }
    Readback rb = std::move(r->reads[i]);
    r->reads.erase(r->reads.begin() + (long)i);
    std::vector<uint8_t> px;
    if (s != GL_WAIT_FAILED) {
      const size_t n = (size_t)rb.w * rb.h * 4;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo);
      if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)n, GL_MAP_READ_BIT)) {
        px.assign((const uint8_t*)p, (const uint8_t*)p + n);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    glDeleteSync(rb.fence);
    glDeleteBuffers(1, &rb.pbo);
    if (px.empty()) rb.done({}, 0, 0);
    else rb.done(std::move(px), rb.w, rb.h);
  }
}

// ---------------------------------------------------------------- export

// One step of the running export, in its own targets (swapped in for the step).
static void exportStep(Renderer* r) {
  ExportJob& j = *r->job;
  std::swap(setOf(r), j.set);
  glDisable(GL_DITHER);
  const Frame& f = j.step(1 / 60.f);
  encodeFrame(r, f);   // queued clears, undos and veils belong to the live picture: not run here
  const bool last = --j.left <= 0;
  if (last) readStill(r, j.w, j.h, std::move(j.done));
  std::swap(setOf(r), j.set);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (last) { freeSet(j.set); r->job.reset(); }
}

void renderer_export_hires(Renderer* r, int w, int h, int frames,
                           std::function<const Frame&(float dt)> step, RendererPixels done) {
  if (r->job || !r->sceneT.tex || frames < 1) { done({}, 0, 0); return; }
  auto job = std::make_unique<ExportJob>();
  job->w = w; job->h = h; job->left = frames;
  job->step = std::move(step);
  job->done = std::move(done);
  // The export's targets, at its size and the live echo scale, made with the live set
  // swapped out; it starts from the live picture: echoes, adaptation and sky.
  std::swap(setOf(r), job->set);
  RenderSet& live = job->set;
  bool ok = renderer_resize(r, w, h, live.FS);
  if (ok) {
    glBindVertexArray(r->vao);
    glDisable(GL_BLEND);
    copyInto(r, live.fb[live.cur], r->fb[0], r->FW, r->FH);
    r->cur = 0;
    if (live.lumCur.tex) { copyInto(r, live.lumAd[live.lumIdx], r->lumAd[0], 1, 1); r->lumIdx = 0; r->lumReset = live.lumReset; }
    if (live.starTex[0] && r->starsOk && starsAlloc(r)) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, live.starFbo[live.starCur]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->starFbo[0]);
      glBlitFramebuffer(0, 0, kStarsW, kStarsH, 0, 0, kStarsW, kStarsH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      r->starCur = 0;
    }
  }
  std::swap(setOf(r), job->set);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!ok) { freeSet(job->set); job->done({}, 0, 0); return; }
  r->job = std::move(job);
}

// ---------------------------------------------------------------- entry points

void renderer_render(Renderer* r, const Frame& f, int targetW, int targetH, Output out, float headroom) {
  if (!r->sceneT.tex) return;
  pollQueries(r);
  r->qOpen = r->caps.timerQuery && !r->qPending[r->qNext];
  if (r->qOpen) glBeginQuery(GL_TIME_ELAPSED_EXT, r->q[r->qNext]);
  glDisable(GL_DITHER);
  runOps(r);
  encodeFrame(r, f);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  const GLenum col = GL_COLOR;
  glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &col);
  finalPass(r, targetW, targetH, out, headroom);
  r->targetW = targetW; r->targetH = targetH;
}

void renderer_frame_end(Renderer* r) {
  if (!r->qOpen) return;
  glEndQuery(GL_TIME_ELAPSED_EXT);
  r->qPending[r->qNext] = true;
  r->qNext = (r->qNext + 1) % Renderer::NQ;
  r->qOpen = false;
}

void renderer_after_swap(Renderer* r) {
  if (r->snapDone && r->last.valid) {
    RendererPixels done = std::move(r->snapDone);
    r->snapDone = nullptr;
    readStill(r, r->targetW, r->targetH, std::move(done));
  }
  if (r->job) exportStep(r);
  if (!r->reads.empty()) pollReads(r);
}

bool renderer_busy(Renderer* r) { return r->job || !r->reads.empty() || (r->snapDone && r->last.valid); }

void renderer_draw_final(Renderer* r, int w, int h, Output out, float headroom) {
  finalPass(r, w, h, out, headroom);
}

void renderer_snapshot(Renderer* r, RendererPixels done) { r->snapDone = std::move(done); }

void renderer_set_camera(Renderer* r, uint32_t tex, const float* transform16, float aspect) {
  r->camTex = tex;
  r->camFresh = tex != 0;
  if (transform16) memcpy(r->camM, transform16, sizeof r->camM);
  if (aspect > 0) r->camAspect = aspect;
  if (!tex && r->camT.w > 1) { freeT(r->camT); blackCam(r); }   // back to the 1x1 black stand-in
}

double renderer_gpu_seconds(Renderer* r) { return r->gpuSec; }
void renderer_size(Renderer* r, int* w, int* h) { *w = r->W; *h = r->H; }
float renderer_echo_scale(Renderer* r) { return r->FS; }
int renderer_max_size(Renderer* r) { return r->caps.maxTexture; }

// ---------------------------------------------------------------- debug

#ifdef FX_DEBUG
std::vector<uint8_t> renderer_debug_snapshot(Renderer* r, int w, int h) {
  Target snap;
  if (!makeTarget(r, snap, w, h, false, GL_CLAMP_TO_EDGE, false)) { freeT(snap); return {}; }
  glBindFramebuffer(GL_FRAMEBUFFER, snap.fbo);
  finalPass(r, w, h, OUT_SDR, 1);
  std::vector<uint8_t> px((size_t)w * h * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  freeT(snap);
  return renderer_encode_png(px.data(), w, h);
}

static void meanRGB(const Target& t, float* out) {
  std::vector<float> px((size_t)t.vw * t.vh * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, t.vw, t.vh, GL_RGBA, GL_FLOAT, px.data());
  double s[4] = {};
  for (size_t i = 0; i < px.size(); i++) s[i & 3] += px[i];
  const double n = (double)t.vw * t.vh;
  for (int c = 0; c < 4; c++) out[c] = (float)(s[c] / n);
}

void renderer_debug_probe(Renderer* r, float out[16]) {
  if (!r->sceneT.tex) return;
  float v[4];
  meanRGB(r->lumCur, v); out[0] = v[0];
  meanRGB(r->lumAd[r->lumIdx], v); out[1] = v[0];
  meanRGB(r->sceneT, v); out[2] = v[0]; out[3] = v[1]; out[4] = v[2];
  meanRGB(r->fb[r->cur], v); out[5] = v[0]; out[6] = v[1]; out[7] = v[2];
  if (r->nb > 0) { meanRGB(r->bloom[0], v); out[8] = v[0]; out[9] = v[1]; out[10] = v[2]; }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
#endif
