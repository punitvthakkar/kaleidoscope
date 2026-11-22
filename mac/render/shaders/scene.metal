// SCENE_FS: 4-tap supersampled mip-aware chamber fold + orbit-trap filigree + optical veil dissolve.

struct SceneFS {
  constant CommonU* _c; constant SceneU* _p; vec4 gl_FragCoord;
  MirrorTex uFB; ClampTex uVeilTex; Tex uSkySharp, uSkySoft;
  vec4 o;
//@COMMON

// The sky places: their light is drawn once, into textures covering one wedge of the
// mirrors in the sky's own space (mac/render/shaders/stars.metal); here every pixel is
// turned and folded into that wedge and reads it. Anything that is not a number is
// dropped (tested by comparison: some GPUs fold isnan away).
vec3 skyAt(vec2 q) {
  vec2 uv = (q - uSkyBox.xy) / (uSkyBox.zw - uSkyBox.xy);
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return vec3(0.0);
  return texture(uSkySharp, uv).rgb + texture(uSkySoft, uv).rgb;
}
vec3 skyLight(vec2 p) {
  p *= 1.0 + uFisheye * dot(p, p) * 0.3;
  vec2 d = (p - uSkyAt.xy - uSkyAt.zw) * uSkyView.z;
  vec2 z = vec2(uSkyView.x * d.x + uSkyView.y * d.y, uSkyView.x * d.y - uSkyView.y * d.x);
  float seg = TAU / uSkyView.w, a = atan(z.y, z.x), r = length(z);
  a -= seg * floor(a / seg);
  if (uSkyMirror != 0 && a > seg * 0.5) a = seg - a;
  vec2 q = r * vec2(cos(a), sin(a));
  // Every finger bends the light behind it as a mass bends space: what is seen at q came
  // from q - E^2 (q - L) / |q - L|^2 for a lens at L of Einstein radius E (softened at its
  // heart), and from each of its images across the wedge's walls. Stars passing behind a
  // fingertip stretch into arcs and close into a ring. It is then folded in again.
  float pupil = 1.0;
  if (uSkyLensN > 0) {
    vec2 dq = vec2(0.0);
    float near = 1e9;
    float c2 = cos(seg), s2 = sin(seg);
    for (int i = 0; i < 8; i++) {
      if (i >= uSkyLensN) break;
      vec4 L = uSkyLens[i];
      vec2 l1 = uSkyMirror != 0 ? vec2(L.x, -L.y) : vec2(c2 * L.x - s2 * L.y, s2 * L.x + c2 * L.y);
      vec2 l2 = uSkyMirror != 0 ? vec2(c2 * L.x + s2 * L.y, s2 * L.x - c2 * L.y) : vec2(c2 * L.x + s2 * L.y, c2 * L.y - s2 * L.x);
      vec2 d0 = q - L.xy, d1 = q - l1, d2 = q - l2;
      float e0 = dot(d0, d0), e1 = dot(d1, d1), e2 = dot(d2, d2), far = 36.0 * L.z;
      // Out to six Einstein radii, fading at the edge: beyond, a pixel spends nothing on it.
      if (min(e0, min(e1, e2)) > far) continue;
      float soft = 0.06 * L.z;
      dq += L.z * (d0 * (smoothstep(far, 0.25 * far, e0) / (e0 + soft)) + d1 * (smoothstep(far, 0.25 * far, e1) / (e1 + soft))
                 + d2 * (smoothstep(far, 0.25 * far, e2) / (e2 + soft)));
      near = min(near, min(e0, min(e1, e2)) / max(L.z, 1e-6));
    }
    // Inside the ring a lens shows only a faint, shrunken image of the sky behind it: a
    // soft dark pupil at the fingertip, framed by the bright ring.
    pupil = smoothstep(0.12, 0.55, near);
    if (near < 36.0) {
      q -= dq;
      r = length(q); a = atan(q.y, q.x);
      a -= seg * floor(a / seg);
      if (uSkyMirror != 0 && a > seg * 0.5) a = seg - a;
      q = r * vec2(cos(a), sin(a));
    }
  }
  vec3 s = skyAt(q) * pupil;
  // Each star is drawn once, on its own side of a wall; within a sprite's reach of a
  // wall, the stars just across it spill over: read them where they were drawn.
  float wall = uSkyMirror != 0 ? seg * 0.5 : seg, reach = uSkyBox.z * 0.029;
  if (q.y < reach) s += pupil * skyAt(uSkyMirror != 0 ? vec2(q.x, -q.y) : r * vec2(cos(a + seg), sin(a + seg)));
  if (r * sin(wall - a) < reach) s += pupil * skyAt(uSkyMirror != 0 ? r * vec2(cos(seg - a), sin(seg - a)) : r * vec2(cos(a - seg), sin(a - seg)));
  return s.r < 1e4 && s.g < 1e4 && s.b < 1e4 ? s : vec3(0.0);
}


vec3 evalPoint(vec2 p) {
  p *= 1.0 + uFisheye * dot(p, p) * 0.3;
  float trap, scale, diskMask, rimGlow;
  vec2 q = foldSpaceEx(p, trap, scale, diskMask, rimGlow);
  float lod = clamp(log2(max(scale * 0.65 * uFbScale, 1.0)), 0.0, 5.0);
  vec3 c = textureLod(uFB, toUV(q), lod).rgb * diskMask;

  // Delicate anti-aliased filigree along fractal, hyperbolic, spiral and tile fold lines
  if (uMode >= 2) {
    float px = max(2.0 / uRes.y, 0.0015);
    float fil = exp(-trap / (px * 1.6)) * diskMask;
    float energy = dot(c, vec3(0.333));
    vec3 filCol = pal(log(max(scale, 0.5)) * 0.2 + uPalShift + 0.25);
    c += filCol * fil * ((uMode == 8 ? 0.3 / scale : 0.045) + 0.16 * min(energy, 1.5));   // pearls are drawn by their outlines
  }
  if (uMode == 3 && rimGlow > 0.0) {
    c += pal(uPalShift + 0.5) * rimGlow * 0.22;
  }
  // The photon ring: always lit, and brighter with the light falling past it.
  // The planet's air: a thin glow around its edge.
  if (uMode == 12) c += pal(uPalShift + 0.5) * rimGlow * 0.7;
  // Each pearl sits in a soft halo of its own light.
  if (uMode == 8) c += pal(log(max(scale, 0.5)) * 0.2 + uPalShift + 0.4) * exp(-trap * 16.0) * (0.5 / (1.0 + scale));
  if (uMode == 6 || uMode == 7) {
    // Inside the glass ball the sky's colours are turned, so it reads as another place.
    if (uMode == 7 && length(p - uCenter) * uViewZoom < uDroste.x) c = c.brg;
    // Stars behind the lens: fixed in the sky, so they stretch into arcs as it moves.
    vec2 g = q * 7.0, h = hash2(floor(g) + 64.0);
    vec2 sd = fract(g) - 0.5 - 0.38 * h;
    float star = exp(-dot(sd, sd) * 2500.0) * step(-0.2, h.y) * (0.4 + 0.6 * fract(h.x * 7.3));
    c += mix(vec3(1.0), pal(h.x + uPalShift), 0.5) * star * diskMask * 1.5;
    c += pal(uPalShift + 0.5) * rimGlow * (0.5 + 1.2 * min(dot(c, vec3(0.333)), 2.0));
  }
  return c;
}

void main() {
  vec2 px = 2.0 / vec2(uRes.y);
  vec2 p0 = fragP();
  // Rotated-grid supersampling, only as many taps as the chamber and the screen need:
  // seamless folds take one, dense screens two, everything else four.
  vec3 c;
  if (uTaps <= 1) c = evalPoint(p0);
  else if (uTaps == 2) c = 0.5 * (evalPoint(p0 + px * 0.25) + evalPoint(p0 - px * 0.25));
  else {
    // Native, uTaps 8: the four taps, then where they disagree (a fold line finer than a
    // pixel) four more, mirrored: eight around the pixel where it shows, four elsewhere.
    vec3 a0 = evalPoint(p0 + px * vec2( 0.125,  0.375)), a1 = evalPoint(p0 + px * vec2(-0.375,  0.125));
    vec3 a2 = evalPoint(p0 + px * vec2( 0.375, -0.125)), a3 = evalPoint(p0 + px * vec2(-0.125, -0.375));
    c = 0.25 * (a0 + a1 + a2 + a3);
    if (uTaps > 4) {
      const vec3 Y = vec3(0.299, 0.587, 0.114);
      float l0 = dot(a0, Y), l1 = dot(a1, Y), l2 = dot(a2, Y), l3 = dot(a3, Y);
      float spread = max(max(l0, l1), max(l2, l3)) - min(min(l0, l1), min(l2, l3));
      if (uTaps > 8 || spread > 0.02 + 0.15 * dot(c, Y)) {
        c = 0.5 * c + 0.125 * (
          evalPoint(p0 + px * vec2(-0.125,  0.375)) + evalPoint(p0 + px * vec2( 0.375,  0.125)) +
          evalPoint(p0 + px * vec2(-0.375, -0.125)) + evalPoint(p0 + px * vec2( 0.125, -0.375)));
      }
    }
  }
  if (uSky != 0) c += skyLight(p0);
  // Liquid sheen: treat brightness as a height field and light it like oil on glass.
  if (uSheen > 0.0) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float h = l / (1.0 + l);
    vec2 g = vec2(dFdx(h), dFdy(h)) * uRes.y * 0.02;
    vec3 n = normalize(vec3(-g, 1.0));
    float spec = pow(max(dot(n, normalize(vec3(uSheenL, 0.75))), 0.0), 28.0);
    float fres = 1.0 - n.z;
    c += (pal(fres * 1.4 + h + uPalShift + 0.33) * fres * 0.7 + spec * 0.45) * uSheen * smoothstep(0.02, 0.3, l);
  }
  if (uPalMap > 0.0) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, pal(l * 0.7 + uPalShift) * (l * 1.3 + 0.02), uPalMap);
  }
  if (uVeil > 0.0) {
    vec2 uv = gl_FragCoord.xy / uRes;
    vec2 uvVeil = 0.5 + (uv - 0.5) * (1.0 - 0.035 * (1.0 - uVeil));
    c = mix(c, texture(uVeilTex, uvVeil).rgb, uVeil);
  }
  o = vec4(c, 1.0);
}
};

fragment float4 scene_fs(VSOut in [[stage_in]],
                         constant CommonU& C [[buffer(0)]], constant SceneU& U [[buffer(1)]],
                         texture2d<float> fb [[texture(0)]], texture2d<float> veil [[texture(1)]],
                         texture2d<float> skySharp [[texture(2)]], texture2d<float> skySoft [[texture(3)]],
                         constant TexK& K [[buffer(3)]]) {
  SceneFS s;
  s._c = &C; s._p = &U;
  s.gl_FragCoord = glFragCoord(in.position, s.uRes.y);
  s.uFB = MirrorTex{fb, &K.kh[0], &K.maxLod};
  s.uVeilTex = ClampTex{veil, &K.kh[1]};
  s.uSkySharp = clampTex(skySharp);
  s.uSkySoft = clampTex(skySoft);
  s.main();
  return s.o;
}
