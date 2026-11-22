// STARS: the sky places. Points live in one wedge of the mirrors and are drawn once
// into wedge textures that the scene pass folds out (design notes: next.md, "Sky places").
// sKind: 0 galaxy, 1 nebula, 2 pulsar, 3 quasar, 4 supernova, 5 aurora.

constant float PI_S = 3.14159265, TAU_S = 6.2831853;

inline float2 starHash(uint2 v, uint s) {
  v = v * 1664525u + 1013904223u + s;
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  v.x += v.y * 1664525u; v.y += v.x * 1664525u;
  v = v ^ (v >> 16u);
  return float2(v) * (1.0 / 4294967295.0);
}
inline float2 starDir(float a) { return float2(cos(a), sin(a)); }
inline float2 starRot(float2 p, float a) { float c = cos(a), s = sin(a); return float2(c * p.x - s * p.y, s * p.x + c * p.y); }
// The speed of a circular orbit at radius r around mass m, with the same softening as the pull.
inline float starP15(float x) { return x * sqrt(x); }   // x^1.5, without pow's log and exp
inline float starOrbit(float m, float r) { return sqrt(m * r * r / starP15(r * r + 0.0009)); }

// Folds a point (and its velocity) into the wedge. Mirrors core/state.h: star_fold.
inline void starFold(float sym, bool mirror, thread float2& x, thread float2& v) {
  if (dot(x, x) < 1e-12) return;                    // at the very middle: nothing to fold (and atan2(0, 0) is undefined in GLSL)
  float seg = TAU_S / sym, a = atan2(x.y, x.x), r = length(x);
  float k = floor(a / seg);
  a -= k * seg;
  v = starRot(v, -k * seg);
  if (mirror && a > seg * 0.5) {
    a = seg - a;
    float c2 = cos(seg), s2 = sin(seg);
    v = float2(c2 * v.x + s2 * v.y, s2 * v.x - c2 * v.y);
  }
  x = r * float2(cos(a), sin(a));
}
// A finger folded into the wedge, and its two nearest reflections (across each wall),
// so a point near a mirror feels the finger on both sides of it. j = 0, 1, 2.
inline float2 starImage(float2 w, int j, float sym, bool mirror) {
  if (j == 0) return w;
  float seg = TAU_S / sym;
  if (!mirror) return starRot(w, j == 1 ? seg : -seg);
  if (j == 1) return float2(w.x, -w.y);
  float c2 = cos(seg), s2 = sin(seg);
  return float2(c2 * w.x + s2 * w.y, s2 * w.x - c2 * w.y);
}

// A flow with no sources or sinks that is the same in every reflection: the curl of a
// rolling potential that is zero along every mirror, so the mirrors are streamlines and
// the flow passes from one reflection to the next without a seam. It blooms in petals.
inline float starPsi(float2 p, float t, float sym) {
  float r = length(p), a = atan2(p.y, p.x);
  float sa = sin(sym * a), ca = cos(sym * a);
  return sa * (sin(4.1 * r - 0.37 * t + 1.4 * sin(2.3 * r + 0.19 * t + ca))
             + 0.6 * sin(7.3 * r + 0.29 * t + 2.0 * ca)
             + 0.35 * sin(11.0 * r - 0.5 * t) * ca) * smoothstep(0.0, 0.25, r);
}
inline float2 starWind(float2 p, float t, float sym) {
  const float e = 0.004;
  return float2(starPsi(p + float2(0, e), t, sym) - starPsi(p - float2(0, e), t, sym),
                starPsi(p - float2(e, 0), t, sym) - starPsi(p + float2(e, 0), t, sym)) / (2.0 * e);
}
// The aurora's curtains: rings round the pole that ripple, the same in every reflection.
inline float starCurtain(float a, float band, float t, float sym) {
  return 0.24 + 0.27 * band + 0.05 * sin(t * 0.21 + band * 1.7)
       + 0.07 * sin(t * 0.43 + band * 2.3) * cos(sym * a)
       + 0.035 * sin(t * 0.71 + band) * cos(2.0 * sym * a)
       + 0.02 * sin(t * 1.3 - band * 3.0) * cos(3.0 * sym * a);
}

