// Debug chambers: see debug.h. Each chamber runs as one render-thread task, so the
// host's own frames continue between chambers but never inside one. The sky places are
// run with a scripted finger, so their pictures show a hand's effect. Dev builds only
// (build.sh passes -DFX_DEBUG to them).
#ifdef FX_DEBUG
#include "debug.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../host.h"
#include "../renderer.h"
#include "../../../mac/core/engine.h"
#include "../../../mac/core/engine_impl.h"

#include "../../../mac/render/mini_engine.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fx", __VA_ARGS__)

namespace {

const int kFrames = 300;    // as mac/render/test_render.mm: 5 s at 1/60 s
const int kTimed = 60;      // GPU time: mean over the last 60 frames

struct Chamber { std::string slug; std::vector<std::pair<std::string, std::string>> kv; };

std::vector<Chamber> loadChambers(const std::string& path) {
  std::vector<Chamber> out;
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return out;
  char slug[64], key[64], val[64];
  while (fscanf(f, "%63s %63s %63s", slug, key, val) == 3) {
    if (out.empty() || out.back().slug != slug) out.push_back({slug, {}});
    out.back().kv.emplace_back(key, val);
  }
  fclose(f);
  return out;
}

void runRef(std::shared_ptr<std::vector<Chamber>> list, size_t i, int waits);

bool save(const char* sub, const std::string& slug, const std::vector<uint8_t>& png) {
  if (png.empty()) return false;
  const std::string dir = std::string(host_files_dir()) + "/" + sub;
  mkdir(dir.c_str(), 0700);
  FILE* f = fopen((dir + "/" + slug + ".png").c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  fclose(f);
  return ok;
}

// Reference mode: mac/render/test_render.mm step 2 exactly (fresh renderer per chamber,
// 1120x704, echo 1, detail 4/76, 299 frames then one with the snapshot, 1/60 s, the
// test's mini engine, OUT_SDR, headroom 1), to <files>/chambers_ref/<slug>.png.
void runRef(std::shared_ptr<std::vector<Chamber>> list, size_t i, int waits) {
  if (!host_gl().renderer) {   // no context yet: try again shortly (render thread, debug only)
    if (waits > 200) { LOGI("chambers: no renderer after 10 s"); return; }
    usleep(50000);
    host_post_render([list, i, waits] { runRef(list, i, waits + 1); });
    return;
  }
  if (i >= list->size()) {
    LOGI("chambers_ref done (%zu)", list->size());
    return;
  }
  const Chamber& c = (*list)[i];
  const int W = 1120, H = 704;
  Params P;
  for (const auto& [k, v] : c.kv) {
    char* end = nullptr;
    const double num = strtod(v.c_str(), &end);
    params_set(P, k.c_str(), *end ? 0 : num, v.c_str());
  }
  char err[4096] = "";
  Renderer* r = renderer_create(host_caps(), (std::string(host_files_dir()) + "/shaders").c_str(), err, sizeof err);
  if (!r) { LOGI("chamber_ref %s FAILED: %s", c.slug.c_str(), err); return; }
  bool ok = renderer_resize(r, W, H, 1);
  renderer_set_detail(r, 4, 76);
  auto me = std::make_unique<MiniEngine>();
  me->init(P);
  auto png = std::make_shared<std::vector<uint8_t>>();
  for (int k = 0; ok && k < 300; k++) {
    const Frame& f = me->step(r, 1 / 60.0);
    renderer_render(r, f, W, H, OUT_SDR, 1);
    renderer_frame_end(r);
    if (k == 299) *png = renderer_debug_snapshot(r, W, H);
  }
  float pr[16] = {};
  if (ok) renderer_debug_probe(r, pr);
  LOGI("chamber_ref %s %s lumCur=%.4f lumAd=%.4f scene=%.4f,%.4f,%.4f echo=%.4f,%.4f,%.4f bloom0=%.4f,%.4f,%.4f%s",
       c.slug.c_str(), ok ? "ok" : "FAILED", pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7], pr[8], pr[9], pr[10],
       save("chambers_ref", c.slug, *png) ? "" : " no snapshot");
  renderer_destroy(r);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  host_post_render([list, i] { runRef(list, i + 1, 0); });
}

void runChamber(std::shared_ptr<std::vector<Chamber>> list, size_t i, int waits) {
  Renderer* r = host_gl().renderer;
  int ow = 0, oh = 0;
  if (r) renderer_size(r, &ow, &oh);
  if (!r || ow == 0) {   // no surface yet: try again shortly (render thread, debug only)
    if (waits > 200) { LOGI("chambers: no renderer after 10 s"); return; }
    usleep(50000);
    host_post_render([list, i, waits] { runChamber(list, i, waits + 1); });
    return;
  }
  if (i >= list->size()) {
    LOGI("chambers done (%zu)", list->size());
    host_post_render([list] { runRef(list, 0, 0); });
    return;
  }
  const Chamber& c = (*list)[i];
  Engine* e = host_engine();
  // The app's own drawable size and echo scale (what it renders at most).
  const EngineStats st0 = engine_stats(e);
  const int w = st0.drawW, h = st0.drawH;
  const float ofs = renderer_echo_scale(r);

  for (const auto& [k, v] : c.kv) {
    char* end = nullptr;
    const double num = strtod(v.c_str(), &end);
    engine_set_param(e, k.c_str(), *end ? 0 : num, v.c_str());
  }
  engine_clear_pens(e);
  renderer_clear(r);
  // The sky places get a finger, as the Mac test gives them: down from frame 60, sweeping
  // a wide loop, lifted 30 frames before the end (held long: not a tap).
  bool sky = false;
  for (const auto& [k, v] : c.kv) if (k == "stars" && atof(v.c_str()) > 0) sky = true;
  const float d = std::max(1.f, host_caps().density), vw = w / d, vh = h / d;
  // Drawable resolution, the engine's echo scale, the highest detail (the Mac test's).
  bool ok = renderer_resize(r, w, h, st0.echoScale);
  renderer_set_detail(r, 4, 76);
  double sum = 0; int n = 0;
  auto png = std::make_shared<std::vector<uint8_t>>();
  for (int k = 0; ok && k < kFrames; k++) {
    if (sky && k >= 60 && k <= kFrames - 30) {
      const double t = k / 60.0, s = vh * 0.5;          // p-space y spans the height: half of it is one unit
      const float px = (float)(vw * 0.5 + s * 0.8 * sin(t * 0.9 + 1.0)), py = (float)(vh * 0.5 - s * 0.45 * sin(t * 1.5));
      engine_pointer(e, 7, k == 60 ? PTR_DOWN : k == kFrames - 30 ? PTR_UP : PTR_MOVE, px, py, t, false);   // seconds, as the host sends them
    }
    const Frame& f = engine_step(e, 1 / 60.0, 0, 0, 0);
    renderer_render(r, f, w, h, OUT_SDR, 1);
    renderer_frame_end(r);
    if (k == kFrames - 1) *png = renderer_debug_snapshot(r, w, h);
    if (k >= kFrames - kTimed) {
      glFinish();   // the frame's timer query is then complete
      sum += renderer_gpu_seconds(r); n++;
    }
  }
  const bool saved = save("chambers", c.slug, *png);
  float pr[16] = {};
  if (ok) renderer_debug_probe(r, pr);
  LOGI("chamber_probe %s lumCur=%.4f lumAd=%.4f scene=%.4f,%.4f,%.4f echo=%.4f,%.4f,%.4f", c.slug.c_str(), pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7]);
  LOGI("chamber %s gpu_ms=%.2f size=%dx%d echo=%.2f%s", c.slug.c_str(), n ? sum / n * 1e3 : 0.0, w, h, st0.echoScale,
       !ok ? " FAILED: out of GPU memory" : saved ? "" : " FAILED: no snapshot");

