// Dual-filter (Kawase) bloom, final, flash-guard measure, copy.

// DOWN_FS
struct DownFS {
  constant DownU* _p; ClampTex uSrc; vec4 gl_FragCoord; vec4 o;
void main() {
  vec2 uv = gl_FragCoord.xy / uOut;
  vec3 c = textureLod(uSrc, uv, 0.0).rgb * 4.0;
  c += textureLod(uSrc, uv + uTexel * vec2(-1, -1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(1, -1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(-1, 1), 0.0).rgb;
  c += textureLod(uSrc, uv + uTexel * vec2(1, 1), 0.0).rgb;
  c /= 8.0;
  // Alpha carries plain average brightness down the chain, so the flash guard
  // reads the scene's mean from the smallest level instead of a full mip chain.
  float lum = textureLod(uSrc, uv, 0.0).a;
  if (uPrefilter) {
    lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float l = max(c.r, max(c.g, c.b));
    c *= max(l - uThreshold, 0.0) / max(l, 1e-4);
  }
  o = vec4(c, lum);
}
};

fragment float4 down_fs(VSOut in [[stage_in]], constant DownU& U [[buffer(1)]], texture2d<float> src [[texture(0)]],
                        constant TexK& K [[buffer(3)]]) {
  DownFS s; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uOut.y);
  s.uSrc = ClampTex{src, &K.kh[0]};
  s.main();
  return s.o;
}

// UP_FS
struct UpFS {
  constant UpU* _p; ClampTex uSrc; vec4 gl_FragCoord; vec4 o;
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
}
};

fragment float4 up_fs(VSOut in [[stage_in]], constant UpU& U [[buffer(1)]], texture2d<float> src [[texture(0)]],
                      constant TexK& K [[buffer(3)]]) {
  UpFS s; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uOut.y);
  s.uSrc = ClampTex{src, &K.kh[0]};
  s.main();
  return s.o;
}

// FINAL_FS. Up to the tone curve this is the web's pass. The web wrote the result
// to an 8-bit sRGB canvas, which clamps to [0, 1] and shows it as sRGB-encoded.
// Natively the same encoded value is converted to extended linear sRGB, and on an
// EDR screen the tone curve's shoulder rises past 1 toward the screen's peak.
struct FinalFS {
  constant FinalU* _p; ClampTex uScene, uBloomTex; Tex uLumCur, uLumAdapt; vec4 gl_FragCoord; vec4 o;

uvec2 pcg2d(uvec2 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  return v;
}
// Chroma-preserving soft-knee tonemapper so vivid hues stay rich without clipping.
vec3 tonemap(vec3 c) {
  float m = max(c.r, max(c.g, c.b));
  if (m > 0.7) c *= (0.7 + 0.3 * (1.0 - exp(-(m - 0.7) * 3.333))) / m;
  return c;
}
// The same knee, with the shoulder running to the encoded peak W instead of 1.
vec3 tonemapTo(vec3 c, float W) {
  float m = max(c.r, max(c.g, c.b));
  if (m > 0.7) c *= (0.7 + (W - 0.7) * (1.0 - exp(-(m - 0.7) / (W - 0.7)))) / m;
  return c;
}
vec3 srgbToLinear(vec3 c) {
  return select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045);
}

