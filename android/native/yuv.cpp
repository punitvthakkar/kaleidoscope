// Camera image (YUV 4:2:0, as Camera2 delivers it) to upright RGBA pixels for the hand
// tracker. A plain function with no state: any thread.
#include "yuv.h"
#include <algorithm>

bool yuv_to_rgba(const YuvImage& s, int rot, uint8_t* out, int outStride, int ow, int oh) {
  const bool turned = rot == 90 || rot == 270;
  if (s.w <= 0 || s.h <= 0 || ow != (turned ? s.h : s.w) || oh != (turned ? s.w : s.h)) return false;
  // The last sample read from each plane must be inside it.
  if ((int64_t)(s.h - 1) * s.yRow + (int64_t)(s.w - 1) * s.yPix >= s.yLen) return false;
  const int64_t lastC = (int64_t)((s.h - 1) >> 1) * s.cRow + (int64_t)((s.w - 1) >> 1) * s.cPix;
  if (lastC >= s.uLen || lastC >= s.vLen) return false;

  // Output (ox, oy) reads source (x0 + ox * dxx + oy * dxy, y0 + ox * dyx + oy * dyy).
  int x0 = 0, y0 = 0, dxx = 1, dxy = 0, dyx = 0, dyy = 1;
  if (rot == 90) { x0 = 0; y0 = s.h - 1; dxx = 0; dxy = 1; dyx = -1; dyy = 0; }
  else if (rot == 180) { x0 = s.w - 1; y0 = s.h - 1; dxx = -1; dyy = -1; }
  else if (rot == 270) { x0 = s.w - 1; y0 = 0; dxx = 0; dxy = -1; dyx = 1; dyy = 0; }

  for (int oy = 0; oy < oh; oy++) {
    uint8_t* o = out + (size_t)oy * outStride;
    int sx = x0 + oy * dxy, sy = y0 + oy * dyy;
    for (int ox = 0; ox < ow; ox++, sx += dxx, sy += dyx, o += 4) {
      const int c = (sy >> 1) * s.cRow + (sx >> 1) * s.cPix;
      const int y = 1192 * std::max(s.y[sy * s.yRow + sx * s.yPix] - 16, 0);
      const int u = s.u[c] - 128, v = s.v[c] - 128;
      o[0] = (uint8_t)(std::clamp(y + 1634 * v, 0, 262143) >> 10);
      o[1] = (uint8_t)(std::clamp(y - 833 * v - 400 * u, 0, 262143) >> 10);
      o[2] = (uint8_t)(std::clamp(y + 2066 * u, 0, 262143) >> 10);
      o[3] = 255;
    }
  }
  return true;
}
