// INK_VS / INK_FS: instanced glowing capsules. Segment i is shared by uCopies
// consecutive instances (the web's vertexAttribDivisor), read from a buffer.

struct InkVSOut {
  float4 position [[position]];
  vec2 vP;
  vec4 vSeg;
  vec4 vCol;
};

struct InkVS {
  constant InkU* _c;
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }
vec2 copyOf(vec2 p, int k) {
  vec2 z = rot(-uSpin) * (p - uCenter);
  if (uInkMirror && (k & 1) == 1) z.y = -z.y;
  int r = uInkMirror ? k >> 1 : k;
  z = rot(6.28318530718 / uSym * float(r)) * z;
  return uCenter + rot(uSpin) * z;
}
};

vertex InkVSOut ink_vs(uint vid [[vertex_id]], uint iid [[instance_id]],
                       constant InkU* _c [[buffer(1)]], const device float4* segs [[buffer(2)]]) {
  InkVS s; s._c = _c;
  int gl_VertexID = int(vid), gl_InstanceID = int(iid);
  vec4 aSeg = segs[(gl_InstanceID / uCopies) * 2], aCol = segs[(gl_InstanceID / uCopies) * 2 + 1];
  InkVSOut o;

  int k = gl_InstanceID % uCopies;
  vec2 a = s.copyOf(aSeg.xy, k), b = s.copyOf(aSeg.zw, k);
  float R = aCol.w * (1.5 + 3.0 * uGlow);
  vec2 dir = b - a;
  float L = length(dir);
  dir = L > 1e-6 ? dir / L : vec2(1.0, 0.0);
  vec2 nrm = vec2(-dir.y, dir.x);
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
  vec2 pos = (corner.x < 0.0 ? a - dir * R : b + dir * R) + nrm * corner.y * R;
  o.vP = pos;
  o.vSeg = vec4(a, b);
  o.vCol = aCol;
  o.position = vec4(pos.x * uRes.y / uRes.x, pos.y, 0.0, 1.0);
  return o;
}

fragment float4 ink_fs(InkVSOut in [[stage_in]], constant InkU* _c [[buffer(1)]]) {
  vec2 vP = in.vP; vec4 vSeg = in.vSeg, vCol = in.vCol;

  vec2 pa = vP - vSeg.xy, ba = vSeg.zw - vSeg.xy;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-12), 0.0, 1.0);
  float d = length(pa - ba * h);
  float rad = vCol.w;
  float R = rad * (1.5 + 3.0 * uGlow);
  float core = exp(-d * d / (rad * rad) * 2.5);
  float halo = uGlow * 0.35 * pow(max(1.0 - d / R, 0.0), 3.0);
  return vec4(vCol.rgb * (core + halo), 1.0);
}
