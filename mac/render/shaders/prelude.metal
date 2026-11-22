// GLSL-compatibility prelude, so the shader bodies below read line for line like the
// web's GLSL (index.html, SHADERS). The renderer concatenates: metal_stdlib,
// uniforms.h, prelude.metal, common.metal, then the pass files.
//
// Orientation: every pass works in GL's convention (gl_FragCoord and texture v both
// grow upward). Fragment entry points build gl_FragCoord with y flipped, and Tex
// samples at v flipped, so a render target's row 0 holds the top of the picture in
// both worlds and nothing turns over, frame after frame. Only the camera, which
// arrives top row first like the web's video upload, is sampled unflipped.

typedef float2 vec2;
typedef float3 vec3;
typedef float4 vec4;
typedef int2 ivec2;
typedef uint2 uvec2;
typedef float2x2 mat2;
typedef float3x3 mat3;

#define PI 3.14159265359
#define TAU 6.28318530718
#define dFdx(x) dfdx(x)
#define dFdy(x) (-dfdy(x))   // Metal's window y grows downward

// GLSL mod (floor-based; Metal's fmod truncates).
inline float mod(float x, float y) { return x - y * floor(x / y); }
inline float2 mod(float2 x, float y) { return x - y * floor(x / y); }
inline float3 mod(float3 x, float y) { return x - y * floor(x / y); }
inline float atan(float y, float x) { return atan2(y, x); }
inline float3 mix(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float2 mix(float2 a, float2 b, float t) { return a + (b - a) * t; }
inline float4 mix(float4 a, float4 b, float t) { return a + (b - a) * t; }

// A sampler2D: texture, its wrap/filter state (GL keeps that on the texture), orientation.
struct Tex { texture2d<float> t; sampler s; bool flip; };
inline float2 texUV(Tex T, float2 uv) { return T.flip ? float2(uv.x, 1.0 - uv.y) : uv; }
inline float4 texture(Tex T, float2 uv) { return T.t.sample(T.s, texUV(T, uv)); }
inline float4 textureLod(Tex T, float2 uv, float lod) { return T.t.sample(T.s, texUV(T, uv), level(lod)); }
inline float4 texelFetch(Tex T, int2 p, int lod) { return T.t.read(uint2(p), uint(lod)); }

constexpr sampler sMirror(filter::linear, mip_filter::linear, address::mirrored_repeat);
constexpr sampler sMipClamp(filter::linear, mip_filter::linear, address::clamp_to_edge);
constexpr sampler sClamp(filter::linear, mip_filter::none, address::clamp_to_edge);
inline Tex clampTex(texture2d<float> t) { return Tex{t, sClamp, true}; }

// Render targets are allocated at the window's largest render size and each detail level
// draws into their top-left part, so wrapping is done here, inside that part (kh: its
// share of the texture, then half a texel, which keeps bilinear taps inside it).
// MirrorTex repeats mirrored (with mips up to maxLod); ClampTex clamps.
struct MirrorTex { texture2d<float> t; constant float4* kh; constant float* maxLod; };
struct ClampTex { texture2d<float> t; constant float4* kh; };
inline float2 partUV(float4 kh, float2 uv) { return clamp(float2(uv.x, 1.0 - uv.y) * kh.xy, kh.zw, kh.xy - kh.zw); }
inline float2 mirrorUV(float2 uv) { return 1.0 - abs(1.0 - (uv - 2.0 * floor(uv * 0.5))); }
inline float4 textureLod(MirrorTex T, float2 uv, float lod) { return T.t.sample(sMipClamp, partUV(*T.kh, mirrorUV(uv)), level(min(lod, *T.maxLod))); }
inline float4 texture(MirrorTex T, float2 uv) { return T.t.sample(sMipClamp, partUV(*T.kh, mirrorUV(uv)), level(0.0)); }
inline float4 textureLod(ClampTex T, float2 uv, float lod) { return T.t.sample(sClamp, partUV(*T.kh, saturate(uv)), level(lod)); }
inline float4 texture(ClampTex T, float2 uv) { return T.t.sample(sClamp, partUV(*T.kh, saturate(uv))); }

// GLSL uniforms by name. Each pass struct (and entry point) points at its blocks as _c
// (CommonU, or InkU for ink) and _p (the pass's own block), in constant memory: a copy
// in the struct would put the arrays (uKifsCS, uSkyLens, uRip) in per-pixel stack memory.
#define uRes _c->uRes
#define uMode _c->uMode
#define uSym _c->uSym
#define uMirror _c->uMirror
#define uSpin _c->uSpin
#define uCenter _c->uCenter
#define uViewZoom _c->uViewZoom
#define uWarp _c->uWarp
#define uKifsIters _c->uKifsIters
#define uKifs _c->uKifs
#define uKifsOff _c->uKifsOff
#define uHyp _c->uHyp
#define uDroste _c->uDroste
#define uTile _c->uTile
#define uPalA _c->uPalA
#define uPalB _c->uPalB
#define uPalC _c->uPalC
#define uPalD _c->uPalD
#define uSpinCS _c->uSpinCS
#define uKifsCS _c->uKifsCS
#define uCopies _c->uCopies
#define uInkMirror _c->uInkMirror
#define uGlow _c->uGlow
#define uFoldLoop _p->uFoldLoop
#define uColorMat _p->uColorMat
#define uFbRot _p->uFbRot
#define uFbZoom _p->uFbZoom
#define uFbSwirl _p->uFbSwirl
#define uFbWarp _p->uFbWarp
#define uFbWarpT _p->uFbWarpT
#define uSharpen _p->uSharpen
#define uDt _p->uDt
#define uDream _p->uDream
#define uDreamC _p->uDreamC
#define uFbRotCS _p->uFbRotCS
#define uRip _p->uRip
#define uRipN _p->uRipN
#define uGen _p->uGen
#define uGenK _p->uGenK
#define uGenScale _p->uGenScale
#define uGenT _p->uGenT
#define uGenRadius _p->uGenRadius
#define uCamOn _p->uCamOn
#define uCamK _p->uCamK
#define uCamZoom _p->uCamZoom
#define uCamAspect _p->uCamAspect
#define uCamColorize _p->uCamColorize
#define uRmRot _p->uRmRot
#define uRmScale _p->uRmScale
#define uRmZ _p->uRmZ
#define uRmT _p->uRmT
#define uRmSteps _p->uRmSteps
#define uRmQ _p->uRmQ
#define uRmShape _p->uRmShape
#define uRmLook _p->uRmLook
#define uRmSlice _p->uRmSlice
#define uRmDetail _p->uRmDetail
#define uRmZoom _p->uRmZoom
#define uRmYaw _p->uRmYaw
#define uRmPitch _p->uRmPitch
#define uRmPad _p->uRmPad
#define uRmCamP _p->uRmCamP
#define uRmCamF _p->uRmCamF
#define uRmCamU _p->uRmCamU
#define uFisheye _p->uFisheye
#define uPalMap _p->uPalMap
#define uPalShift _p->uPalShift
#define uVeil _p->uVeil
#define uSheen _p->uSheen
#define uFbScale _p->uFbScale
#define uSheenL _p->uSheenL
#define uTaps _p->uTaps
#define uSky _p->uSky
#define uSkyMirror _p->uSkyMirror
#define uSkyView _p->uSkyView
#define uSkyAt _p->uSkyAt
#define uSkyBox _p->uSkyBox
#define uSkyLens _p->uSkyLens
#define uSkyLensN _p->uSkyLensN
#define uTexel _p->uTexel
#define uOut _p->uOut
#define uThreshold _p->uThreshold
#define uPrefilter _p->uPrefilter
#define uK _p->uK
#define uGuard _p->uGuard
#define uBloom _p->uBloom
#define uChroma _p->uChroma
#define uGrain _p->uGrain
#define uVignette _p->uVignette
#define uExposure _p->uExposure
#define uCap _p->uCap
#define uFrame _p->uFrame
#define uPeak _p->uPeak
#define uKs _p->uKs
#define uG _p->uG
#define uBloomMeanK _p->uBloomMeanK
#define uLinearOut _p->uLinearOut

inline float4 glFragCoord(float4 pos, float h) { return float4(pos.x, h - pos.y, pos.z, pos.w); }

struct VSOut { float4 position [[position]]; };

// FULLSCREEN_VS
vertex VSOut fullscreen_vs(uint gl_VertexID [[vertex_id]]) {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  VSOut o; o.position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
  return o;
}
