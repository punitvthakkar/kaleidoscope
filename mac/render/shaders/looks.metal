// Hand looks (index.html SHADERS: LOOK_COPY, LOOK_VS, LOOK_FS, STRIP_VS, STRIP_FS, AURA_FS).
// Glowing segments (a point is a segment of no length), instanced over the chamber's
// mirror images like INK_VS when they feed the echoes (uCopies > 1); strips (satin
// ribbons and aurora curtains); and the aura, a fullscreen pass scissored to the hands.
// Every entry point reads one LookU block in place (constant memory, see prelude.metal).
// Segments: 12 floats each (aSeg, aCol, aK), instance i / uCopies; strip vertices: 6
// floats each (aPos, aD).

#define uLookMirror _c->uLookMirror
#define uFish _c->uFish
#define uGain _c->uGain
#define uT _c->uT
#define uKind _c->uKind
#define uBreath _c->uBreath
#define uHands _c->uHands
#define uPts _c->uPts
#define uHand _c->uHand

// LOOK_COPY
struct LookCopy {
  constant LookU* _c;
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
vec2 copyOf(vec2 p, int k) {
  p *= 1.0 + uFish * dot(p, p) * 0.3;
  vec2 z = rot(-uSpin) * (p - uCenter);
  if (uLookMirror && (k & 1) == 1) z.y = -z.y;
  int r = uLookMirror ? k >> 1 : k;
  z = rot(6.28318530718 / uSym * float(r)) * z;
  return uCenter + rot(uSpin) * z;
}
};

// LOOK_VS / LOOK_FS
struct LookVSOut {
  float4 position [[position]];
  vec2 vP;
  vec4 vSeg;
  vec4 vCol;
  vec4 vK;
};

vertex LookVSOut look_vs(uint vid [[vertex_id]], uint iid [[instance_id]],
                         constant LookU* _c [[buffer(1)]], const device float4* segs [[buffer(2)]]) {
  LookCopy s; s._c = _c;
  int gl_VertexID = int(vid), gl_InstanceID = int(iid);
  const int i = (gl_InstanceID / uCopies) * 3;
  vec4 aSeg = segs[i], aCol = segs[i + 1], aK = segs[i + 2];
  LookVSOut o;

  int k = gl_InstanceID % uCopies;
  vec2 a = s.copyOf(aSeg.xy, k), b = s.copyOf(aSeg.zw, k);
  float R = aCol.w;
  vec2 dir = b - a;
  float L = length(dir);
  dir = L > 1e-6 ? dir / L : vec2(1.0, 0.0);
  vec2 nrm = vec2(-dir.y, dir.x);
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
  vec2 pos = (corner.x < 0.0 ? a - dir * R : b + dir * R) + nrm * corner.y * R;
  o.vP = pos;
  o.vSeg = vec4(a, b);
  o.vCol = aCol;
  o.vK = aK;
  o.position = vec4(pos.x * uRes.y / uRes.x, pos.y, 0.0, 1.0);
  return o;
}

fragment float4 look_fs(LookVSOut in [[stage_in]], constant LookU* _c [[buffer(1)]]) {
  vec2 vP = in.vP; vec4 vSeg = in.vSeg, vCol = in.vCol, vK = in.vK;

  vec2 pa = vP - vSeg.xy, ba = vSeg.zw - vSeg.xy;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-12), 0.0, 1.0);
  float d = length(pa - ba * h) / vCol.w;
  if (d > 1.0) discard_fragment();
  float k = exp(-d * d * vK.y) + exp(-d * 3.5) * vK.z * (1.0 - d);
  return vec4(vCol.rgb * (vK.x * k * uGain), 0.0);
}

// STRIP_VS / STRIP_FS: satin ribbons (uKind 0, premultiplied) and aurora curtains (uKind 1, light).
struct StripVSOut {
  float4 position [[position]];
  vec4 vD;
};

vertex StripVSOut strip_vs(uint vid [[vertex_id]], uint iid [[instance_id]],
                           constant LookU* _c [[buffer(1)]], const device float* verts [[buffer(2)]]) {
  LookCopy s; s._c = _c;
  const device float* v = verts + vid * 6;
  vec2 aPos = vec2(v[0], v[1]); vec4 aD = vec4(v[2], v[3], v[4], v[5]);
  int gl_InstanceID = int(iid);
  StripVSOut o;

  vec2 p = s.copyOf(aPos, gl_InstanceID % uCopies);
  o.vD = aD;
  o.position = vec4(p.x * uRes.y / uRes.x, p.y, 0.0, 1.0);
  return o;
}

