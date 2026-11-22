// COMMON (mirrors fold.js). Bodies verbatim from the web; GLSL `out` parameters are
// `thread T&`. Like the web's HEADER + COMMON + pass, the renderer pastes this text
// into each pass struct at its //@COMMON line; uniform names resolve through the
// macros in prelude.metal to the struct's C and U members.

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
mat2 spinM() { return mat2(uSpinCS.x, uSpinCS.y, -uSpinCS.y, uSpinCS.x); }
mat2 spinInv() { return mat2(uSpinCS.x, -uSpinCS.y, uSpinCS.y, uSpinCS.x); }
vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / max(dot(b, b), 1e-6); }
vec3 pal(float t) { return uPalA + uPalB * cos(TAU * (uPalC * t + uPalD)); }

float sabs(float x, float k) { return sqrt(x * x + k * k) - k; }
vec2 sabs2(vec2 v, float k) { return sqrt(v * v + k * k) - k; }

vec2 toUV(vec2 p) { return vec2(p.x * uRes.y / uRes.x, p.y) * 0.5 + 0.5; }
vec2 fragP() { return (gl_FragCoord.xy - 0.5 * uRes) / uRes.y * 2.0; }

uvec2 pcg2d(uvec2 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  return v;
}
vec2 hash2(vec2 p) { return vec2(pcg2d(uvec2(ivec2(p)))) * (2.0 / 4294967295.0) - 1.0; }
float gnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = dot(hash2(i), f), b = dot(hash2(i + vec2(1, 0)), f - vec2(1, 0));
  float c = dot(hash2(i + vec2(0, 1)), f - vec2(0, 1)), d = dot(hash2(i + vec2(1, 1)), f - vec2(1, 1));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < 4; i++) { s += a * gnoise(p); p = rot(0.7) * p * 2.03 + 1.7; a *= 0.5; }
  return s;
}

vec2 warpSpace(vec2 p) {
  if (uWarp.x <= 0.0) return p;
  float s = uWarp.y, t = uWarp.z;
  vec2 o = vec2(gnoise(p * s + vec2(t, 0.0)), gnoise(p * s + vec2(5.2, 1.3 - t)));
  vec2 w = (p + o) * s * 2.0;
  vec2 o2 = vec2(gnoise(w + vec2(1.7, 9.2 + t)), gnoise(w + vec2(8.3 - t, 2.8)));
  return p + uWarp.x * (o + 0.5 * o2);
}

vec2 foldKaleido(vec2 z, thread float& trap) {
  float seg = TAU / uSym, halfSeg = seg * 0.5;
  float a = mod(atan(z.y, z.x), seg);
  float r = length(z);
  if (uMirror) {
    trap = r * abs(a - halfSeg);
    a = halfSeg - sabs(a - halfSeg, 0.002);
  } else {
    trap = r * min(a, seg - a);
  }
  return r * vec2(cos(a), sin(a));
}

// Smooth C-infinity KIFS fold with fractional iteration blending and O(1) output normalization.
vec2 foldKifs(vec2 z, thread float& trap, thread float& scale) {
  float s = uKifs.x;
  float iters = uKifsIters;
  int maxI = int(ceil(iters));
  trap = 1e9;
  scale = 1.0;
  z = sabs2(z, 0.012);
  float d0 = z.x - z.y, sd0 = sabs(d0, 0.012);
  z = 0.5 * vec2(z.x + z.y + sd0, z.x + z.y - sd0);
  for (int i = 0; i < 10; i++) {
    if (i >= maxI) break;
    float w = clamp(iters - float(i), 0.0, 1.0);
    w = w * w * (3.0 - 2.0 * w);
    float k = 0.018 / (1.0 + 0.3 * float(i));
    vec2 fz = sabs2(z, k);
    float d = fz.x - fz.y, sd = sabs(d, k);
    fz = 0.5 * vec2(fz.x + fz.y + sd, fz.x + fz.y - sd);
    float stepTrap = min(fz.y, (fz.x - fz.y) * 0.7071) / scale;
    fz = fz * s - uKifsOff * (s - 1.0);
    vec2 cs = uKifsCS[i];
    fz = mat2(cs.x, cs.y, -cs.y, cs.x) * fz;
    z = mix(z, fz, w);
    scale *= mix(1.0, s, w);
    trap = min(trap, mix(trap, stepTrap, w));
  }
  float norm = 1.0 / max(length(uKifsOff) * 1.25, 0.55);
  scale *= norm;
  return z * norm;
}