// The aurora's rays: 48 in each wedge, each with its own pace. ray() says which ray an
// angle in the wedge belongs to, up() how fast that ray's points leave the curtain now.
constant float AURORA_RAYS = 48.0;
inline float2 auroraRayHash(float ang, float wedge, float band) {
  float ray = floor(clamp(ang / wedge, 0.0, 0.9999) * AURORA_RAYS);
  return starHash(uint2(uint(ray), uint(band) + 11u), 5u);
}
inline float auroraUp(float2 rh, float t) {
  return max((0.12 + 0.5 * rh.y) * (0.6 + 0.4 * sin(t * (0.3 + 0.5 * rh.x) + rh.y * 6.283)), 0.06);
}

// How much tighter a finger's reach is drawn: a finger acts at every reflection, so the
// more reflections, the closer together they sit, and a reach sized for a few would join
// up into a whole ring. Scales the exponent of each reach (1 up to eight reflections).
inline float starReachK(float sym, bool mirror) { float n = (mirror ? 2.0 : 1.0) * sym / 8.0; return max(1.0, n * n); }

fragment float4 stars_step_fs(VSOut in [[stage_in]], constant StarsU& u [[buffer(1)]], texture2d<float> prev [[texture(0)]]) {
  uint2 px = uint2(in.position.xy);
  float4 s = prev.read(px);
  float2 x = s.xy, v = s.zw;
  const float dt = u.sDt, t = u.sTime, sym = u.sSym;
  const bool mir = u.sMirror != 0;
  const int kind = u.sKind;
  float2 h = starHash(px, uint(u.sSeed));
  float2 g = starHash(px + uint2(17u, 59u), uint(u.sSeed) * 7u + 3u);

  // The place's own core sits at the middle.
  float2 a = float2(0.0);
  const float soft = kind == 4 ? 0.02 : 0.0009;      // supernova: matter gathers as a ball, not a point
  for (int i = 0; i < u.sAuto; i++) a += -x * (u.sCore[i].z / starP15(dot(x, x) + soft));

  // Every finger, at each of its reflections.
  float nearest = 1e9;
  const float rk = starReachK(sym, mir);
  for (int i = u.sAuto; i < u.sCores; i++) {
    const bool hover = u.sCore[i].w > 0.5;
    for (int j = 0; j < 3; j++) {
      float2 w = starImage(u.sCore[i].xy, j, sym, mir), fv = starImage(u.sVel[i].xy, j, sym, mir);
      float2 d = w - x;
      float r2 = dot(d, d);
      nearest = min(nearest, r2);
      // The pull. Close in, it is softened so a point falls through a finger rather than
      // being flung off at once.
      a += d * (u.sCore[i].z / starP15(r2 + (kind == 4 ? 0.02 : 0.004)));
      // A moving finger carries a wide handful of what is around it (on the pulsar it
      // only aims: the beams are left alone).
      float reach = (hover ? 22.0 : 12.0) * rk;
      // (On the aurora a finger does not carry: the rays must keep climbing.)
      if (kind != 2 && kind != 5) v += (fv * 0.95 - v) * (exp(-r2 * reach) * min(1.0, dt * (hover ? 5.0 : 9.0)));
      // Each sky's own answer.
      float near = exp(-r2 * (hover ? 30.0 : 16.0) * rk), turn = hover ? 0.5 : 1.0;
      float2 around = float2(-d.y, d.x) * rsqrt(r2 + 1e-4);
      if (kind == 0) v += around * (0.5 * turn * near * dt);                    // a sun: what falls in is set circling
      else if (kind == 1) v += (around * 1.6 + d * 0.9) * (turn * near * dt);   // a whirlpool: gas winds in spirals
      else if (kind == 3) v += around * (0.6 * turn * near * dt);
    }
  }
  // A burst throws everything near it outward, at each of its reflections.
  if (u.sBlast.z > 0.0) {
    for (int j = 0; j < 3; j++) {
      float2 d = x - starImage(u.sBlast.xy, j, sym, mir);
      float r = length(d) + 1e-3;
      float2 out = normalize(d + starDir(TAU_S * g.y) * 0.1);   // matter packed into one spot still flies every way
      // Three speeds, so a burst opens as three shells, one inside the next.
      // Elsewhere a tap is a smaller thing: a ring thrown from under the finger.
      float shell = kind == 4 ? 0.4 + 0.3 * floor(h.y * 3.0) + 0.04 * g.x : 0.45 + 0.1 * h.y;
      v += out * (u.sBlast.z * shell * (kind == 4 ? 0.6 : 0.55) * exp(-r * r * (kind == 4 ? 2.5 : 16.0)));
    }
  }
  // Lost to rounding (NaN or out of range): reborn. Tested by comparison, which every
  // compiler keeps; isnan may be folded away where a GPU's maths assumes no NaN.
  bool reborn = u.sReset != 0 || !(abs(x.x) < 1e4 && abs(x.y) < 1e4 && abs(v.x) < 1e4 && abs(v.y) < 1e4);
  const float seg = TAU_S / sym, wedge = mir ? seg * 0.5 : seg;

  if (kind == 0) {
    // A heart and a dark halo, so the outer stars keep turning nearly as fast as the
    // inner ones and the arms wind slowly instead of tearing.
    a += -x * (0.035 / (dot(x, x) + 0.01));
    v += a * dt; x += v * dt;
    float r = length(x);
    if (reborn || r > 2.4 || r < 0.012 || nearest < 0.0002 || h.x < dt * 0.2) {   // short lives, so the young stars draw the arms
      // Born in the disc, most along its two spiral arms, which turn slowly as a pattern.
      float rr = 0.035 + 1.05 * pow(g.x, 1.6);
      float arm = 2.1 * log(rr / 0.035) - t * 0.09;   // one arm in each turn of the chamber
      float ang = fract(g.y * 7.0) < 0.85 ? arm + (g.y - 0.5) * (0.18 + 0.3 * rr) : TAU_S * g.y;
      float2 dir = starDir(ang);
      x = dir * rr;
      float vc = sqrt(0.03 * rr * rr / starP15(rr * rr + 0.0009) + 0.035 * rr * rr / (rr * rr + 0.01));
      v = float2(-dir.y, dir.x) * vc * (0.97 + 0.06 * h.y);
    }
  } else if (kind == 1) {
    // Gas carried on the petal flow, which breathes: it opens and closes with the sky's pulse.
    float2 wind = starWind(x, t * (0.8 + 0.4 * u.sPulse), sym) * 0.035;
    v += (wind - v) * min(1.0, dt * 5.0);          // close on the flow, so the gas draws its streamlines
    v += a * dt;
    x += v * dt;
    if (reborn || length(x) > u.sReach || h.x < dt * 0.03) {
      float rr = u.sReach * sqrt(g.x), ang = wedge * g.y;
      x = rr * starDir(ang);
      v = float2(0.0);
    }
  } else if (kind == 2) {
    // Where the beams point, how hard they shine and how fast they turn: the finger's to give.
    const float aim = u.sCore[0].w, power = u.sVel[0].x, spin = u.sVel[0].y;
    v += a * dt; x += v * dt;
    bool ring = (px.y & 15u) == 0u;
    if (reborn || dot(x, x) > u.sReach * u.sReach || (dot(x, x) < 0.0004 && !ring) || (ring && h.x < dt * 0.3)) {
      if (ring) {                                    // a few of them circle the star close in
        float rr = 0.05 + 0.1 * g.x;
        float2 dir = starDir(TAU_S * g.y);
        x = dir * rr; v = float2(-dir.y, dir.x) * starOrbit(u.sCore[0].z, rr);
      } else {                                       // the rest leave along one of the two beams
        float age = u.sReset != 0 ? g.x * 6.0 : 0.0; // at the start, as if it had been shining a while
        float speed = (0.2 + 0.24 * power) * (0.99 + 0.02 * g.y);   // slow enough that the beams wind round several times, and all
                                                                      // at one pace, or the spirals would smear
        float2 dir = starDir(aim - age * spin + float(px.x & 1u) * PI_S + (h.y - 0.5) * 0.08);
        x = dir * (0.02 + speed * age); v = dir * speed;
      }
    }
  } else if (kind == 3) {
    float speed = length(v);
    v += a * dt;
    if (speed < 1.0) v *= 1.0 - dt * 0.3;            // the disc loses speed, so each orbit is a little lower; the jets fly free
    x += v * dt;
    if (dot(x, x) < 0.0022 && !reborn) {             // swallowed: thrown out along the axis
      float2 dir = starDir(u.sCore[0].w + (g.x - 0.5) * 0.06) * ((px.x & 1u) == 0u ? 1.0 : -1.0);
      x = dir * 0.06; v = dir * (1.9 + 0.4 * g.y);   // faster than the hole's pull, so the jets get away
    } else if (reborn || dot(x, x) > 7.0 || (speed < 1.0 && h.x < dt * 0.06)) {
      // Fed in at the rim along one spiral lane in each turn of the chamber; the drag
      // winds it inward over a few orbits.
      float rr = 0.22 + 0.7 * sqrt(g.x);
      float lane = -1.6 * log(rr / 0.2) + u.sTime * 0.15;
      float ang = fract(g.y * 5.0) < 0.7 ? lane + (h.y - 0.5) * 0.5 : TAU_S * g.y;
      float2 dir = starDir(ang);
      x = dir * rr; v = float2(-dir.y, dir.x) * starOrbit(u.sCore[0].z, rr) * (0.92 + 0.08 * h.y);
    }
  } else if (kind == 4) {
    v += a * dt;
    v *= 1.0 - min(1.0, dt * 0.22);
    x += v * dt;
    if (reborn || dot(x, x) > 4.4) {
      float rr = 0.04 + 0.3 * sqrt(g.x);
      float2 dir = starDir(TAU_S * g.y);
      x = dir * rr; v = float2(-dir.y, dir.x) * (0.95 * starOrbit(u.sCore[0].z, rr));
    }
  } else {
    // Each point is one step along a ray: it leaves a curtain, climbs outward, slows and
    // fades. The rays are fine, separate lines, each with its own height and pace.
    float r = length(x) + 1e-4;
    float2 out = x / r;
    v *= 1.0 - min(1.0, dt * 0.55);
    v += a * dt;
    x += v * dt;
    if (reborn || dot(v, out) < 0.07 || r > u.sReach) {   // gone before it slows to a crawl: a faded ray tip costs as much to draw as a bright one
      float band = float(px.y % 3u), ray = floor(g.x * AURORA_RAYS);
      float2 rh = starHash(uint2(uint(ray), uint(band) + 11u), 5u);
      float ang = wedge * (ray + 0.5 + 0.18 * (rh.x - 0.5)) / AURORA_RAYS + (h.x - 0.5) * 0.0015;
      float rb = starCurtain(ang, band, t, sym);
      // A curtain reaches for a finger near it.
      for (int i = u.sAuto; i < u.sCores; i++) {
        for (int j = 0; j < 3; j++) {
          float2 w = starImage(u.sCore[i].xy, j, sym, mir);
          float rw = length(w), across = length(w - rw * starDir(ang));
          float pull = exp(-across * across * (u.sCore[i].w > 0.5 ? 40.0 : 18.0) * starReachK(sym, mir)) * exp(-abs(rw - rb) * 2.2);
          rb = mix(rb, rw, (u.sCore[i].w > 0.5 ? 0.6 : 0.95) * pull);
        }
      }
      // Each ray climbs at its own pace, which drifts, so the crown flickers slowly.
      float up = auroraUp(rh, t) * (0.9 + 0.2 * h.y);
      float age = u.sReset != 0 ? g.y : 0.0;         // at the start, rays of every height at once
      float2 dir = starDir(ang);
      x = dir * (rb + (h.y - 0.5) * 0.01 + up * age * 1.2);
      v = dir * up * (1.0 - 0.7 * age);
    }
  }
  starFold(sym, mir, x, v);
  return float4(x, v);
}

