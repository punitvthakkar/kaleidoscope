// test_render helper: the real hand looks (mac/core/looks.cpp) built from a scripted open
// hand, so the renderer's look passes are drawn and timed with the batches the app makes.
// Compiled on its own (C++20, the core's standard) and linked into test_render.
#include "../core/engine_impl.h"

static Looks gLooks;
static Hands gHands;

// One or two open hands at camera coordinates (cx, cy) (0..1, y down), `size` the
// wrist-to-middle-knuckle span, swaying with t so trails and ribbons move.
const LookData* test_look_build(int look, double dt, double t, double aspect, float cx, float cy, float size, int nHands) {
  // An open right hand, wrist at the origin, y down, middle knuckle at (0, -1).
  static const float tmpl[21][2] = {
    {0, 0}, {-0.45f, -0.2f}, {-0.8f, -0.45f}, {-1.05f, -0.7f}, {-1.25f, -0.9f},
    {-0.35f, -0.95f}, {-0.45f, -1.45f}, {-0.5f, -1.75f}, {-0.55f, -2.0f},
    {0, -1}, {0, -1.55f}, {0, -1.9f}, {0, -2.15f},
    {0.32f, -0.95f}, {0.4f, -1.45f}, {0.45f, -1.75f}, {0.5f, -1.98f},
    {0.6f, -0.82f}, {0.75f, -1.2f}, {0.85f, -1.45f}, {0.93f, -1.65f}};
  for (int h = 0; h < 2; h++) {
    Hands::H& H = gHands.hs[h];
    H.seen = h < nHands;
    H.pinch = false;
    const float ox = cx + (h ? 0.28f : 0) + 0.06f * (float)std::sin(t * 1.3 + h), oy = cy + 0.03f * (float)std::sin(t * 0.9 + h);
    const float a = 0.25f * (float)std::sin(t * 0.7 + h), c = std::cos(a), s = std::sin(a);
    for (int j = 0; j < 21; j++) {
      const float x = tmpl[j][0] * (h ? -1 : 1), y = tmpl[j][1];
      H.lx[j] = ox + (c * x - s * y) * size / (float)aspect * 0.5f;   // camera x spans the width
      H.ly[j] = oy + (s * x + c * y) * size * 0.5f;
    }
  }
  return gLooks.build(look, dt, t, aspect, gHands);
}