struct StripFS {
  constant LookU* _c; vec4 vD; vec4 o;
vec3 spectral(float t) { return clamp(0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67))), 0.0, 1.0); }
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y); }
void main() {
  if (uKind == 0) {
    // Satin: across, along, cos of the twist, hue. Narrow edge-on, a sheen that slides across.
    float across = vD.x, along = vD.y, cs = vD.z, hue = fract(vD.w), pres = floor(vD.w) / 255.0;
    vec3 front = spectral(hue) * 1.15, back = spectral(hue + 0.12) * 0.55 + 0.05;
    vec3 base = mix(back, front, step(0.0, cs));
    float sn = sqrt(max(0.0, 1.0 - cs * cs)) * sign(cs + 1e-4);
    float sheen = exp(-pow((across * 0.9 - sn * 0.85) * 3.2, 2.0));
    float weave = 0.92 + 0.08 * sin(along * 1400.0 + across * 3.0);
    float a = smoothstep(1.0, 0.7, abs(across)) * smoothstep(0.0, 0.03, along) * (1.0 - smoothstep(0.55, 1.0, along)) * pres;
    vec3 col = (base * (0.25 + 0.75 * abs(cs)) * weave + vec3(1.0, 0.96, 0.92) * sheen * (0.5 + 0.9 * (1.0 - abs(cs))) * 0.9) * 0.62;
    o = vec4(col * a * uGain, a * 0.92 * uGain);
  } else {
    // Curtain: u along the finger's path, v down the curtain, hue, strength.
    float u = vD.x, v = vD.y;
    float rays = pow(0.5 + 0.5 * sin(u * 160.0 + uT * 2.0 + sin(u * 31.0 + uT) * 3.0), 2.5);
    float n = noise(vec2(u * 26.0 - uT * 0.4, v * 2.5 - uT * 0.9));
    vec3 high = mix(vec3(0.75, 0.25, 1.0), vec3(1.0, 0.35, 0.55), vD.z);
    vec3 col = mix(vec3(0.15, 1.0, 0.55), high, smoothstep(0.15, 0.95, v + 0.25 * (n - 0.5)));
    float a = smoothstep(0.0, 0.06, v) * pow(1.0 - v, 1.6) * (0.3 + 0.7 * rays * (0.4 + 0.8 * n)) * smoothstep(0.0, 0.15, u) * vD.w;
    o = vec4(col * a * 1.6 * uGain, 0.0);
  }
}
};

fragment float4 strip_fs(StripVSOut in [[stage_in]], constant LookU* _c [[buffer(1)]]) {
  StripFS s; s._c = _c; s.vD = in.vD;
  s.main();
  return s.o;
}

// AURA_FS: a corona of flames around each whole hand, from its distance field. Drawn on
// top only, in a box around the hands (scissored), so it costs little.
constant int lookAuraB[42] = {0,1, 1,2, 2,3, 3,4, 0,5, 5,6, 6,7, 7,8, 5,9, 9,10, 10,11, 11,12, 9,13, 13,14, 14,15, 15,16, 13,17, 17,18, 18,19, 19,20, 0,17};
#define B lookAuraB
struct AuraFS {
  constant LookU* _c; vec4 gl_FragCoord; vec4 o;
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y); }
float fbm(vec2 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 3; i++) { s += a * noise(p); p = mat2(1.6, 1.2, -1.2, 1.6) * p + 7.1; a *= 0.5; } return s * 1.07; }   // 3 octaves: soft light needs no more
vec3 spectral(float t) { return clamp(0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67))), 0.0, 1.0); }
float seg(vec2 p, vec2 a, vec2 b) { vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0); return length(pa - ba * h); }
void main() {
  vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y * 2.0;
  vec3 c = vec3(0.0);
  for (int h = 0; h < 2; h++) {
    if (h >= uHands) break;
    vec4 H = uHand[h];          // palm xy, hand size, presence
    if (H.w <= 0.0) continue;
    float S = H.z, d = 1e9;
    for (int i = 0; i < 42; i += 2) {
      int e = B[i + 1];
      float r = (e == 4 || e == 8 || e == 12 || e == 16 || e == 20) ? 0.055 : 0.075;
      d = min(d, seg(p, uPts[h * 21 + B[i]], uPts[h * 21 + e]) - r * S);
    }
    d = min(d, length(p - H.xy) - 0.3 * S);
    float dn = d / S;
    if (dn > 2.2) continue;   // past here the corona adds under 1e-3: skip the flames' noise
    vec2 rel = (p - H.xy) / S;
    vec2 dir = rel / max(length(rel), 1e-3);
    vec2 fq = dir * 1.9 + vec2(0.7, 0.5) * (dn * 2.6 - uT * 0.7);
    float fl = fbm(fq + fbm(fq * 0.8 + vec2(uT * 0.1, 0.0)) * 1.4);
    float reach = mix(0.32, 0.7, uBreath);
    float dd = dn - (fl - 0.45) * 0.55 * (0.55 + 0.45 * uBreath);
    float corona = exp(-max(dd, 0.0) / reach * 2.6) * smoothstep(-0.06, 0.03, dn);
    float rays = corona > 0.01 ? 0.55 + 0.9 * pow(fbm(dir * 7.0 + vec2(uT * 0.25 + dn)), 2.0) : 1.0;
    vec3 col = spectral(fract(dd * 1.1 - uT * 0.05 + dir.x * 0.06)) * 0.75 + 0.25;
    c += H.w * (col * corona * rays * (0.34 + 0.3 * uBreath)
              + vec3(1.0, 0.86, 0.7) * exp(-abs(dn) * 34.0) * (0.28 + 0.22 * uBreath)
              + vec3(0.6, 0.65, 1.0) * smoothstep(0.0, -0.25, dn) * 0.035);
  }
  o = vec4(c * uGain, 0.0);
}
};
#undef B

fragment float4 aura_fs(VSOut in [[stage_in]], constant LookU* _c [[buffer(1)]]) {
  AuraFS s; s._c = _c;
  s.gl_FragCoord = glFragCoord(in.position, uRes.y);
  s.main();
  return s.o;
}

#undef uLookMirror
#undef uFish
#undef uGain
#undef uT
#undef uKind
#undef uBreath
#undef uHands
#undef uPts
#undef uHand