// Anti-aliased Poincare disk fold with smooth horizon compression and fundamental domain normalization.
vec2 foldHyper(vec2 z, thread float& trap, thread float& scale, thread float& diskMask, thread float& rimGlow) {
  float r = length(z);
  diskMask = 1.0 - smoothstep(0.95, 1.04, r);
  rimGlow = exp(-pow((r - 0.965) / 0.028, 2.0));
  if (r > 1e-6) {
    float rh = min(0.985, r / sqrt(1.0 + pow(max(r - 0.82, 0.0) * 3.5, 2.0)));
    z *= rh / r;
  }
  vec2 a = uHyp.zw;
  vec2 denom = vec2(1.0, 0.0) - cmul(vec2(a.x, -a.y), z);
  z = cdiv(z - a, denom);
  float seg = TAU / max(3.0, uSym), halfSeg = seg * 0.5;
  vec2 C = vec2(uHyp.x, 0.0);
  float r2 = uHyp.y * uHyp.y;
  trap = 1e9;
  scale = 1.0 / max(1.0 - r * r * 0.88, 0.08);
  for (int i = 0; i < 28; i++) {
    float ang = mod(atan(z.y, z.x), seg);
    ang = halfSeg - sabs(ang - halfSeg, 0.003);
    z = length(z) * vec2(cos(ang), sin(ang));
    vec2 v = z - C;
    float vv = dot(v, v);
    trap = min(trap, abs(sqrt(vv) - uHyp.y));
    if (vv >= r2) break;
    float inv = r2 / max(vv, 1e-6);
    z = C + v * inv;
    scale *= clamp(inv, 0.5, 2.5);
  }
  float domRadius = max(uHyp.x - uHyp.y, 0.15);
  float norm = 0.72 / domRadius;
  scale *= norm;
  return z * norm;
}

// Seamless Droste log-spiral fold using a smooth reflection wave across scale period L.
vec2 foldDroste(vec2 z, thread float& trap, thread float& scale) {
  float L = uDroste.x;
  float r = max(length(z), 1e-5);
  vec2 w = vec2(log(r), atan(z.y, z.x));
  float T = L / TAU;
  w = vec2(w.x + w.y * T, w.y - w.x * T);
  w.x -= uDroste.y;
  float phase = w.x / L;
  float f = fract(phase);
  float sf = 0.5 - sabs(f - 0.5, 0.04);
  trap = min(f, 1.0 - f) * r * 0.5;
  w.x = (sf * 2.0 - 1.0) * L * 0.55;
  float er = exp(w.x) * 0.85;
  scale = clamp(er / r, 0.25, 8.0);
  return er * vec2(cos(w.y), sin(w.y));
}

vec2 foldTiles(vec2 z, thread float& trap, thread float& scale) {
  scale = uTile;
  z = z * uTile + 1.0;
  vec2 f = z - 2.0 * floor(z * 0.5) - 1.0;
  z = sabs2(f, 0.02);
  float d = z.x - z.y, sd = sabs(d, 0.02);
  z = 0.5 * vec2(z.x + z.y + sd, z.x + z.y - sd);
  trap = min(abs(d) * 0.7071, min(z.x, 1.0 - z.x)) / uTile;
  return z * 1.4 - 0.7;
}

// Black hole: a point-mass lens over the painted sky (index.html, foldLens).
vec2 foldLens(vec2 z, thread float& scale, thread float& mask, thread float& rim) {
  float E = uDroste.x, rs = 0.5 * E;
  float r = max(length(z), 1e-5);
  float k = E * E / (r * r);
  float b = r - k * r;
  float x = max(r - rs, 1e-3 * E);
  float wind = 0.9 * log(1.0 + 0.4 * E / x);
  scale = clamp(1.0 + k + abs(b) * 0.36 * E / (x * (x + 0.4 * E)), 1.0, 24.0);
  float gather = clamp(1.0 / max(abs(1.0 - k * k), 0.08) - 1.0, -0.6, 5.0);
  mask = smoothstep(rs, rs * 1.08, r) * (1.0 + 0.5 * gather);
  float e = (r - rs * 1.05) / E;
  rim = exp(-e * e * 1600.0) + 0.22 * exp(-e * e * 60.0) * step(rs, r);
  return rot(wind) * (z / r) * b;
}

