// Extracts the GLSL ES 3.00 sources from index.html into <assets>/shaders/ as plain
// files, applies the native HDR / flash-guard / brightness-ceiling changes
// (mac/render/shaders/post.metal) to final.frag as one anchored patch, and writes
// <assets>/chambers.txt (mac/render/chambers.mjs: "slug key value" per line).
//
//   node make_shaders.mjs <repo>/index.html <assetsDir>
//
// Fails loudly: a missing shader, an anchor that does not match exactly once, or a
// source the evaluated string would not reproduce byte for byte.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const [indexPath, assets] = process.argv.slice(2);
if (!indexPath || !assets) fail('usage: node make_shaders.mjs <index.html> <assetsDir>');
function fail(msg) { console.error('make_shaders: ' + msg); process.exit(1); }

const html = fs.readFileSync(indexPath, 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const found = blocks.filter((b) => b.includes('// GLSL ES 3.0 sources'));
if (found.length !== 1) fail(`expected one script block with the GLSL sources, found ${found.length}`);
const src = found[0];
// Template literals equal their raw text only without escapes or substitutions.
if (src.includes('\\') || src.includes('${')) fail('GLSL block has escapes or ${}: extracted text would differ from index.html');
const S = new Function(src + '\nreturn SHADERS;')();

// File name -> SHADERS key. Every one must exist.
const FILES = {
  'fullscreen.vert': 'FULLSCREEN_VS', 'feedback.frag': 'FEEDBACK_FS', 'raymarch.frag': 'RAYMARCH_FS',
  'scene.frag': 'SCENE_FS', 'down.frag': 'DOWN_FS', 'up.frag': 'UP_FS', 'final.frag': 'FINAL_FS',
  'copy.frag': 'COPY_FS', 'lum.frag': 'LUM_FS', 'adapt.frag': 'ADAPT_FS', 'ink.vert': 'INK_VS', 'ink.frag': 'INK_FS',
  'look.vert': 'LOOK_VS', 'look.frag': 'LOOK_FS', 'strip.vert': 'STRIP_VS', 'strip.frag': 'STRIP_FS', 'aura.frag': 'AURA_FS',
};
const keys = Object.keys(S).sort().join(), want = Object.values(FILES).sort().join();
if (keys !== want) fail(`SHADERS has [${keys}], expected [${want}]`);
for (const [file, key] of Object.entries(FILES)) {
  if (typeof S[key] !== 'string' || !S[key].startsWith('#version 300 es')) fail(`${key} missing or not GLSL ES 3.00`);
}

// ---- the one native patch: FINAL_FS as in mac/render/shaders/post.metal ----
// Up to the tone curve this is the web's pass. With uOutMode 0 and uPeak 1 the output
// is the web canvas's exactly (an 8-bit target clamps to [0, 1] as the canvas did).
// uOutMode 1 converts the same sRGB-encoded value (shoulder rising to uPeak) to
// linear light and encodes BT.2020 HLG as mac/platform/recorder.mm does.
const PATCH = [
  [`uniform uint uFrame;
out vec4 o;
`, `uniform uint uFrame;
// Native (mac/render/shaders/post.metal): HDR shoulder, flash-guard HDR limit, output.
uniform float uPeak;      // sRGB-encoded value of the brightest output (1 = SDR white)
uniform float uKs;        // max over x of SDR linear output / x
uniform float uG;         // max over x of (HDR - SDR linear output) / x at this peak
uniform float uBloomMeanK;  // bound on mean(scene + bloom) / mean(scene)
uniform int uOutMode;     // 0: sRGB-encoded (the web canvas); 1: BT.2020 HLG
out vec4 o;
`],
  [`  if (m > 0.7) c *= (0.7 + 0.3 * (1.0 - exp(-(m - 0.7) * 3.333))) / m;
  return c;
}
`, `  if (m > 0.7) c *= (0.7 + 0.3 * (1.0 - exp(-(m - 0.7) * 3.333))) / m;
  return c;
}
// The same knee, with the shoulder running to the encoded peak W instead of 1.
vec3 tonemapTo(vec3 c, float W) {
  float m = max(c.r, max(c.g, c.b));
  if (m > 0.7) c *= (0.7 + (W - 0.7) * (1.0 - exp(-(m - 0.7) / (W - 0.7)))) / m;
  return c;
}
vec3 srgbToLinear(vec3 c) {
  return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}
// Linear sRGB (1 = SDR white, 203 cd/m2) to the BT.2020 HLG signal, as recorder.mm.
vec3 hlg(vec3 srgb) {
  const mat3 toBT2020 = mat3(vec3(0.627404, 0.069097, 0.016391),
                             vec3(0.329283, 0.919540, 0.088013),
                             vec3(0.043313, 0.011362, 0.895595));
  vec3 d = max(toBT2020 * srgb, 0.0) * (203.0 / 1000.0);
  float l = 0.5 * dot(d, vec3(0.2627, 0.6780, 0.0593)) + 0.5 * max(d.r, max(d.g, d.b));
  vec3 e = l > 1e-9 ? d * pow(l, -1.0 / 6.0) : vec3(0.0);
  e = clamp(e, 0.0, 1.0);
  vec3 lo = sqrt(3.0 * e);
  vec3 hi = 0.17883277 * log(max(12.0 * e - 0.28466892, 1e-6)) + 0.55991073;
  return mix(hi, lo, lessThanEqual(e, vec3(1.0 / 12.0)));
}
`],
  [`  // Highlight-only flash limiter: absorbs sudden upward brightness spikes without pumping darks.
  if (uGuard) {
    float cur = texelFetch(uLumCur, ivec2(0), 0).r;
    float ad = texelFetch(uLumAdapt, ivec2(0), 0).r;
    float spike = max(0.0, cur - (ad * 1.18 + 0.04));
    c /= 1.0 + spike * 2.2;
    // Brightness ceiling: past the knee, average brightness rolls off toward uCap.
    float L = max(cur, ad), knee = 0.6 * uCap;
    if (L > knee) c *= (knee + (uCap - knee) * (1.0 - exp(-(L - knee) / (uCap - knee)))) / L;
  }
  c = tonemap(c * uExposure);
  float v = length(d * vec2(uOut.x / uOut.y, 1.0));
  c *= 1.0 - uVignette * smoothstep(0.35, 1.15, v);
  float n = float(pcg2d(uvec2(gl_FragCoord.xy) + uvec2(uFrame * 1973u, uFrame * 9277u)).x) / 4294967295.0;
  c += (n - 0.5) * uGrain;
  o = vec4(c, 1.0);
}`, `  float hdr = 1.0;   // share of the HDR shoulder the flash guard allows (native)
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
    // Native: keeps the screen's mean linear light at or below uKs * uExposure * uCap,
    // the most the web can ever show under the same ceiling.
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
  if (uOutMode == 1) c = hlg(srgbToLinear(c));
  o = vec4(c, 1.0);
}`],
];
let fin = S.FINAL_FS;
for (const [from, to] of PATCH) {
  const n = fin.split(from).length - 1;
  if (n !== 1) fail(`final.frag patch anchor matches ${n} times (want 1):\n${from.split('\n')[0]}`);
  fin = fin.replace(from, () => to);
}

const dir = path.join(assets, 'shaders');
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });
// Every file except final.frag is index.html's text unchanged (guaranteed by the
// escape check above: each is a concatenation of raw template literals).
for (const [file, key] of Object.entries(FILES)) fs.writeFileSync(path.join(dir, file), file === 'final.frag' ? fin : S[key]);

const chambers = execFileSync(process.execPath, [path.join(path.dirname(indexPath), 'mac/render/chambers.mjs'), indexPath], { encoding: 'utf8' });
const slugs = new Set(chambers.trim().split('\n').map((l) => l.split(' ')[0]));
if (slugs.size < 1) fail('chambers.mjs printed no chambers');
fs.writeFileSync(path.join(assets, 'chambers.txt'), chambers);
console.error(`make_shaders: ${Object.keys(FILES).length} shaders, final.frag patched, ${slugs.size} chambers`);