struct StarOut {
  float4 position [[position]];
  float size [[point_size]];
  float3 col;
  float spike;                                       // > 0: a giant or a glint, drawn with four rays
  float head;
  float px;                                          // the point's size again: [[point_size]] does not reach the fragment
  float sig;                                         // the sharp core's width, in pixels
};
// A point this pass does not draw: off screen, before any of the work.
inline StarOut starOff() { StarOut o; o.position = float4(3.0, 3.0, 0.0, 1.0); o.size = 1.0; o.col = float3(0.0); o.spike = o.head = o.px = 0.0; o.sig = 1.0; return o; }

vertex StarOut stars_vs(uint vid [[vertex_id]], constant StarsU& u [[buffer(1)]], texture2d<float> st [[texture(0)]]) {
  uint w = uint(u.sDim.x);
  uint2 px = uint2(vid % w, vid / w);
  // Each pass draws only a share of the points: the first so many texels, which are as
  // good a sample as any (a point's texel says nothing of where it is). The renderer
  // draws just that many (star_share in sky_layout.h), so no vertex is spent on a point
  // that is not drawn: a GPU runs vertices in groups, and a group with one survivor
  // costs as much as a full one. The sharp texture takes 10 to 30 in a hundred; the
  // soft one, where points stack, fewer than all, and on a phone (sEchoKeep) fewer again.
  // (Written as choices, not tables: some phone GPUs misread a constant table indexed by
  // a variable in a vertex shader.)
  const bool head = u.sHead != 0;
  const int kind = u.sKind;
  float2 h = starHash(px, 7u);
  float share = head ? (kind == 1 ? 0.1 : kind == 4 ? 0.2 : 0.3) : (kind >= 4 ? 0.3 : 1.0) * u.sEchoKeep;
  float4 s = st.read(px);
  if (!(abs(s.x) < 1e4 && abs(s.y) < 1e4 && abs(s.z) < 1e4 && abs(s.w) < 1e4)) s = float4(9.0, 9.0, 0.0, 0.0);   // lost: far off screen
  const float zoom = u.sView.x, sym = u.sSym;
  const bool mir = u.sMirror != 0;
  float speed = length(s.zw), r = length(s.xy);
  // How near this point is to a finger (at any reflection), and to a hovering one.
  float touch = 0.0;
  const float rk = starReachK(sym, mir);
  for (int i = u.sAuto; i < u.sCores; i++) {
    for (int j = 0; j < 3; j++) {
      float d2 = distance_squared(s.xy, starImage(u.sCore[i].xy, j, sym, mir));
      touch = max(touch, exp(-d2 * (u.sCore[i].w > 0.5 ? 40.0 : 22.0) * rk) * (u.sCore[i].w > 0.5 ? 0.75 : 1.0));
    }
  }
  // Where on the palette this point sits, how white-hot it is, how big and how bright.
  float tone = h.x * 0.3 + speed * 0.45, hot = 0.15, size = 0.8 + 1.0 * h.y * h.y * h.y, gain = 0.35 + 0.65 * h.y * h.y;
  float giant = 0.0;
  // Where thousands of points sit on the same few pixels (a galaxy's heart, a crowd on a
  // finger) drawing them all only burns out to white. There, each point is drawn with
  // this chance: the crowd keeps its shape and colour at a fraction of the work.
  float keep = 1.0;
  const float pulse = u.sPulse;
  if (kind == 0) {
    // Warm at the heart, cooler out along the arms. The arms are where the light is: a
    // star shines brighter and bluer as it passes through one, as young stars do, so the
    // spiral holds however much the disc has wound.
    float seg = TAU_S / sym, arm = 2.1 * log(max(r, 0.035) / 0.035) - u.sTime * 0.09;
    float off = atan2(s.y, s.x) - arm;
    off -= seg * floor(off / seg + 0.5);
    float onArm = exp(-off * off / (0.04 + 0.05 * r)) * smoothstep(0.05, 0.2, r);
    tone = 0.06 + 0.75 * min(r, 0.9) + h.x * 0.12 + 0.12 * onArm;
    keep = mix(0.08, 1.0, smoothstep(0.0, 0.25, r));
    hot = 0.1 + 0.5 * smoothstep(0.15, 0.0, r) + 0.2 * onArm;
    gain *= 0.4 + 1.4 * onArm + 0.6 * smoothstep(0.25, 0.0, r);
    if (h.x > 0.985) { giant = 1.0; gain *= 2.5; hot = 0.55; }
    gain *= 1.0 + 0.25 * pulse;
  } else if (kind == 1) {
    tone = 0.18 * starPsi(s.xy, u.sTime, sym) + 0.4 * r + h.x * 0.1;
    hot = 0.05 + clamp(speed * 2.5 - 0.15, 0.0, 0.6);
    gain *= (0.5 + 0.5 * pulse) * (0.7 + 2.0 * clamp(speed * 3.0 - 0.45, 0.0, 1.0)); size *= 0.8;   // stirred gas glows
    if (h.x > 0.993) { giant = 1.0; gain *= 2.0; hot = 0.5; }
  } else if (kind == 2) {
    tone = 0.12 + h.x * 0.15 + 0.3 * r; hot = clamp(speed - 0.4, 0.0, 0.7); gain *= speed > 0.5 ? 1.6 : 0.8;
    gain *= mix(1.0, 0.3, smoothstep(0.9, 1.8, r));   // the beams thin out with distance
    // Every beam starts at the star, so near it thousands of points sit on a few texels
    // (their density falls as 1 / r): draw few of them there, each brighter.
    float thin = mix(0.03, 1.0, smoothstep(0.0, 0.6, r));
    keep = thin; gain *= mix(2.5, 1.0, smoothstep(0.0, 0.6, r));
  } else if (kind == 3) {
    tone = 0.05 + 0.55 * r + h.x * 0.1; hot = speed > 1.0 ? 0.5 : 0.6 * smoothstep(0.3, 0.05, r);   // white-hot within, red at the rim; the jets white
    if (speed > 1.0) { gain *= 2.4; size *= 1.2; } else { keep = mix(0.04, 1.0, smoothstep(0.03, 0.35, r)); }
  } else if (kind == 4) {
    tone = 0.05 + 0.55 * speed + h.x * 0.2; hot = clamp(speed * 0.35, 0.0, 0.5);   // each speed its own colour, so a burst opens in rings
    gain *= (0.12 + 2.9 * clamp(speed, 0.0, 1.0)) * mix(1.0, 0.25, smoothstep(0.6, 1.6, r));   // the shells, not the dust they leave
    // The gathered ball is thousands of points on a few pixels: a few of them draw it,
    // each brighter (the ball burns white either way).
    float ball = smoothstep(0.35, 0.0, r) * (1.0 - clamp(speed * 2.0, 0.0, 1.0));
    keep = mix(1.0, 0.06, ball) * mix(1.0, 0.35, smoothstep(0.5, 1.5, r) * (1.0 - clamp(speed * 3.0, 0.0, 1.0)));
    gain *= 1.0 + 4.0 * ball; size *= 1.0 - 0.4 * ball;
  } else {
    // Bright at the curtain, fading as the ray climbs: how much of the ray's launch speed
    // a point still has says how far it has come, wherever a finger has bent its curtain.
    float wedgeA = (mir ? 0.5 : 1.0) * TAU_S / sym;
    float up = auroraUp(auroraRayHash(atan2(s.y, s.x), wedgeA, float(px.y % 3u)), u.sTime) * (0.9 + 0.2 * h.y);
    float pace = dot(s.zw, s.xy / max(r, 1e-4)) / up;
    // Faster than its ray ever sends it: flung by a tap, so a faint spark, not a fresh ray.
    float life = pace <= 1.0 ? max(pace, 0.0) : exp(-(pace - 1.0) * 2.0);
    life *= life;
    tone = 0.17 + 0.5 * life + h.x * 0.04;           // green at the curtain, through blue to violet at the tips (rainbow palette)
    gain *= (0.2 + 1.2 * life) * (0.7 + 0.3 * pulse); hot = 0.3 * life * life * life;
  }
  // Life: every star twinkles at its own pace, its colour shimmers a little, and now and
  // then one glints: a brief flare with four rays. A star twinkles in step in all its
  // reflections, so the mirrors stay true.
  const float t = u.sTime;
  float tw = 0.5 + 0.5 * sin(t * (1.3 + 3.7 * h.x) + 40.0 * h.y);
  tone += 0.03 * sin(t * 0.7 + h.x * 30.0);
  float glint = 0.0;
  if (head && kind != 5) {
    float ph = fract(t * (0.07 + 0.11 * h.x) + h.y * 7.0);
    glint = exp(-(ph - 0.5) * (ph - 0.5) * 900.0) * step(0.88, fract(h.x * 53.1));   // about one star in a hundred at any moment
    gain *= (0.6 + 0.4 * tw * tw) * (1.0 + 3.0 * glint);
    hot = max(hot, glint * 0.8);
    giant = max(giant, glint > 0.3 ? 1.0 : 0.0);
  }
  // The soft pass is for what moves: a star sitting still draws no smear under its sharp
  // point. The gas and the curtains are drawn by their soft light, so they keep it all.
  if (!head && kind != 1 && kind != 5) gain *= mix(0.25, 1.0, smoothstep(0.03, 0.3, speed));
  // A finger answers by bending the light (the scene pass's lens), not by flaring. Near
  // it, where its pull gathers a crowd, every sky draws fewer points, each a little
  // brighter and smaller: a crowd on a few pixels costs much and shows nothing more.
  const bool crowds = kind == 0 || kind == 3 || kind == 4;
  gain *= rsqrt(1.0 - (crowds ? 0.93 : 0.75) * touch);
  if (kind == 4) { gain *= 1.0 + 1.5 * touch; hot = max(hot, 0.4 * touch); }   // the supernova's gathering star glows as it charges
  if (crowds) size *= 1.0 - 0.4 * touch;
  keep *= 1.0 - (crowds ? 0.93 : 0.75) * touch;
  keep = min(1.0, keep * max(zoom * zoom, 0.4));   // zoomed in, a crowd covers more pixels and can show more of itself; zoomed out, it keeps enough to read as a sky
  if (gain < 0.05) keep = 0.0;                      // too faint to see: not worth the pixels it would cost
  gain *= clamp(pow(zoom, 1.1), 1.0, 4.0);         // zoomed in, the same points spread over more of the screen (zoomed out, fewer are drawn instead: see keep)
  float tt = tone + u.sPalShift;
  float3 c = max(u.sPalA + u.sPalB * cos(TAU_S * (u.sPalC * tt + u.sPalD)), float3(0.0));

  // The sharp texture's share of the points is each brighter, so the sky reads as single
  // stars over the soft light of the rest rather than a haze; the soft one's share is
  // made brighter to make up for the points it left out.
  float echoGain = kind == 1 ? 1.8 : kind == 2 ? 0.55 : kind == 5 ? 0.7 : 1.0;   // how much each sky leans on its soft light
  if (!head) gain /= share;
  float psz = (head ? u.sHeadSize * (giant > 0.0 ? 5.0 : 1.0) : u.sSize) * size * u.sTexK;   // in the target's texels
  // Drawn once, whole, even where its sprite spills over a wall: the scene pass reads
  // that spill from across the wall (scene.metal, skyLight).
  float2 uv = (s.xy - u.sBB.xy) / (u.sBB.zw - u.sBB.xy);
  StarOut o;
  o.col = mix(c, float3(1.0), hot) * gain * (head ? u.sHeadBright : u.sBright * echoGain);
  o.position = fract(h.x * 91.7 + h.y * 13.3) < keep ? float4(uv * 2.0 - 1.0, 0.0, 1.0) : float4(3.0, 3.0, 0.0, 1.0);
  o.size = psz;
  o.spike = giant;
  o.px = psz;
  o.sig = u.sPx * u.sTexK * (0.45 + 0.2 * size);    // a crisp core: under a pixel and a half across on a 1000-pixel screen
  o.head = head ? 1.0 : 0.0;
  return o;
}