  // Back to what the host had; the engine's ladder sets detail again.
  const EngineStats st = engine_stats(e);
  renderer_resize(r, ow, oh, ofs);
  renderer_set_detail(r, st.ssTaps, st.rmSteps);
  host_post_render([list, i] { runChamber(list, i + 1, 0); });
}

// Two open hands in camera coordinates (0..1, y down), drifting on a slow loop so the
// trails, ribbons and curtains have motion. Hand 1 is hand 0 mirrored.
void synthHands(Hands& hs, double t) {
  // Wrist, then per finger its base direction (degrees from up) and four joints.
  const double ang[5] = {-62, -24, -4, 14, 32}, len[5] = {0.055, 0.075, 0.08, 0.075, 0.06};
  for (int i = 0; i < 2; i++) {
    auto& h = hs.hs[i];
    h.seen = true; h.pinch = false;
    const double sx = i == 0 ? 1 : -1;
    const double wx = 0.5 + sx * (0.2 + 0.06 * sin(t * 0.9)), wy = 0.72 + 0.05 * cos(t * 1.3);
    h.lx[0] = (float)wx; h.ly[0] = (float)wy;
    for (int f = 0; f < 5; f++) {
      const double a = (ang[f] + 6 * sin(t * 2 + f)) * 3.14159265 / 180 * sx;
      const double dx = sin(a), dy = -cos(a);
      // The base knuckle sits a palm's length from the wrist, the thumb's closer.
      const double base = f == 0 ? 0.05 : 0.11;
      for (int j = 0; j < 4; j++) {
        const double d = (f == 0 ? 0.035 : base) + len[f] * j;
        h.lx[1 + f * 4 + j] = (float)(wx + dx * d * 0.62);   // camera x spans the wider side
        h.ly[1 + f * 4 + j] = (float)(wy + dy * d);
      }
    }
  }
}

