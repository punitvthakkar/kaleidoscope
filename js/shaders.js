// GLSL ES 3.0 sources. COMMON mirrors fold.js line for line.

const SHADERS = (() => {
  const HEADER = `#version 300 es
precision highp float;
precision highp int;
#define PI 3.14159265359
#define TAU 6.28318530718
`;

  const FULLSCREEN_VS = `#version 300 es
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;

  const COMMON = `
uniform vec2 uRes;
uniform int uMode;
uniform float uSym;
uniform bool uMirror;
uniform float uSpin;
uniform vec2 uCenter;
uniform float uViewZoom;
uniform vec4 uWarp;       // amount, scale, time
uniform int uKifsIters;
uniform vec2 uKifs;       // scale, angle
uniform vec2 uKifsOff;
uniform vec4 uHyp;        // circle center, radius, mobius a.xy
uniform vec2 uDroste;     // log ratio, phase
uniform float uTile;
uniform vec3 uPalA, uPalB, uPalC, uPalD;

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / dot(b, b); }
vec3 pal(float t) { return uPalA + uPalB * cos(TAU * (uPalC * t + uPalD)); }

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

vec2 foldKaleido(vec2 z) {
  float seg = TAU / uSym;
  float a = atan(z.y, z.x);
  a = mod(a, seg);
  if (uMirror && a > seg * 0.5) a = seg - a;
  return length(z) * vec2(cos(a), sin(a));
}

vec2 foldKifs(vec2 z) {
  float s = uKifs.x;
  mat2 R = rot(uKifs.y);
  for (int i = 0; i < 10; i++) {
    if (i >= uKifsIters) break;
    z = abs(z);
    if (z.x < z.y) z = z.yx;
    z = z * s - uKifsOff * (s - 1.0);
    z = R * z;
  }
  return z * pow(s, -float(uKifsIters));
}

vec2 foldHyper(vec2 z) {
  float d2 = dot(z, z);
  if (d2 > 1.0) z /= d2;
  vec2 a = uHyp.zw;
  z = cdiv(z - a, vec2(1, 0) - cmul(vec2(a.x, -a.y), z));
  float seg = TAU / max(3.0, uSym);
  vec2 C = vec2(uHyp.x, 0.0);
  float r2 = uHyp.y * uHyp.y;
  for (int i = 0; i < 32; i++) {
    float ang = mod(atan(z.y, z.x), seg);
    if (ang > seg * 0.5) ang = seg - ang;
    z = length(z) * vec2(cos(ang), sin(ang));
    vec2 v = z - C;
    float vv = dot(v, v);
    if (vv >= r2) break;
    z = C + v * (r2 / vv);
  }
  return z;
}

vec2 foldDroste(vec2 z) {
  float L = uDroste.x;
  vec2 w = vec2(log(max(length(z), 1e-6)), atan(z.y, z.x));
  float T = L / TAU;
  w = vec2(w.x + w.y * T, w.y - w.x * T);
  w.x -= uDroste.y;
  w.x = w.x - L * floor(w.x / L) - L;
  return exp(w.x) * 0.9 * vec2(cos(w.y), sin(w.y));
}

vec2 foldTiles(vec2 z) {
  z = z * uTile + 1.0;
  z = abs(z - 2.0 * floor(z / 2.0) - 1.0);
  if (z.x < z.y) z = z.yx;
  return z / uTile;
}

vec2 foldSpace(vec2 p) {
  p = warpSpace(p);
  if (uMode == 0) return p;
  vec2 z = rot(-uSpin) * (p - uCenter) * uViewZoom;
  if (uMode == 1) z = foldKaleido(z);
  else if (uMode == 2) z = foldKifs(z);
  else if (uMode == 3) z = foldHyper(z);
  else if (uMode == 4) z = foldDroste(z);
  else z = foldTiles(z);
  return uCenter + rot(uSpin) * z / uViewZoom;
}
`;

  // Feedback: new = max(colorMatrix * prev(T(fold(p))), generators, webcam)
  const FEEDBACK_FS = HEADER + COMMON + `
uniform sampler2D uPrev, uCam, uRM;
uniform bool uFoldLoop;
uniform mat3 uColorMat;
uniform float uFbRot, uFbZoom, uFbSwirl, uFbWarp, uFbWarpT, uSharpen;
uniform int uGen;
uniform float uGenK, uGenScale, uGenT, uGenRadius, uGenFold;
uniform bool uCamOn;
uniform float uCamK, uCamZoom, uCamAspect, uCamColorize;
out vec4 o;

vec2 fbTransform(vec2 q) {
  vec2 d = q - uCenter;
  d = rot(uFbRot + uFbSwirl * length(d)) * d / uFbZoom;
  if (uFbWarp > 0.0) d += uFbWarp * vec2(gnoise(d * 3.0 + uFbWarpT), gnoise(d * 3.0 - uFbWarpT + 7.3));
  return uCenter + d;
}

vec2 voronoi(vec2 x) {   // returns (F2 - F1, cell hash)
  vec2 n = floor(x), f = fract(x);
  float d1 = 8.0, d2 = 8.0, id = 0.0;
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec2 g = vec2(i, j);
    vec2 h = hash2(n + g);
    vec2 r = g + 0.5 + 0.4 * sin(uGenT * 0.6 + TAU * h) - f;
    float d = dot(r, r);
    if (d < d1) { d2 = d1; d1 = d; id = h.x; } else if (d < d2) d2 = d;
  }
  return vec2(sqrt(d2) - sqrt(d1), id);
}

