#include "rmflight.h"
#include <algorithm>
#include <cmath>

static inline double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

// ---- the maths shapes on the CPU: the same distance estimates as raymarch.metal, used to
// fly the camera over their surface without ever entering them.
struct RmShape { int shape; double c[4]; double slice, detail; };
bool rm_surface(int shape) { return shape == RMSHAPE_sinequat || shape == RMSHAPE_quatjulia || shape == RMSHAPE_bulb || shape == RMSHAPE_box; }
static double rm_radius(int shape) { return shape == RMSHAPE_sinequat ? 3.2 : shape == RMSHAPE_box ? 4.5 : 1.7; }
static void q_mul(const double a[4], const double b[4], double o[4]) {
  const double x = a[0] * b[0] - (a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
  const double y = a[0] * b[1] + b[0] * a[1] + (a[2] * b[3] - a[3] * b[2]);
  const double z = a[0] * b[2] + b[0] * a[2] + (a[3] * b[1] - a[1] * b[3]);
  const double w = a[0] * b[3] + b[0] * a[3] + (a[1] * b[2] - a[2] * b[1]);
  o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
static double rm_de(const RmShape& S, const double p[3]) {
  switch (S.shape) {
    case RMSHAPE_sinequat: {
      double q[4] = {p[0], p[1], p[2], S.slice}, dr = 1, r2 = 0;
      const double cl = std::sqrt(S.c[0] * S.c[0] + S.c[1] * S.c[1] + S.c[2] * S.c[2] + S.c[3] * S.c[3]);
      for (int i = 0; i < 9; i++) {
        const double l = std::sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3]), ca = std::cos(q[0]), sa = std::sin(q[0]), ch = std::cosh(l), sh = std::sinh(l);
        dr *= cl * std::sqrt(ca * ca * ch * ch + sa * sa * sh * sh);
        const double k = l > 1e-6 ? ca * sh / l : 0, s[4] = {sa * ch, k * q[1], k * q[2], k * q[3]};
        q_mul(S.c, s, q);
        r2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        if (r2 > 200) break;
      }
      const double r = std::sqrt(r2);
      return 0.15 * std::log(std::max(r, 1.0001)) * r / std::max(dr, 1e-6);
    }
    case RMSHAPE_quatjulia: {
      double z[4] = {p[0], p[1], p[2], S.slice}, m2 = z[0] * z[0] + z[1] * z[1] + z[2] * z[2] + z[3] * z[3], dz2 = 1;
      for (int i = 0; i < 11; i++) {
        dz2 *= 4 * m2;
        const double x = z[0] * z[0] - (z[1] * z[1] + z[2] * z[2] + z[3] * z[3]);
        z[1] = 2 * z[0] * z[1] + S.c[1]; z[2] = 2 * z[0] * z[2] + S.c[2]; z[3] = 2 * z[0] * z[3] + S.c[3]; z[0] = x + S.c[0];
        m2 = z[0] * z[0] + z[1] * z[1] + z[2] * z[2] + z[3] * z[3];
        if (m2 > 16) break;
      }
      return 0.25 * std::sqrt(m2 / dz2) * std::log(std::max(m2, 1.0001));
    }
    case RMSHAPE_bulb: {
      const double n = 4 + 6 * S.detail + S.slice * 1.5;
      double z[3] = {p[0], p[1], p[2]}, dr = 1, r = 0;
      for (int i = 0; i < 8; i++) {
        r = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
        if (r > 2) break;
        double th = std::acos(clampd(z[2] / std::max(r, 1e-6), -1, 1)) + S.c[1] * 0.4, phi = std::atan2(z[1], z[0]) + S.c[2] * 0.4;
        dr = std::pow(r, n - 1) * n * dr + 1;
        const double zr = std::pow(r, n);
        th *= n; phi *= n;
        z[0] = zr * std::sin(th) * std::cos(phi) + p[0]; z[1] = zr * std::sin(th) * std::sin(phi) + p[1]; z[2] = zr * std::cos(th) + p[2];
      }
      return 0.5 * std::log(std::max(r, 1.0001)) * r / dr;
    }
    case RMSHAPE_box: {
      const double s = -2.6 + 1.2 * S.detail - S.slice * 0.4;
      double z[3] = {p[0], p[1], p[2]}, dr = 1;
      for (int i = 0; i < 11; i++) {
        for (double& v : z) v = clampd(v, -1, 1) * 2 - v;
        const double r2 = z[0] * z[0] + z[1] * z[1] + z[2] * z[2];
        if (r2 < 0.25) { for (double& v : z) v *= 4; dr *= 4; }
        else if (r2 < 1) { for (double& v : z) v /= r2; dr /= r2; }
        for (int j = 0; j < 3; j++) z[j] = s * z[j] + p[j] + S.c[j + 1] * 0.2;
        dr = dr * std::fabs(s) + 1;
      }
      return std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]) / std::fabs(dr);
    }
  }
  return 1;
}
// How far from the centre the surface lies along dir: march in from outside.
static double rm_probe(const RmShape& S, const double dir[3], double R) {
  double r = R * 1.6;
  for (int i = 0; i < 160 && r > 0; i++) {
    const double pos[3] = {dir[0] * r, dir[1] * r, dir[2] * r};
    const double d = rm_de(S, pos);
    if (d < 1e-4 * R) break;
    r -= d * 0.9;
  }
  return std::max(r, 0.0);
}

