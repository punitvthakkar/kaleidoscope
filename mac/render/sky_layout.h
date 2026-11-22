// The sky places' wedge textures (mac/render/shaders/stars.metal): which part of the sky
// they cover and at how many texels. Shared by the Mac and Android renderers.
//
// A kaleidoscope only needs one wedge drawn: the points are drawn once into textures
// covering the wedge (angles 0 to pi/sym when mirrored, 0 to 2pi/sym when not, in the
// sky's own space), and the scene pass folds every screen pixel into it. The textures
// reach out to the farthest corner of the screen and hold one texel per screen pixel (the
// sharp one; the soft one half that), so the stars are as crisp as if drawn on screen.
// Their size depends only on the render size and the mirrors, so zooming never
// reallocates them: zoomed in, the same texels cover less sky, more finely.
#pragma once
#include <algorithm>
#include <cmath>

// The wedge out to radius 1 as a box, with a margin for sprites spilling over a wall.
inline void sky_unit_box(float sym, bool mirror, float out[4]) {
  const double W = (mirror ? M_PI : 2 * M_PI) / std::max(2.f, sym);
  double x0 = 0, y0 = 0, x1 = 1, y1 = 0;
  auto add = [&](double a) {
    const double x = cos(a), y = sin(a);
    x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
  };
  add(W);
  for (double a = M_PI / 2; a < W; a += M_PI / 2) add(a);
  const double m = 0.03;
  out[0] = (float)(x0 - m); out[1] = (float)(y0 - m); out[2] = (float)(x1 + m); out[3] = (float)(y1 + m);
}

// How far the screen reaches, in p-space (whose y runs -1..1): the corner, with a little
// room for the fisheye and the hand's parallax.
inline double sky_corner(int w, int h) { return std::hypot((double)w / std::max(1, h), 1.0) * 1.05; }

// Texture size for a w x h render: the wedge out to the corner at `scale` texels per
// screen pixel, at most maxDim on a side.
inline void sky_tex_size(float sym, bool mirror, int w, int h, float scale, int maxDim, int& tw, int& th) {
  float b[4]; sky_unit_box(sym, mirror, b);
  const double cornerPx = sky_corner(w, h) * 0.5 * h;
  const double sx = (b[2] - b[0]) * cornerPx * scale, sy = (b[3] - b[1]) * cornerPx * scale;
  const double f = std::min(1.0, maxDim / std::max(sx, sy));
  tw = std::max(16, (int)std::ceil(sx * f)); th = std::max(16, (int)std::ceil(sy * f));
}

// This frame's extent: the sky seen out to the screen's corner at this zoom, but no
// farther than any point lives (2.6), and its texel density, per unit of sky and per
// screen pixel (sizes are given in screen pixels).
struct SkyLayout { float box[4]; float den, k; };
inline SkyLayout sky_layout(float sym, bool mirror, int w, int h, float zoom, int tw) {
  float b[4]; sky_unit_box(sym, mirror, b);
  const double R = std::min(2.6, sky_corner(w, h) / std::max(zoom, 1e-3f));
  SkyLayout L;
  for (int i = 0; i < 4; i++) L.box[i] = (float)(b[i] * R);
  L.den = (float)(tw / (L.box[2] - L.box[0]));
  L.k = (float)(L.den / (0.5 * h * std::max(zoom, 1e-3f)));
  return L;
}

// How many of the n points each wedge texture draws (stars.metal: the same shares): the
// first so many, so the GPU runs no vertex for a point it does not draw.
inline int star_share(int n, int kind, bool sharp, float echoKeep) {
  const double share = sharp ? (kind == 1 ? 0.1 : kind == 4 ? 0.2 : 0.3) : (kind >= 4 ? 0.3 : 1.0) * echoKeep;
  return std::max(1, std::min(n, (int)std::ceil(n * share)));
}