// Wormhole, Indra's pearls, poles and zeros (index.html: foldWorm, foldPearls, foldPoles).
vec2 foldWorm(vec2 z, thread float& trap, thread float& scale, thread float& rim) {
  float a = uDroste.x, r = max(length(z), 1e-5), u = r / a;
  trap = abs(r - a) / uViewZoom;
  rim = exp(-(u - 1.0) * (u - 1.0) * 900.0);
  if (u >= 1.0) {
    float b = sqrt(r * r - a * a);
    scale = clamp(r / max(b, 1e-3), 1.0, 24.0);
    return z / r * b;
  }
  float w = sqrt(max(1.0 - u * u, 1e-4));
  scale = clamp(0.9 / (w * w * w), 1.0, 24.0);
  return -z / r * (0.9 * a * u / w);
}

vec2 tri(vec2 p) { return abs(mod(p - 1.0, 4.0) - 2.0) - 1.0; }

vec2 foldPearls(vec2 z, thread float& trap, thread float& scale) {
  float s = uTile, rs = sqrt(uTile);
  trap = 1e9;
  scale = 1.0;
  z += uHyp.zw;
  for (int i = 0; i < 4; i++) {
    z = tri(z);
    float r2 = max(dot(z, z), 1e-4);
    float k = s / r2;
    k = 0.5 * (k + 1.0 + sqrt((k - 1.0) * (k - 1.0) + 0.02));   // a soft max(k, 1): no crease where a pearl begins
    trap = min(trap, abs(sqrt(r2) - rs) / scale);
    z *= k;
    scale *= k;
  }
  scale = min(scale, 24.0);  // the finest pearls blend into a smooth glow instead of sparkling
  return tri(z) * 0.85;
}

vec2 foldPoles(vec2 z, thread float& trap, thread float& scale) {
  float t = uDroste.y;
  vec2 a = 0.62 * vec2(cos(t), sin(t)), b = 0.5 * vec2(cos(1.0 - 0.7 * t), sin(1.0 - 0.7 * t));
  vec2 z2 = cmul(z, z), a2 = cmul(a, a), b2 = cmul(b, b);
  vec2 n = z2 - a2, dn = z2 - b2;
  float dd = max(dot(dn, dn), 1e-6);
  vec2 w = cmul(z, cdiv(n, dn));
  scale = clamp(length(n) / sqrt(dd) + 2.0 * dot(z, z) * length(a2 - b2) / dd, 0.3, 24.0);
  vec2 f = tri(w * 0.9);
  trap = min(1.0 - abs(f.x), 1.0 - abs(f.y)) / max(scale, 1.0);
  return f * 0.9;
}

// Places a finger steers, modes 10 and up (index.html: foldPool .. foldJulia).
float tri(float x) { return abs(mod(x - 1.0, 4.0) - 2.0) - 1.0; }

vec2 foldPool(vec2 z, vec2 F, float t, float A, thread float& mask) {
  vec2 d = z - F;
  float r1 = length(d) + 1e-4, r0 = length(z) + 1e-4;
  float w1 = sin(r1 * 22.0 - t * 8.0) * exp(-r1 * 1.6);
  float w0 = 0.5 * sin(r0 * 16.0 - t * 6.0) * exp(-r0 * 1.2);
  mask = max(1.0 + 1.1 * A * (w1 + w0), 0.2);
  return z + (d / r1 * w1 + z / r0 * w0) * (0.07 * A);
}

vec2 foldWhirl(vec2 z, vec2 F, float t, float A, thread float& trap, thread float& scale) {
  vec2 d = z - F;
  float r2 = dot(d, d), g = exp(-r2 * 4.0);
  scale = 1.0 + 24.0 * A * r2 * g;
  return foldKaleido(F + rot((3.0 * A + 0.4 * sin(t)) * g) * d, trap);
}