void rm_frame(int shape, double rmT, double morph, double dl, double slice0,
              double yaw, double pitch, double zoom, double yawV, double dt, RmCam& cam, Frame& f) {
  // c and the slice: a slow, smooth closed loop (morph sets its pace), never driven by a hand.
  const double ph = rmT * morph;
  double q[4];
  if (shape == RMSHAPE_quatjulia) {
    // A small loop around the "rabbit", near the edge of the Mandelbrot set, where the
    // Julia sets are connected but full of lace; j and k lean it gently into 3D.
    q[0] = -0.16 + 0.1 * std::cos(ph) - 0.1 * dl; q[1] = 0.68 + 0.08 * std::sin(ph * 0.8); q[2] = 0.12 * std::sin(ph * 0.6); q[3] = 0.06 * std::cos(ph * 0.9);
  } else if (shape == RMSHAPE_sinequat) {
    // c · sin q is chaotic: c moves only a little, so the surface flows rather than reshuffles.
    q[0] = 0.95 + 0.3 * dl + 0.03 * std::sin(ph * 0.7); q[1] = 0.25 + 0.06 * std::sin(ph); q[2] = 0.06 * std::cos(ph * 1.3); q[3] = 0.04 * std::sin(ph * 1.1);
  } else {
    q[0] = 0.95 + 0.3 * dl + 0.08 * std::sin(ph * 0.7); q[1] = 0.25 * std::sin(ph); q[2] = 0.2 * std::cos(ph * 1.3); q[3] = 0.12 * std::sin(ph * 1.1);
  }
  const double slice = slice0 + 0.15 * std::sin(ph * 0.5);
  for (int i = 0; i < 4; i++) f.rmQ[i] = (float)q[i];
  f.rmSlice = (float)slice;
  f.rmSurf = rm_surface(shape);
  if (!f.rmSurf) return;

  const RmShape S{shape, {q[0], q[1], q[2], q[3]}, slice, dl};
  const double cp = std::cos(pitch), sp = std::sin(pitch), cyw = std::cos(yaw), syw = std::sin(yaw);
  const double dir[3] = {cp * syw, sp, cp * cyw};
  const double R = rm_radius(shape);
  const double rs = rm_probe(S, dir, R);                // the surface straight below
  // The flight keeps a steady height over the surface's average level rather than bobbing over
  // every bump below; a bump that rises close is cleared smoothly, not with a jump.
  if (cam.rs <= 0) cam.rs = rs;
  cam.rs += (rs - cam.rs) * (1 - std::exp(-dt * 0.6));
  const double h = R * 0.1 / zoom;                      // height above the surface
  if (cam.rc <= 0) cam.rc = cam.rs + h;
  cam.rc += (cam.rs + h - cam.rc) * (1 - std::exp(-dt * 1.5));
  double need = cam.rc;
  for (int i = 0; i < 6; i++) {
    const double pos[3] = {dir[0] * need, dir[1] * need, dir[2] * need};
    const double d = rm_de(S, pos);
    if (d >= h * 0.35) break;
    need += h * 0.35 - d;
  }
  if (need > cam.rc) cam.rc += (need - cam.rc) * (1 - std::exp(-dt * 10.0));
  cam.gs = h / std::max(cam.rs, 1e-3);
  // Look down onto the surface, tilted a little toward the flight, so the surface fills the
  // whole picture (the kaleidoscope folds any wedge of it); when the flight reverses, the
  // tilt swings round gently.
  if (std::fabs(yawV) > 0.01) cam.head += ((yawV < 0 ? -1 : 1) - cam.head) * (1 - std::exp(-dt * 0.9));
  const double tan[3] = {cyw, 0, -syw};
  double fw[3];
  for (int i = 0; i < 3; i++) fw[i] = tan[i] * 0.4 * cam.head - dir[i] * 0.92;
  const double fl = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
  for (double& v : fw) v /= fl;
  for (int i = 0; i < 3; i++) { f.rmCam[i] = (float)(dir[i] * cam.rc); f.rmCam[4 + i] = (float)fw[i]; f.rmCam[8 + i] = (float)dir[i]; }
  f.rmCam[3] = (float)std::max(cam.rc - rs, 1e-5);
  f.rmCam[7] = (float)R;
}
