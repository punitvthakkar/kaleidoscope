// FEEDBACK_FS: smooth dihedral flow + damped recursive echo coupling + soft generator injection.

struct FeedbackFS {
  constant CommonU* _c; constant FeedbackU* _p; vec4 gl_FragCoord;
  MirrorTex uPrev; Tex uCam; MirrorTex uRM;
  vec4 o;
//@COMMON

vec2 fbTransform(vec2 q) {
  vec2 d = q - uCenter;
  d = (uFbSwirl == 0.0 ? mat2(uFbRotCS.x, uFbRotCS.y, -uFbRotCS.y, uFbRotCS.x) : rot(uFbRot + uFbSwirl * length(d))) * d / uFbZoom;
  // Dream current: the flow of z² + c, turned a quarter so its two drifting poles
  // become slow whirlpools that light orbits and spirals between.
  if (uDream > 0.0) {
    vec2 v = (cmul(d, d) * 0.9 + uDreamC - d) / (1.0 + 0.6 * dot(d, d));
    d += uDream * (vec2(-v.y, v.x) + 0.1 * v);
  }
  if (uFbWarp > 0.0) d += uFbWarp * vec2(gnoise(d * 2.6 + uFbWarpT), gnoise(d * 2.6 - uFbWarpT + 7.3));
  for (int i = 0; i < 4; i++) {
    if (i >= uRipN) break;
    vec2 r = q - uRip[i].xy;
    // Wet paint (a negative radius): a moving finger drags the picture along with it.
    if (uRip[i].z < 0.0) {
      float a = -uRip[i].z - 4.0;
      d -= vec2(cos(a), sin(a)) * (uRip[i].w * exp(-dot(r, r) * 28.0));
      continue;
    }
    float l = max(length(r), 1e-4), w = l - uRip[i].z;
    d -= r / l * (uRip[i].w * exp(-w * w * 160.0));
  }
  return uCenter + d;
}

vec3 voronoi(vec2 x) {   // returns (edge distance, cell center distance, cell hash)
  vec2 n = floor(x), f = fract(x);
  float d1 = 8.0, d2 = 8.0, id = 0.0;
  for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
    vec2 g = vec2(i, j);
    vec2 h = hash2(n + g);
    vec2 r = g + 0.5 + 0.38 * sin(uGenT * 0.55 + TAU * h) - f;
    float d = dot(r, r);
    if (d < d1) { d2 = d1; d1 = d; id = h.x; } else if (d < d2) d2 = d;
  }
  return vec3(sqrt(d2) - sqrt(d1), sqrt(d1), id);
}

vec3 generator(vec2 q) {
  float t = uGenT;
  vec2 g = (q - uCenter) * uGenScale;
  float r = length(g), a = atan(g.y, g.x);
  vec3 c = vec3(0.0);
  if (uGen == 1) {
    float w1 = sin(r * 12.0 - t * 2.6);
    float w2 = sin(r * 7.5 - t * 1.7 + 1.5 * cos(a * 4.0));
    float v = smoothstep(0.35, 0.98, 0.55 * w1 + 0.45 * w2);
    c = pal(r * 0.38 - t * 0.08) * (v * 1.15 + 0.08 * (0.5 + 0.5 * w1));
  } else if (uGen == 2) {
    float w = sin(r * 9.0 - t * 1.8 + 2.4 * sin(a * 6.0 + t * 0.6));
    float v = smoothstep(0.45, 0.99, w);
    float glow = exp(-abs(w - 0.85) * 6.0) * 0.35;
    c = pal(a / TAU + r * 0.28 + t * 0.05) * (v * 1.2 + glow);
  } else if (uGen == 3) {
    float n = fbm(g * 1.35 + vec2(0.0, t * 0.14));
    float wave = 0.5 + 0.5 * cos(TAU * (n * 3.6 + t * 0.16));
    float v = pow(wave, 5.0);
    c = pal(n * 1.4 + t * 0.05) * (v * 1.15 + 0.06 * wave);
  } else if (uGen == 4) {
    vec3 v = voronoi(g * 2.6);
    float edge = exp(-v.x * 14.0);
    float core = exp(-v.y * 4.2) * 0.35;
    c = pal(v.z * 0.6 + t * 0.05) * (edge * 1.2 + core);
  } else if (uGen == 5) {
    for (int i = 0; i < 6; i++) {
      float fi = float(i);
      vec2 pos = 0.68 * vec2(sin(t * 0.65 + fi * 1.3), cos(t * 0.82 + fi * 2.1)) * (0.6 + 0.4 * sin(t * 0.28 + fi));
      vec2 d = g - pos;
      c += pal(fi / 6.0 + t * 0.08) * exp(-dot(d, d) * 24.0) * 1.25;
    }
  } else if (uGen == 6) {
    c = texture(uRM, toUV(q)).rgb;
  }
  if (uGenRadius > 0.0) c *= 1.0 - smoothstep(uGenRadius * 0.65, uGenRadius, length(q - uCenter));
  return c;
}

