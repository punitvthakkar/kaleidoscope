#pragma once
#include <cstdint>

// One YUV_420_888 image: three planes with their row and pixel strides and lengths.
struct YuvImage {
  const uint8_t *y, *u, *v;
  int64_t yLen, uLen, vLen;
  int yRow, yPix, cRow, cPix;   // chroma strides are the same for u and v
  int w, h;
};
// Writes ow x oh RGBA pixels (BT.601 video range), turned clockwise by rot degrees
// (0, 90, 180, 270) so the picture is upright. False if the sizes or planes do not fit.
bool yuv_to_rgba(const YuvImage&, int rot, uint8_t* out, int outStride, int ow, int oh);