void runLooks(std::shared_ptr<std::vector<int>> list, size_t i, int waits) {
  Renderer* r = host_gl().renderer;
  int w = 0, h = 0;
  if (r) renderer_size(r, &w, &h);
  if (!r || w == 0) {
    if (waits > 200) { LOGI("looks: no renderer after 10 s"); return; }
    usleep(50000);
    host_post_render([list, i, waits] { runLooks(list, i, waits + 1); });
    return;
  }
  if (i >= list->size()) { LOGI("looks done (%zu)", list->size()); return; }
  const int look = (*list)[i];
  Engine* e = host_engine();
  auto looks = std::make_unique<Looks>();
  auto hands = std::make_unique<Hands>();
  const double aspect = (double)w / h;
  // Without, then with: the same chamber, 120 frames each, GPU time over the last 60.
  double base = 0, with = 0; int nb = 0, nw = 0;
  const LookData* last = nullptr; int copies = 1;
  auto png = std::make_shared<std::vector<uint8_t>>();
  for (int k = 0; k < 240; k++) {
    const bool on = k >= 120;
    const Frame& f0 = engine_step(e, 1 / 60.0, 0, 0, 0);
    Frame f = f0;
    if (on) {
      synthHands(*hands, k / 60.0);
      f.looks = last = looks->build(look, 1 / 60.0, k / 60.0, aspect, *hands);
    } else f.looks = nullptr;
    copies = f.inkCopies;
    renderer_render(r, f, w, h, OUT_SDR, 1);
    renderer_frame_end(r);
    if (k == 239) *png = renderer_debug_snapshot(r, w, h);
    if (k % 120 >= 60) {
      glFinish();
      (on ? with : base) += renderer_gpu_seconds(r); (on ? nw : nb)++;
    }
  }
  const GLenum err = glGetError();
  const bool saved = save("looks", "look" + std::to_string(look), *png);
  LOGI("looks %d gpu_ms=%.3f base_ms=%.3f size=%dx%d seg=%d vert=%d strips=%d aura=%d feed=%.4f copies=%d gl=0x%x%s", look,
       nw ? with / nw * 1e3 : 0.0, nb ? base / nb * 1e3 : 0.0, w, h, last ? last->nSeg : -1, last ? last->nVert : -1,
       last ? last->nStrip : -1, last ? last->auraN : -1, last ? last->feed : 0.f, copies, err, saved ? "" : " no snapshot");
  host_post_render([list, i] { runLooks(list, i + 1, 0); });
}

}  // namespace

void render_debug_looks(const char* which) {
  auto list = std::make_shared<std::vector<int>>();
  const std::string s = which ? which : "";
  if (s.empty() || s == "true") *list = {1, 2, 3, 4, 5};
  else for (size_t p = 0; p < s.size();) { list->push_back(atoi(s.c_str() + p)); size_t q = s.find(',', p); p = q == std::string::npos ? s.size() : q + 1; }
  host_post_render([list] { runLooks(list, 0, 0); });
}

void render_debug_chambers(const char* only) {
  auto list = std::make_shared<std::vector<Chamber>>(loadChambers(std::string(host_files_dir()) + "/chambers.txt"));
  const std::string want = only ? only : "";
  if (!want.empty() && want != "true") {
    std::vector<Chamber> keep;
    for (auto& c : *list) if (("," + want + ",").find("," + c.slug + ",") != std::string::npos) keep.push_back(std::move(c));
    *list = std::move(keep);
  }
  if (list->empty()) { LOGI("chambers: none in %s/chambers.txt", host_files_dir()); return; }
  host_post_render([list] { runChamber(list, 0, 0); });
}
#endif  // FX_DEBUG