vec3 generator(vec2 q) {
  float t = uGenT;
  vec2 g = (q - uCenter) * uGenScale * uGenFold;
  float r = length(g), a = atan(g.y, g.x);
  vec3 c = vec3(0.0);
  if (uGen == 1) {
    float v = sin(r * 14.0 - t * 3.0);
    c = pal(r * 0.4 - t * 0.1) * smoothstep(0.7, 1.0, v) * 1.5;
  } else if (uGen == 2) {
    float v = sin(r * 10.0 - t * 2.0 + 3.0 * sin(a * 5.0 + t * 0.7));
    c = pal(a / TAU + r * 0.3 + t * 0.05) * smoothstep(0.85, 1.0, v) * 1.6;
  } else if (uGen == 3) {
    float n = fbm(g * 1.5 + vec2(0.0, t * 0.15));
    float v = abs(fract(n * 5.0 + t * 0.2) - 0.5);
    c = pal(n * 1.5 + t * 0.05) * smoothstep(0.1, 0.0, v) * 1.4;
  } else if (uGen == 4) {
    vec2 v = voronoi(g * 3.0);
    c = pal(v.y + t * 0.05) * smoothstep(0.12, 0.0, v.x) * 1.6;
  } else if (uGen == 5) {
    for (int i = 0; i < 6; i++) {
      float fi = float(i);
      vec2 pos = 0.7 * vec2(sin(t * 0.7 + fi * 1.3), cos(t * 0.9 + fi * 2.1)) * (0.6 + 0.4 * sin(t * 0.3 + fi));
      vec2 d = g - pos;
      c += pal(fi / 6.0 + t * 0.1) * exp(-dot(d, d) * 30.0) * 1.5;
    }
  } else if (uGen == 6) {
    c = texture(uRM, toUV(q)).rgb;
  }
  if (uGenRadius > 0.0) c *= 1.0 - smoothstep(uGenRadius * 0.7, uGenRadius, length(q - uCenter));
  return c;
}

vec3 camera(vec2 q) {
  vec2 cu = (q - uCenter) * uCamZoom;
  vec2 uv = vec2(0.5 - 0.5 * cu.x / uCamAspect, 0.5 - 0.5 * cu.y);
  vec3 c = texture(uCam, uv).rgb;
  float l = dot(c, vec3(0.299, 0.587, 0.114));
  return mix(c, pal(l * 1.2 + uGenT * 0.05) * (l * 1.5 + 0.05), uCamColorize);
}

