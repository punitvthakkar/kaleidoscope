// Translates the sky places' Metal shaders (mac/render/shaders/stars.metal) into
// GLSL ES 3.00 for the Android renderer, so the Mac file stays the only source:
//   <assets>/shaders/stars_step.frag   moves every point one step (with fullscreen.vert)
//   <assets>/shaders/stars.vert        draws the points into the wedge textures
//   <assets>/shaders/stars.frag
//   <assets>/shaders/stars_trail.frag  carries the soft light on (with fullscreen.vert)
//
//   node make_stars.mjs <repo>/index.html <assetsDir>
//
// stars.metal keeps to a small subset of Metal on purpose (no textures but the state
// texture, no Metal-only built-ins beyond the ones mapped below). The uniforms become
// one std140 block, StarsU, whose layout equals the Metal struct in
// mac/render/shaders/uniforms.h (float3 there is padded to 16 bytes, so the palette is
// vec4 here and read as .xyz). Fails loudly on anything it does not know how to map.
import fs from 'node:fs';
import path from 'node:path';

const [indexPath, assets] = process.argv.slice(2);
if (!indexPath || !assets) fail('usage: node make_stars.mjs <index.html> <assetsDir>');
function fail(msg) { console.error('make_stars: ' + msg); process.exit(1); }

const root = path.dirname(indexPath);
const metal = fs.readFileSync(path.join(root, 'mac/render/shaders/stars.metal'), 'utf8');

// ---- cut the file into its helpers and the three entry points ----
function between(a, b) {
  const i = metal.indexOf(a), j = metal.indexOf(b, i + 1);
  if (i < 0 || j < 0 || metal.indexOf(a, i + 1) >= 0) fail(`cannot find exactly one "${a}" followed by "${b}"`);
  return metal.slice(i, j);
}
function body(sig) {   // the text between the entry point's opening brace and its matching close
  const i = metal.indexOf(sig);
  if (i < 0 || metal.indexOf(sig, i + 1) >= 0) fail(`cannot find exactly one "${sig}"`);
  let k = metal.indexOf('{', i), depth = 0;
  for (let j = k; j < metal.length; j++) {
    if (metal[j] === '{') depth++;
    else if (metal[j] === '}' && --depth === 0) return metal.slice(k + 1, j);
  }
  fail(`unbalanced braces in "${sig}"`);
}
const helpers = between('constant float PI_S', 'fragment float4 stars_step_fs(');
const stepBody = body('fragment float4 stars_step_fs(');
const vsBody = body('vertex StarOut stars_vs(');
const fsBody = body('fragment float4 stars_fs(');
const trailBody = body('fragment float4 stars_trail_fs(');