void main() {
  vec2 uv = gl_FragCoord.xy / uOut;
  vec2 d = uv - 0.5;
  vec2 ca = d * uChroma * 0.01;
  vec3 c = texture(uScene, uv).rgb;
  if (uChroma > 0.0) { c.r = texture(uScene, uv - ca).r; c.b = texture(uScene, uv + ca).b; }
  if (uBloom > 0.0) {
    // The glow splits like light through a prism: faintly everywhere, more toward
    // the edges, and further when the lens is set to fringe.
    vec2 pr = d * (0.006 + uChroma * 0.012);
    c += vec3(texture(uBloomTex, uv - pr).r, texture(uBloomTex, uv).g, texture(uBloomTex, uv + pr).b) * uBloom;
  }
  float hdr = 1.0;   // share of the HDR shoulder the flash guard allows (native)
  // Highlight-only flash limiter: absorbs sudden upward brightness spikes without pumping darks.
  if (uGuard) {
    float cur = texelFetch(uLumCur, ivec2(0), 0).r;
    float ad = texelFetch(uLumAdapt, ivec2(0), 0).r;
    float spike = max(0.0, cur - (ad * 1.18 + 0.04));
    c /= 1.0 + spike * 2.2;
    float g = 1.0 / (1.0 + spike * 2.2);
    // Brightness ceiling: past the knee, average brightness rolls off toward uCap.
    float L = max(cur, ad), knee = 0.6 * uCap;
    if (L > knee) c *= (knee + (uCap - knee) * (1.0 - exp(-(L - knee) / (uCap - knee)))) / L;
    if (L > knee) g *= (knee + (uCap - knee) * (1.0 - exp(-(L - knee) / (uCap - knee)))) / L;
    // Native: per channel, SDR shows at most uKs * x in linear light and the HDR
    // shoulder adds at most uG * x. Lx bounds the mean of x after the guard, so
    // limiting hdr this way keeps the screen's mean linear light at or below
    // uKs * uExposure * uCap: the most the web can ever show under the same ceiling.
    float Lx = cur * g * uExposure * uBloomMeanK;
    hdr = clamp(uKs * (uExposure * uCap - Lx) / max(uG * Lx, 1e-9), 0.0, 1.0);
  }
  vec3 x = c * uExposure;
  c = tonemap(x);
  if (uPeak > 1.0) c = mix(c, tonemapTo(x, uPeak), hdr);
  float v = length(d * vec2(uOut.x / uOut.y, 1.0));
  c *= 1.0 - uVignette * smoothstep(0.35, 1.15, v);
  float n = float(pcg2d(uvec2(gl_FragCoord.xy) + uvec2(uFrame * 1973u, uFrame * 9277u)).x) / 4294967295.0;
  c += (n - 0.5) * uGrain;
  c = clamp(c, 0.0, max(uPeak, 1.0));   // the web canvas clamps to [0, 1]
  if (uLinearOut) c = srgbToLinear(c);
  o = vec4(c, 1.0);
}
};

fragment float4 final_fs(VSOut in [[stage_in]], constant FinalU& U [[buffer(1)]],
                         texture2d<float> scene [[texture(0)]], texture2d<float> bloom [[texture(1)]],
                         texture2d<float> lumCur [[texture(2)]], texture2d<float> lumAd [[texture(3)]],
                         constant TexK& K [[buffer(3)]]) {
  FinalFS s; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uOut.y);
  s.uScene = ClampTex{scene, &K.kh[0]}; s.uBloomTex = ClampTex{bloom, &K.kh[1]};
  s.uLumCur = clampTex(lumCur); s.uLumAdapt = clampTex(lumAd);
  s.main();
  return s.o;
}

// LUM_FS
fragment float4 lum_fs(VSOut in [[stage_in]], texture2d<float> src [[texture(0)]], constant TexK& K [[buffer(3)]]) {
  ClampTex uSrc = ClampTex{src, &K.kh[0]};
  float s = 0.0;
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
    s += textureLod(uSrc, (vec2(x, y) + 0.5) / 8.0, 0.0).a;
  }
  return vec4(s / 64.0);
}

// ADAPT_FS
fragment float4 adapt_fs(VSOut in [[stage_in]], constant AdaptU* _p [[buffer(1)]],
                         texture2d<float> prev [[texture(0)]], texture2d<float> cur [[texture(1)]]) {
  Tex uPrev = clampTex(prev), uCur = clampTex(cur);
  // As the web's ADAPT_FS: a value that is not a number is dropped, or it would stay
  // here for good and dim every chamber after it to black.
  vec4 p = texelFetch(uPrev, ivec2(0), 0), c = texelFetch(uCur, ivec2(0), 0);
  bool pk = all(abs(p) < 1e6), ck = all(abs(c) < 1e6);
  return !pk ? (ck ? c : vec4(0.0)) : !ck ? p : mix(p, c, uK);
}

// COPY_FS
fragment float4 copy_fs(VSOut in [[stage_in]], constant CopyU* _p [[buffer(1)]], texture2d<float> src [[texture(0)]],
                        constant TexK& K [[buffer(3)]]) {
  ClampTex uSrc = ClampTex{src, &K.kh[0]};
  vec4 gl_FragCoord = glFragCoord(in.position, uOut.y);
  return texture(uSrc, gl_FragCoord.xy / uOut);
}

// Pixel-exact copy of the capture texture to the drawable (native; works with framebufferOnly).
fragment float4 blit_fs(VSOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
  return src.read(uint2(in.position.xy));
}