vec3 camera(vec2 q) {
  vec2 cu = (q - uCenter) * uCamZoom;
  vec2 uv = vec2(0.5 - 0.5 * cu.x / uCamAspect, 0.5 - 0.5 * cu.y);
  vec3 c = texture(uCam, uv).rgb;
  float l = dot(c, vec3(0.299, 0.587, 0.114));
  return mix(c, pal(l * 1.2 + uGenT * 0.05) * (l * 1.5 + 0.05), uCamColorize);
}

vec3 samplePrev(vec2 uv) {
  vec3 c = textureLod(uPrev, uv, 0.0).rgb;
  if (uSharpen > 0.0) {
    vec2 px = 1.0 / uRes;
    vec3 a = textureLod(uPrev, uv + vec2(px.x, 0.0), 0.0).rgb;
    vec3 b = textureLod(uPrev, uv - vec2(px.x, 0.0), 0.0).rgb;
    vec3 d = textureLod(uPrev, uv + vec2(0.0, px.y), 0.0).rgb;
    vec3 e = textureLod(uPrev, uv - vec2(0.0, px.y), 0.0).rgb;
    vec3 lo = min(min(a, b), min(d, e)), hi = max(max(a, b), max(d, e));
    c = clamp(c + (c - (a + b + d + e) * 0.25) * uSharpen, min(lo, c), max(hi, c));
  }
  return c;
}

void main() {
  vec2 p = fragP();
  bool kaleidoFold = uFoldLoop && uMode == 1;
  vec2 qBase = kaleidoFold ? foldKaleidoSpace(p) : p;
  vec2 qAdv = fbTransform(qBase);
  if (kaleidoFold) qAdv = foldKaleidoSpace(qAdv);
  vec3 c = samplePrev(toUV(qAdv));

  // In multi-scale chambers (kifs, hyperbolic, droste, tiles), "Fold the echoes too"
  // couples a damped recursive echo without collapsing the full-resolution canvas.
  if (uFoldLoop && uMode >= 2) {
    float tr, sc, dm, rg;
    vec2 qFold = foldSpaceEx(p, tr, sc, dm, rg);
    float lod = clamp(log2(max(sc * 0.75, 1.0)), 0.0, 4.0);
    vec3 cFold = textureLod(uPrev, toUV(fbTransform(qFold)), lod).rgb * dm;
    float kEcho = 1.0 - exp(-uDt * 4.5);
    c = mix(c, max(c, cFold * 0.88), kEcho);
  }

  c = clamp(uColorMat * c, 0.0, 16.0);
  // Soft highlight compression inside the loop prevents runaway hot spots.
  c /= 1.0 + max(c.r, max(c.g, c.b)) * 0.035;

  float kIn = 1.0 - exp(-uDt * 14.0);
  if (uGen > 0) {
    vec3 g = generator(qBase) * uGenK;
    c = mix(c, max(c, g), kIn) + g * (0.14 * kIn);
  }
  if (uCamOn) {
    vec3 cm = camera(qBase) * uCamK;
    c = mix(c, max(c, cm), kIn) + cm * (0.12 * kIn);
  }
  if (any(isnan(c))) c = vec3(0.0);
  o = vec4(c, 1.0);
}
};

fragment float4 feedback_fs(VSOut in [[stage_in]],
                            constant CommonU& C [[buffer(0)]], constant FeedbackU& U [[buffer(1)]],
                            texture2d<float> prev [[texture(0)]], texture2d<float> cam [[texture(1)]],
                            texture2d<float> rm [[texture(2)]], constant TexK& K [[buffer(3)]]) {
  FeedbackFS s;
  s._c = &C; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uRes.y);
  s.uPrev = MirrorTex{prev, &K.kh[0], &K.maxLod};
  s.uCam = Tex{cam, sMirror, false};   // camera rows arrive top first, as the web's video upload
  s.uRM = MirrorTex{rm, &K.kh[2], &K.zero};
  s.main();
  return s.o;
}