vec2 foldPlanet(vec2 z, vec2 F, float t, thread float& scale, thread float& mask, thread float& rim) {
  float r = length(z) / 0.8, e = (r - 1.0) / 0.05;
  rim = exp(-e * e) * (r > 1.0 ? 1.0 : 0.35);
  mask = 0.0;
  scale = 1.0;
  if (r >= 1.0) return z;
  float nz = sqrt(1.0 - r * r);
  vec3 n = vec3(z / 0.8, nz);
  n.yz = rot(F.y * 1.4) * n.yz;
  n.xz = rot(F.x * 2.2 + t) * n.xz;
  mask = smoothstep(1.0, 0.97, r) * (0.3 + 0.7 * nz);
  scale = clamp(1.0 / max(nz, 0.12), 1.0, 24.0);
  return vec2(tri(atan2(n.x, n.z) / PI * 2.0) * 1.5, asin(clamp(n.y, -1.0, 1.0)) / PI * 1.8);
}

vec2 foldTunnel(vec2 z, vec2 F, float t, thread float& scale, thread float& mask) {
  vec2 d = z - 0.6 * F;
  float r = length(d) + 1e-4;
  mask = smoothstep(0.015, 0.3, r);
  scale = clamp(0.3 / (r * r), 1.0, 24.0);
  return vec2(tri(atan2(d.y, d.x) / PI * 4.0) * 1.3, tri(0.35 / r + t * 2.0) * 0.9);
}

vec2 foldJulia(vec2 z, vec2 F, thread float& trap, thread float& scale, thread float& mask) {
  vec2 c = F * 1.1;
  z *= 1.25;
  scale = 1.25;
  trap = 1e9;
  for (int i = 0; i < 4; i++) {
    scale = min(scale * 2.0 * max(length(z), 0.05), 1e6);
    z = clamp(cmul(z, z) + c, -64.0, 64.0);   // far outside the set is dark anyway: keep the numbers small
    trap = min(trap, abs(length(z) - 1.0) / max(scale, 1.0));
  }
  mask = 1.0 / (1.0 + 0.003 * scale);
  scale = clamp(scale, 0.3, 24.0);
  return tri(z * 0.6) * 0.9;
}

vec2 foldKaleidoSpace(vec2 p) {
  p = warpSpace(p);
  if (uMode == 0) return p;
  vec2 z = spinInv() * (p - uCenter) * uViewZoom;
  float tr;
  z = foldKaleido(z, tr);
  return uCenter + spinM() * z / uViewZoom;
}

vec2 foldSpaceEx(vec2 p, thread float& trap, thread float& scale, thread float& diskMask, thread float& rimGlow) {
  trap = 1e9;
  scale = uViewZoom;
  diskMask = 1.0;
  rimGlow = 0.0;
  p = warpSpace(p);
  if (uMode == 0) return p;
  vec2 z = spinInv() * (p - uCenter) * uViewZoom;
  if (uMode == 1) {
    z = foldKaleido(z, trap);
    return uCenter + spinM() * z / uViewZoom;
  }
  if (uMode == 6) {
    z = foldLens(z, scale, diskMask, rimGlow);
    return uCenter + spinM() * z / uViewZoom;
  }
  if (uMode == 7) {
    z = foldWorm(z, trap, scale, rimGlow);
    return uCenter + spinM() * z / uViewZoom;
  }
  if (uMode >= 10) {
    vec2 F = spinInv() * (uHyp.zw - uCenter) * uViewZoom;
    if (uMode == 10) { scale = 1.0; return uCenter + spinM() * foldPool(z, F, uDroste.y, uTile, diskMask) / uViewZoom; }
    if (uMode == 11) return uCenter + spinM() * foldWhirl(z, F, uDroste.y, uTile, trap, scale) / uViewZoom;
    if (uMode == 12) z = foldPlanet(z, F, uDroste.y, scale, diskMask, rimGlow);
    else if (uMode == 13) z = foldTunnel(z, F, uDroste.y, scale, diskMask);
    else z = foldJulia(z, F, trap, scale, diskMask);
    return uCenter + spinM() * z;
  }
  if (uMode == 2) z = foldKifs(z, trap, scale);
  else if (uMode == 3) z = foldHyper(z, trap, scale, diskMask, rimGlow);
  else if (uMode == 4) z = foldDroste(z, trap, scale);
  else if (uMode == 8) z = foldPearls(z, trap, scale);
  else if (uMode == 9) z = foldPoles(z, trap, scale);
  else z = foldTiles(z, trap, scale);
  scale = clamp(scale * uViewZoom, 0.2, 24.0);
  return uCenter + spinM() * z;
}

vec2 foldSpace(vec2 p) {
  float trap, scale, diskMask, rimGlow;
  return foldSpaceEx(p, trap, scale, diskMask, rimGlow);
}