void main() {
  vec2 p = fragP();
  vec2 q = uFoldLoop ? foldSpace(p) : p;
  vec2 uv = toUV(fbTransform(q));
  vec3 c = texture(uPrev, uv).rgb;
  if (uSharpen > 0.0) {   // unsharp mask: undoes the blur of resampling every frame
    vec2 px = 1.0 / uRes;
    vec3 a = texture(uPrev, uv + vec2(px.x, 0.0)).rgb, b = texture(uPrev, uv - vec2(px.x, 0.0)).rgb;
    vec3 d = texture(uPrev, uv + vec2(0.0, px.y)).rgb, e = texture(uPrev, uv - vec2(0.0, px.y)).rgb;
    vec3 lo = min(min(a, b), min(d, e)), hi = max(max(a, b), max(d, e));
    c = clamp(c + (c - (a + b + d + e) * 0.25) * uSharpen, min(lo, c), max(hi, c));
  }
  c = clamp(uColorMat * c, 0.0, 32.0);
  if (uGen > 0) c = max(c, generator(q) * uGenK);
  if (uCamOn) c = max(c, camera(q) * uCamK);
  if (any(isnan(c))) c = vec3(0.0);
  o = vec4(c, 1.0);
}`;

  // Raymarched kaleidoscopic IFS (Menger-style) tunnel, fed into the loop as a source.
  const RAYMARCH_FS = HEADER + COMMON + `
uniform mat3 uRmRot;
uniform float uRmScale, uRmZ, uRmT;
out vec4 o;

float de(vec3 p, out float trap) {
  p = mod(p + 2.0, 4.0) - 2.0;
  float S = uRmScale, s = 1.0;
  trap = 1e9;
  for (int i = 0; i < 5; i++) {
    p = abs(p);
    if (p.x < p.y) p.xy = p.yx;
    if (p.x < p.z) p.xz = p.zx;
    if (p.y < p.z) p.yz = p.zy;
    p = uRmRot * p;
    p.xy = S * p.xy - (S - 1.0);
    p.z = S * p.z;
    if (p.z > 0.5 * (S - 1.0)) p.z -= S - 1.0;
    s *= S;
    trap = min(trap, dot(p, p));
  }
  return length(max(abs(p) - 1.0, 0.0)) / s;
}

void main() {
  vec2 p = fragP();
  vec3 ro = vec3(0.0, 0.0, uRmZ);
  vec3 rd = normalize(vec3(p, 1.4));
  rd.xy = rot(uRmT * 0.15) * rd.xy;
  rd.yz = rot(sin(uRmT * 0.11) * 0.25) * rd.yz;
  float t = 0.02, glow = 0.0, trap = 0.0, steps = 0.0;
  bool hit = false;
  for (int i = 0; i < 80; i++) {
    float d = de(ro + rd * t, trap);
    glow += exp(-d * 80.0);
    steps += 1.0;
    if (d < 0.0005 * t) { hit = true; break; }
    t += d * 0.85;
    if (t > 14.0) break;
  }
  vec3 c = pal(t * 0.08 + uRmT * 0.03) * glow * 0.006;
  if (hit) {
    vec3 pos = ro + rd * t;
    vec2 e = vec2(0.0005 * t, 0.0);
    float tr;
    vec3 n = normalize(vec3(de(pos + e.xyy, tr) - de(pos - e.xyy, tr),
                            de(pos + e.yxy, tr) - de(pos - e.yxy, tr),
                            de(pos + e.yyx, tr) - de(pos - e.yyx, tr)));
    float ao = 1.0 - steps / 80.0;
    float dif = 0.35 + 0.65 * max(dot(n, -rd), 0.0);
    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    vec3 base = pal(sqrt(trap) * 0.35 + t * 0.05 + uRmT * 0.04);
    c += (base * dif * ao * ao + rim * pal(t * 0.1 + 0.5) * 0.8) * exp(-t * 0.35) * 1.3;
  }
  o = vec4(c, 1.0);
}`;

  const SCENE_FS = HEADER + COMMON + `