fragment float4 stars_fs(StarOut in [[stage_in]], float2 pc [[point_coord]]) {
  float2 q = pc - 0.5;
  if (in.head < 0.5) {                               // into the echo: a soft dab
    float k = max(1.0 - dot(q, q) * 4.0, 0.0);
    return float4(in.col * (k * k), 1.0);
  }
  // A sharp point: a core whose width is fixed in screen pixels, a faint halo, and on the
  // giants and glints four fine rays. Measured from the true centre, so it stays crisp and
  // steady wherever it falls between pixels.
  float2 d = q * in.px;
  float edge = max(1.0 - dot(q, q) * 4.0, 0.0);
  float sig2 = in.sig * in.sig;
  float core = exp(-dot(d, d) / (2.0 * sig2));
  float halo = 0.05 * exp(-length(d) / (2.5 * in.sig)) * edge;
  float rays = 0.0;
  if (in.spike > 0.0) {
    float2 ad = abs(d) / in.sig;
    rays = (exp(-ad.y * 1.4) * exp(-ad.x * 0.16) + exp(-ad.x * 1.4) * exp(-ad.y * 0.16)) * 0.4 * edge * edge;
  }
  return float4(in.col * (core + halo + rays), 0.0);   // alpha untouched: the bloom chain keeps the scene's mean brightness there
}

