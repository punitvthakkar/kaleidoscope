// RAYMARCH_FS: the 3D pattern (gen fractal3d). Shape 0 is the web's kaleidoscopic IFS lattice
// tunnel; the others are the Mathematics places (Mac only so far): two quaternion Julia sets
// seen as 3D slices of 4D (c·sin q and q² + c), the Mandelbulb, the gyroid, the Mandelbox and
// the Apollonian packing, each lit one of four ways (uRmLook: glow, glass, metal, contour).
// The compact shapes are flown over, low above their surface; the endless ones are flown through.

struct RaymarchFS {
  constant CommonU* _c; constant RaymarchU* _p; vec4 gl_FragCoord;
  vec4 o;
//@COMMON

// ---------------------------------------------------------------- quaternions (real part x)
vec4 qmul(vec4 a, vec4 b) { return vec4(a.x * b.x - dot(a.yzw, b.yzw), a.x * b.yzw + b.x * a.yzw + cross(a.yzw, b.yzw)); }
vec4 qsq(vec4 a) { return vec4(a.x * a.x - dot(a.yzw, a.yzw), 2.0 * a.x * a.yzw); }
vec4 qsin(vec4 q) {
  float l = length(q.yzw); vec3 v = l > 1e-6 ? q.yzw / l : vec3(0.0);
  return vec4(sin(q.x) * cosh(l), cos(q.x) * sinh(l) * v);
}
float qcosLen(vec4 q) {   // |cos q|
  float l = length(q.yzw), ca = cos(q.x), sa = sin(q.x), ch = cosh(l), sh = sinh(l);
  return sqrt(ca * ca * ch * ch + sa * sa * sh * sh);
}

// ---------------------------------------------------------------- distance estimates
float deLattice(vec3 p, thread float& trap) {
  float tunnel = 0.24 - length(p.xy);
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
  float d = length(max(abs(p) - 1.0, 0.0)) / s;
  return max(d, tunnel);
}

// q ← c · sin(q): a wave folded through four dimensions.
float deSineQuat(vec3 p, thread float& trap) {
  vec4 q = vec4(p, uRmSlice), c = uRmQ;
  float dr = 1.0, r2 = dot(q, q), cl = length(c);
  trap = 1e9;
  for (int i = 0; i < 9; i++) {
    dr *= cl * qcosLen(q);
    q = qmul(c, qsin(q));
    r2 = dot(q, q);
    trap = min(trap, r2);
    if (r2 > 200.0) break;
  }
  float r = sqrt(r2);
  return 0.15 * log(max(r, 1.0001)) * r / max(dr, 1e-6);
}

// q ← q² + c
float deQuatJulia(vec3 p, thread float& trap) {
  vec4 z = vec4(p, uRmSlice), c = uRmQ;
  float m2 = dot(z, z), dz2 = 1.0;
  trap = 1e9;
  for (int i = 0; i < 11; i++) {
    dz2 *= 4.0 * m2;
    z = qsq(z) + c;
    m2 = dot(z, z);
    trap = min(trap, m2);
    if (m2 > 16.0) break;
  }
  return 0.25 * sqrt(m2 / dz2) * log(max(m2, 1.0001));
}

float deBulb(vec3 p, thread float& trap) {
  float n = mix(4.0, 10.0, uRmDetail) + uRmSlice * 1.5;
  vec3 z = p; float dr = 1.0, r = length(z);
  trap = 1e9;
  for (int i = 0; i < 8; i++) {
    r = length(z);
    if (r > 2.0) break;
    float th = acos(clamp(z.z / max(r, 1e-6), -1.0, 1.0)) + uRmQ.y * 0.4, ph = atan2(z.y, z.x) + uRmQ.z * 0.4;
    dr = pow(r, n - 1.0) * n * dr + 1.0;
    float zr = pow(r, n);
    th *= n; ph *= n;
    z = zr * vec3(sin(th) * cos(ph), sin(th) * sin(ph), cos(th)) + p;
    trap = min(trap, dot(z, z));
  }
  return 0.5 * log(max(r, 1.0001)) * r / dr;
}

// sin x cos y + sin y cos z + sin z cos x = level: an endless, seamless surface.
float deGyroid(vec3 p, thread float& trap) {
  float f = mix(3.0, 7.5, uRmDetail);
  vec3 q = p * f + vec3(0.0, 0.0, uRmT * 0.05);
  float g = dot(sin(q), cos(q.yzx)) + uRmSlice * 0.9 + 0.25 * sin(uRmQ.x * 3.0 + q.z * 0.2);
  vec3 s2 = sin(q * 0.5);
  trap = dot(s2, s2) + 0.05;
  float d = (abs(g) - 0.16) / (f * 1.9);
  return max(d, 0.12 - length(p.xy));   // a clear channel to fly down
}

float deBox(vec3 p, thread float& trap) {
  float s = mix(-2.6, -1.4, uRmDetail) - uRmSlice * 0.4;
  vec3 z = p; float dr = 1.0;
  trap = 1e9;
  for (int i = 0; i < 11; i++) {
    z = clamp(z, -1.0, 1.0) * 2.0 - z;
    float r2 = dot(z, z);
    if (r2 < 0.25) { z *= 4.0; dr *= 4.0; }
    else if (r2 < 1.0) { z /= r2; dr /= r2; }
    z = s * z + p + uRmQ.yzw * 0.2;
    dr = dr * abs(s) + 1.0;
    trap = min(trap, r2);
  }
  return length(z) / abs(dr);
}

float deApollo(vec3 p, thread float& trap) {
  float s = mix(1.05, 1.45, uRmDetail) + uRmSlice * 0.1, scale = 1.0;
  p += vec3(0.0, 0.0, uRmT * 0.02);
  trap = 1e9;
  for (int i = 0; i < 7; i++) {
    p = -1.0 + 2.0 * fract(0.5 * p + 0.5);
    float r2 = dot(p, p);
    trap = min(trap, r2);
    float k = s / r2;
    p *= k; scale *= k;
  }
  return 0.25 * abs(p.y) / scale;
}

float de(vec3 p, thread float& trap) {
  switch (uRmShape) {
    case 1: return deSineQuat(p, trap);
    case 2: return deQuatJulia(p, trap);
    case 3: return deBulb(p, trap);
    case 4: return deGyroid(p, trap);
    case 5: return deBox(p, trap);
    case 6: return deApollo(p, trap);
    default: return deLattice(p, trap);
  }
}

// ---------------------------------------------------------------- lighting
vec3 shade(vec3 pos, vec3 n, vec3 rd, float trap, float t, float ao, float fade) {
  float facing = max(dot(n, -rd), 0.0), fres = pow(1.0 - facing, 3.0);
  vec3 L = normalize(vec3(0.5, 0.8, -0.4));
  if (uRmLook == 1) {
    // Glass: dark at its heart, colour pooling at the edges like a soap film.
    vec3 film = pal(facing * 1.6 + sqrt(trap) * 0.4 + uRmT * 0.04);
    vec3 r = reflect(rd, n);
    float spec = pow(max(dot(r, L), 0.0), 60.0);
    return (film * (0.08 + 1.7 * fres) + vec3(1.0) * spec * 1.4) * fade;
  }
  if (uRmLook == 2) {
    // Metal: the palette as a sky reflected in a polished surface.
    vec3 r = reflect(rd, n);
    vec3 env = pal(r.y * 0.45 + 0.5 + uRmT * 0.02) * (0.55 + 0.9 * smoothstep(-0.3, 1.0, r.y));
    float spec = pow(max(dot(r, L), 0.0), 40.0);
    float f0 = 0.55 + 0.45 * fres;
    return (env * f0 * (0.35 + 0.65 * ao) + spec * 1.6 + pal(sqrt(trap) * 0.3) * 0.08) * fade;
  }
  if (uRmLook == 3) {
    // Neon contours: glowing lines where the shape's own measure crosses even steps.
    float v = sqrt(trap) * 5.0 + t * 0.5 - uRmT * 0.3;
    float line = smoothstep(0.07, 0.0, abs(fract(v) - 0.5));
    vec3 col = pal(floor(v) * 0.13 + uRmT * 0.03);
    return (col * line * 2.2 + pal(0.5 + t * 0.1) * fres * 0.6 + vec3(0.02)) * fade;
  }
  // Glow: lit softly, coloured by how the orbit wandered.
  float dif = 0.35 + 0.65 * facing;
  vec3 base = pal(sqrt(trap) * 0.35 + t * 0.05 + uRmT * 0.04);
  return (base * dif * ao * ao + fres * pal(t * 0.1 + 0.5) * 0.8) * fade * (uRmShape == 0 ? 1.25 : 2.2);
}

// The compact maths shapes, flown over: the camera skims the surface (the engine keeps it
// clear of it), which fills the picture; far away it dissolves into the palette's haze.
// Surface detail is resolved to the pixel at any height, so sinking closer keeps revealing more.
vec3 surfaceFlight(vec2 p) {
  vec3 ro = uRmCamP.xyz, fw = uRmCamF.xyz, up0 = uRmCamU.xyz;
  float h = max(uRmCamP.w, 1e-5), R = uRmCamF.w;
  vec3 rt = normalize(cross(up0, fw)), up = cross(fw, rt);
  vec3 rd = normalize(p.x * rt + p.y * up + 1.5 * fw);
  float pix = 2.0 / (uRes.y * 1.5);                 // one pixel's width per unit of distance
  float far = R * 3.0, t = 0.0, trap = 0.0, glow = 0.0, steps = 0.0;
  bool hit = false;
  int maxSteps = min(uRmSteps * 2, 140);
  for (int i = 0; i < 140; i++) {
    if (i >= maxSteps) break;
    float d = de(ro + rd * t, trap);
    glow += exp(-d / (h * 0.12));
    steps += 1.0;
    if (d < pix * t + 1e-6) { hit = true; break; }   // a pixel's width: detail finer than that would only shimmer
    t += d * (uRmShape == 1 ? 0.55 : 0.8);
    if (t > far) break;
  }
  float upness = dot(rd, up0);
  vec3 c = pal(0.55 + upness * 0.35 + uRmT * 0.02) * (0.06 + 0.12 * max(upness, 0.0));
  float fog = exp(-t / (h * 9.0 + R * 0.2));
  c += pal(t / h * 0.04 + uRmT * 0.03) * glow * 0.004 * fog;
  if (hit) {
    vec3 pos = ro + rd * t;
    vec2 e = vec2(max(pix * t, 1e-6), 0.0);
    float tr;
    vec3 n = normalize(vec3(de(pos + e.xyy, tr) - de(pos - e.xyy, tr),
                            de(pos + e.yxy, tr) - de(pos - e.yxy, tr),
                            de(pos + e.yyx, tr) - de(pos - e.yyx, tr)));
    float ao = sqrt(1.0 - steps / float(maxSteps));   // up close crevices take many steps: soften
    c = mix(c, shade(pos, n, rd, trap, t / h * 0.25, ao, 1.0) * (uRmLook == 0 ? 1.5 : 1.0), fog);
  }
  return c;
}

// The kaleidoscope folds every point of the picture into one wedge before it reads this
// layer (feedback.metal: generator(foldKaleidoSpace(p))), so only that wedge, and a few
// pixels around it for the bilinear taps, is ever seen: everything else is skipped.
bool rmSeen(vec2 p) {
  if (uRmPad < 0.5 || uMode != 1) return true;
  vec2 z = spinInv() * (p - uCenter) * uViewZoom;
  float wedge = uMirror ? PI / uSym : TAU / uSym, r = length(z), a = atan(z.y, z.x);
  float m = 8.0 / uRes.y * uViewZoom;               // four pixels
  if (r < m || (a >= 0.0 && a <= wedge)) return true;
  float d0 = a < 0.0 ? (a > -0.5 * PI ? r * sin(-a) : r) : 1e9;
  float a1 = a - wedge, d1 = a1 > 0.0 ? (a1 < 0.5 * PI ? r * sin(a1) : r) : 1e9;
  return min(d0, d1) < m;
}

void main() {
  vec2 p = fragP();
  if (!rmSeen(p)) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
  if (uRmCamU.w > 0.5) { o = vec4(surfaceFlight(p), 1.0); return; }
  const bool orbit = uRmShape == 1 || uRmShape == 2 || uRmShape == 3 || uRmShape == 5;
  vec3 ro, rd;
  float far = 14.0;
  if (orbit) {
    // Turned by the hand (and slowly on its own), zoomed by a dive.
    float base = uRmShape == 1 ? 6.8 : uRmShape == 2 ? 2.4 : uRmShape == 3 ? 2.5 : 6.0;
    float dist = base / max(uRmZoom, 0.1);
    float yaw = uRmYaw + uRmT * 0.06, pitch = clamp(0.25 + uRmPitch, -1.3, 1.3);
    ro = dist * vec3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw));
    vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0.0, 1.0, 0.0), fw)), up = cross(fw, rt);
    rd = normalize(p.x * rt + p.y * up + 1.7 * fw);
    far = dist + base * 1.6;
  } else {
    // The Apollonian packing is built on planes through y = 0: fly above them, never along one.
    ro = vec3(0.0, uRmShape == 6 ? 0.55 : 0.0, uRmZ);
    rd = normalize(vec3(p, 1.4));
    rd.xy = rot(uRmT * 0.03 + uRmYaw) * rd.xy;
    rd.yz = rot(sin(uRmT * 0.09) * 0.18 + uRmPitch * 0.6) * rd.yz;
  }
  // A shape seen from outside: start where its bounding sphere begins.
  float t = 0.05;
  if (orbit) {
    float R = uRmShape == 1 ? 3.2 : uRmShape == 5 ? 4.5 : 1.7;
    float b = dot(ro, rd), c = dot(ro, ro) - R * R, h = b * b - c;
    if (h < 0.0) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    t = max(0.05, -b - sqrt(h));
    far = min(far, -b + sqrt(h));
  }
  float glow = 0.0, trap = 0.0, steps = 0.0;
  bool hit = false;
  int maxSteps = uRmShape == 0 ? uRmSteps : orbit ? min(uRmSteps * 3 / 2, 120) : min(uRmSteps * 5 / 4, 100);
  for (int i = 0; i < 150; i++) {
    if (i >= maxSteps) break;
    float d = de(ro + rd * t, trap);
    glow += exp(-d * 75.0);
    steps += 1.0;
    if (d < (uRmShape == 1 ? 0.0016 : 0.0006) * t) { hit = true; break; }
    t += d * (orbit ? 0.7 : 0.85);
    if (t > far) break;
  }
  vec3 c = pal(t * 0.08 + uRmT * 0.03) * glow * (uRmShape == 0 ? 0.006 : 0.003);
  if (hit) {
    vec3 pos = ro + rd * t;
    vec2 e = vec2(0.0006 * t, 0.0);
    float tr;
    vec3 n = normalize(vec3(de(pos + e.xyy, tr) - de(pos - e.xyy, tr),
                            de(pos + e.yxy, tr) - de(pos - e.yxy, tr),
                            de(pos + e.yyx, tr) - de(pos - e.yyx, tr)));
    float ao = 1.0 - steps / float(maxSteps);
    float fade = orbit ? 1.0 : exp(-t * 0.32);
    c += shade(pos, n, rd, trap, t, ao, fade);
  }
  o = vec4(c, 1.0);
}
};

fragment float4 raymarch_fs(VSOut in [[stage_in]],
                            constant CommonU& C [[buffer(0)]], constant RaymarchU& U [[buffer(1)]]) {
  RaymarchFS s;
  s._c = &C; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uRes.y);
  s.main();
  return s.o;
}