uniform sampler2D uFB;
uniform bool uFoldLoop;
uniform float uFisheye, uPalMap, uPalShift;
out vec4 o;
void main() {
  vec2 p = fragP();
  p *= 1.0 + uFisheye * dot(p, p) * 0.3;
  vec2 q = uFoldLoop ? p : foldSpace(p);
  vec3 c = texture(uFB, toUV(q)).rgb;
  if (uPalMap > 0.0) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, pal(l * 0.7 + uPalShift) * (l * 1.3 + 0.02), uPalMap);
  }
  o = vec4(c, 1.0);
}`;

  // Dual-filter (Kawase) bloom
  const DOWN_FS = HEADER + `
uniform sampler2D uSrc;
uniform vec2 uTexel, uOut;
uniform float uThreshold;
uniform bool uPrefilter;
out vec4 o;
void main() {
  vec2 uv = gl_FragCoord.xy / uOut;
  vec3 c = textureLod(uSrc, uv, 0.0).rgb * 4.0;
  c += textureLod(uSrc, uv + uTexel * vec2(-1, -1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(1, -1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(-1, 1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(1, 1), 0.0).rgb;
  c /= 8.0;
  if (uPrefilter) {
    float l = max(c.r, max(c.g, c.b));
    c *= max(l - uThreshold, 0.0) / max(l, 1e-4);
  }
  o = vec4(c, 1.0);
}`;

  const UP_FS = HEADER + `
uniform sampler2D uSrc;
uniform vec2 uTexel, uOut;
out vec4 o;
void main() {
  vec2 uv = gl_FragCoord.xy / uOut;
  vec3 c = texture(uSrc, uv + uTexel * vec2(-2, 0)).rgb;
  c += texture(uSrc, uv + uTexel * vec2(2, 0)).rgb;
  c += texture(uSrc, uv + uTexel * vec2(0, -2)).rgb;
  c += texture(uSrc, uv + uTexel * vec2(0, 2)).rgb;
  c += texture(uSrc, uv + uTexel * vec2(-1, -1)).rgb * 2.0;
  c += texture(uSrc, uv + uTexel * vec2(1, -1)).rgb * 2.0;
  c += texture(uSrc, uv + uTexel * vec2(-1, 1)).rgb * 2.0;
  c += texture(uSrc, uv + uTexel * vec2(1, 1)).rgb * 2.0;
  o = vec4(c / 12.0, 1.0);
}`;

  const FINAL_FS = HEADER + `
uniform sampler2D uScene, uBloomTex, uVeilTex, uLumCur, uLumAdapt;
uniform bool uGuard;
uniform vec2 uOut;
uniform float uVeil, uBloom, uChroma, uGrain, uVignette, uExposure;
uniform uint uFrame;
out vec4 o;

uvec2 pcg2d(uvec2 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  return v;
}
// Compress by the brightest channel so hot colors stay saturated instead of going white.
vec3 tonemap(vec3 c) {
  float m = max(c.r, max(c.g, c.b));
  if (m > 0.75) c *= (0.75 + 0.25 * (1.0 - exp(-(m - 0.75) * 4.0))) / m;
  return c;
}

void main() {
  vec2 uv = gl_FragCoord.xy / uOut;
  vec2 d = uv - 0.5;
  vec2 ca = d * uChroma * 0.012;
  vec3 c = vec3(texture(uScene, uv - ca).r, texture(uScene, uv).g, texture(uScene, uv + ca).b);
  c += texture(uBloomTex, uv).rgb * uBloom;
  if (uGuard) {
    float cur = texelFetch(uLumCur, ivec2(0), 0).r, ad = texelFetch(uLumAdapt, ivec2(0), 0).r;
    c *= clamp((ad + 0.02) / (cur + 0.02), 0.2, 3.0);
  }
  if (uVeil > 0.0) c = mix(c, texture(uVeilTex, uv).rgb, uVeil);
  c = tonemap(c * uExposure);
  float v = length(d * vec2(uOut.x / uOut.y, 1.0));
  c *= 1.0 - uVignette * smoothstep(0.3, 1.1, v);
  float n = float(pcg2d(uvec2(gl_FragCoord.xy) + uvec2(uFrame * 1973u, uFrame * 9277u)).x) / 4294967295.0;
  c += (n - 0.5) * uGrain;
  o = vec4(c, 1.0);
}`;

  // Flash guard: average scene luminance into 1×1, then ease an adapted copy
  // toward it. The final pass scales brightness by adapted / current, so
  // full-screen pulses are damped while slow changes pass through.
  const LUM_FS = HEADER + `
uniform sampler2D uSrc;
uniform float uLod;
out vec4 o;
void main() {
  float s = 0.0;
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    vec3 c = textureLod(uSrc, (vec2(x, y) + 0.5) / 16.0, uLod).rgb;
    s += dot(c, vec3(0.2126, 0.7152, 0.0722));
  }
  o = vec4(s / 256.0);
}`;

  const ADAPT_FS = HEADER + `
uniform sampler2D uPrev, uCur;
uniform float uK;
out vec4 o;
void main() { o = mix(texelFetch(uPrev, ivec2(0), 0), texelFetch(uCur, ivec2(0), 0), uK); }`;

  const COPY_FS = HEADER + `
uniform sampler2D uSrc;
uniform vec2 uOut;
out vec4 o;
void main() { o = texture(uSrc, gl_FragCoord.xy / uOut); }`;

  // Ink: instanced glowing capsules. In fold modes the CPU pre-folds endpoints;
  // in mode "none" each segment is instanced into its dihedral copies here.
  const INK_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec4 aSeg;
layout(location = 1) in vec4 aCol;
uniform vec2 uRes;
uniform int uCopies;
uniform bool uInkMirror;
uniform float uSym, uSpin, uGlow;
uniform vec2 uCenter;
out vec2 vP;
out vec4 vSeg;
out vec4 vCol;

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
vec2 copyOf(vec2 p, int k) {
  vec2 z = rot(-uSpin) * (p - uCenter);
  if (uInkMirror && (k & 1) == 1) z.y = -z.y;
  int r = uInkMirror ? k >> 1 : k;
  z = rot(6.28318530718 / uSym * float(r)) * z;
  return uCenter + rot(uSpin) * z;
}

void main() {
  int k = gl_InstanceID % uCopies;
  vec2 a = copyOf(aSeg.xy, k), b = copyOf(aSeg.zw, k);
  float R = aCol.w * (1.5 + 3.0 * uGlow);
  vec2 dir = b - a;
  float L = length(dir);
  dir = L > 1e-6 ? dir / L : vec2(1.0, 0.0);
  vec2 nrm = vec2(-dir.y, dir.x);
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
  vec2 pos = (corner.x < 0.0 ? a - dir * R : b + dir * R) + nrm * corner.y * R;
  vP = pos;
  vSeg = vec4(a, b);
  vCol = aCol;
  gl_Position = vec4(pos.x * uRes.y / uRes.x, pos.y, 0.0, 1.0);
}`;

  const INK_FS = `#version 300 es
precision highp float;
uniform float uGlow;
in vec2 vP;
in vec4 vSeg;
in vec4 vCol;
out vec4 o;
void main() {
  vec2 pa = vP - vSeg.xy, ba = vSeg.zw - vSeg.xy;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-12), 0.0, 1.0);
  float d = length(pa - ba * h);
  float rad = vCol.w;
  float R = rad * (1.5 + 3.0 * uGlow);
  float core = exp(-d * d / (rad * rad) * 2.5);
  float halo = uGlow * 0.35 * pow(max(1.0 - d / R, 0.0), 3.0);
  o = vec4(vCol.rgb * (core + halo), 1.0);
}`;

  return { FULLSCREEN_VS, FEEDBACK_FS, RAYMARCH_FS, SCENE_FS, DOWN_FS, UP_FS, FINAL_FS, COPY_FS, LUM_FS, ADAPT_FS, INK_VS, INK_FS };
})();