// The soft light, carried from frame to frame: each texel of the trail texture reads
// where its light was last frame (turned and zoomed back by the trails' own motion, in
// last frame's extent) and fades it. The points are then added.
constexpr sampler starLinear(filter::linear, address::clamp_to_edge);
fragment float4 stars_trail_fs(VSOut in [[stage_in]], constant StarsU& u [[buffer(1)]], texture2d<float> prev [[texture(0)]]) {
  float2 uv = float2(in.position.x, u.sRes.y - in.position.y) / u.sRes;
  float2 z = u.sBB.xy + (u.sBB.zw - u.sBB.xy) * uv;
  float2 w = float2(u.sFb.x * z.x + u.sFb.y * z.y, u.sFb.x * z.y - u.sFb.y * z.x) / u.sFb.z;
  // Inside the wedge, light turned out over a wall comes back in from its other side (it
  // is folded in again). Outside it, in the margin past a wall, a texel holds only what
  // spilled over that wall, which the scene pass reads from across it: no folding there,
  // or that light would be counted twice.
  const float wedgeT = (u.sMirror != 0 ? 0.5 : 1.0) * TAU_S / u.sSym, az = atan2(z.y, z.x);
  if (az >= 0.0 && az <= wedgeT) {
    float2 unused = float2(0.0);
    starFold(u.sSym, u.sMirror != 0, w, unused);
  }
  float2 q = (w - u.sBBPrev.xy) / (u.sBBPrev.zw - u.sBBPrev.xy);
  float3 c = float3(0.0);
  if (q.x >= 0.0 && q.y >= 0.0 && q.x <= 1.0 && q.y <= 1.0) c = prev.sample(starLinear, float2(q.x, 1.0 - q.y)).rgb * u.sFb.w;
  if (!(c.r < 1e4 && c.g < 1e4 && c.b < 1e4)) c = float3(0.0);
  return float4(c, 1.0);
}