// ---- Metal -> GLSL, token by token ----
function common(s) {
  return s
    .replace(/\bconstant float\b/g, 'const float')
    .replace(/\binline\s+/g, '')
    .replace(/thread (float2|float3|float4)&/g, 'inout $1')
    .replace(/\bfloat2\b/g, 'vec2').replace(/\bfloat3\b/g, 'vec3').replace(/\bfloat4\b/g, 'vec4')
    .replace(/\buint2\b/g, 'uvec2').replace(/\bint2\b/g, 'ivec2')
    .replace(/\batan2\(/g, 'atan(')
    .replace(/\brsqrt\(/g, 'inversesqrt(')
    .replace(/\bout\b/g, 'outv')                       // a GLSL keyword; only ever a local name in stars.metal
    .replace(/u\.sPal([ABCD])\b/g, 'u.sPal$1.xyz');
}
// Inside a function GLSL's const needs a constant expression; Metal's only says "not
// reassigned". Dropped, except on the per-sky tables, which become array constructors.
function locals(s) {
  s = s.replace(/const float (\w+)\[(\d+)\] = \{([^}]*)\};/g, 'const float $1[$2] = float[$2]($3);');
  return s.replace(/^(\s*)const (?!float \w+\[)/gm, '$1');
}

const step = locals(common(stepBody))
  .replace('uvec2 px = uvec2(in.position.xy);', 'uvec2 px = uvec2(gl_FragCoord.xy);')
  .replace('vec4 s = prev.read(px);', 'vec4 s = texelFetch(uPrev, ivec2(px), 0);')
  .replace('return vec4(x, v);', 'o = vec4(x, v);');
const vs = locals(common(vsBody))
  .replace('uint w = uint(u.sDim.x);', 'uint w = uint(u.sDim.x), vid = uint(gl_VertexID);')
  .replace('vec4 s = st.read(px);', 'vec4 s = texelFetch(uState, ivec2(px), 0);')
  .replace('int j = int(iid);', 'int j = gl_InstanceID;')
  .replace(/\biid\b/g, 'gl_InstanceID')
  .replaceAll('return starOff();', '{ gl_Position = vec4(3.0, 3.0, 0.0, 1.0); gl_PointSize = 1.0; return; }')
  .replace('StarOut o;', '')
  .replace(/\bo\.position\b/g, 'gl_Position').replace(/\bo\.size\b/g, 'gl_PointSize')
  .replace(/\bo\.(col|spike|px|sig|head)\b/g, (m, n) => 'v' + n[0].toUpperCase() + n.slice(1))
  .replace(/\n\s*return o;\s*$/, '\n');
const trail = locals(common(trailBody))
  .replace('vec2 uv = vec2(in.position.x, u.sRes.y - in.position.y) / u.sRes;', 'vec2 uv = gl_FragCoord.xy / u.sRes;')
  .replace('prev.sample(starLinear, vec2(q.x, 1.0 - q.y))', 'texture(uPrev, q)')
  .replace('return vec4(c, 1.0);', 'o = vec4(c, 1.0);');
const frag = locals(common(fsBody))
  .replace('vec2 q = pc - 0.5;', 'vec2 q = gl_PointCoord - 0.5;')
  .replace(/\bin\.(col|spike|px|sig|head)\b/g, (m, n) => 'v' + n[0].toUpperCase() + n.slice(1))
  .replace(/return (vec4\([^;]*\));/g, '{ o = $1; return; }');

// Leftovers would compile on the Mac and fail on the phone: refuse them here.
for (const [name, src] of [['helpers', common(helpers)], ['step', step], ['vertex', vs], ['fragment', frag], ['trail', trail]]) {
  const bad = src.replace(/\/\/.*$/gm, '').match(/\b(float[234]|uint2|thread|constant|inline|prev\.read|prev\.sample|st\.read|in\.\w+|iid|StarOut|starLinear|starOff)\b|\[\[/);
  if (bad) fail(`${name}: unmapped Metal "${bad[0]}"`);
}

const BLOCK = `layout(std140) uniform StarsU {
  vec4 sCore[8];
  vec4 sVel[8];
  vec4 sBlast;
  vec4 sView;
  vec4 sPalA, sPalB, sPalC, sPalD;   // float3 in Metal, padded to 16 bytes there too
  vec4 sBB, sBBPrev, sFb;
  vec2 sRes, sDim;
  int sCores, sAuto, sReset, sKind;
  int sMirror, sHead;
  float sDt, sTime, sSeed, sBright, sPalShift, sSize;
  float sSym, sPulse, sHeadBright, sHeadSize, sPx;
  float sTexDen, sTexK, sEchoKeep, sReach;
} u;
float distance_squared(vec2 a, vec2 b) { vec2 d = a - b; return dot(d, d); }
`;
const HEAD = `#version 300 es
// Generated by android/tools/make_stars.mjs from mac/render/shaders/stars.metal. Do not edit.
precision highp float;
precision highp int;
`;
const VARY = (q) => `${q} vec3 vCol;
${q} float vSpike;
${q} float vHead;
${q} float vPx;
${q} float vSig;
`;

const dir = path.join(assets, 'shaders');
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(path.join(dir, 'stars_step.frag'),
  HEAD + 'uniform highp sampler2D uPrev;\nlayout(location = 0) out vec4 o;\n' + BLOCK + common(helpers) + '\nvoid main() {' + step + '}\n');
fs.writeFileSync(path.join(dir, 'stars.vert'),
  HEAD + 'uniform highp sampler2D uState;\n' + VARY('out') + BLOCK + common(helpers) + '\nvoid main() {' + vs + '}\n');
fs.writeFileSync(path.join(dir, 'stars_trail.frag'),
  HEAD + 'uniform highp sampler2D uPrev;\nlayout(location = 0) out vec4 o;\n' + BLOCK + common(helpers) + '\nvoid main() {' + trail + '}\n');
fs.writeFileSync(path.join(dir, 'stars.frag'),
  HEAD + VARY('in') + 'layout(location = 0) out vec4 o;\nvoid main() {' + frag + '}\n');
console.error('make_stars: stars_step.frag, stars.vert, stars.frag, stars_trail.frag from stars.metal');
