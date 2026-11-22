// Uniform blocks, shared by the Metal shaders (prepended to their source at runtime)
// and renderer.mm, so the two layouts cannot drift. Names follow the web's GLSL uniforms.
// simd types on the CPU side have the same size and alignment as MSL's (float3 is 16 bytes).
#ifndef FRACTONAUT_UNIFORMS_H
#define FRACTONAUT_UNIFORMS_H
#ifdef __METAL_VERSION__
#define F2 float2
#define F3 float3
#define F4 float4
#define M3 float3x3
#define U32 uint
#else
#include <simd/simd.h>
#define F2 simd_float2
#define F3 simd_float3
#define F4 simd_float4
#define M3 simd_float3x3
#define U32 unsigned
#endif

struct CommonU {
  F2 uRes; int uMode; float uSym;
  int uMirror; float uSpin; F2 uCenter;
  float uViewZoom; F4 uWarp;          // amount, scale, time
  float uKifsIters; F2 uKifs;         // scale, angle
  F2 uKifsOff;
  F4 uHyp;                            // circle center, radius, mobius a.xy
  F2 uDroste;                         // log ratio, phase
  float uTile;
  F3 uPalA, uPalB, uPalC, uPalD;
  F2 uSpinCS;                         // cos, sin of the mirror spin
  F2 uKifsCS[10];                     // cos, sin of each fractal fold's twist
};

struct FeedbackU {
  int uFoldLoop;
  M3 uColorMat;
  float uFbRot, uFbZoom, uFbSwirl, uFbWarp, uFbWarpT, uSharpen, uDt, uDream;
  F2 uDreamC, uFbRotCS;
  F4 uRip[4];                         // ripple center.xy, radius, push
  int uRipN;
  int uGen;
  float uGenK, uGenScale, uGenT, uGenRadius;
  int uCamOn;
  float uCamK, uCamZoom, uCamAspect, uCamColorize;
};

struct RaymarchU {
  M3 uRmRot;
  float uRmScale, uRmZ, uRmT;
  int uRmSteps;
  F4 uRmQ;                            // the 4D shapes' quaternion c this frame (real part first)
  int uRmShape, uRmLook;              // RM_SHAPES and RM_LOOKS in index.html
  float uRmSlice, uRmDetail;          // the 3D slice through the fourth dimension; the shape's own dial (0..1)
  float uRmZoom, uRmYaw, uRmPitch, uRmPad;   // the hand's camera: zoom (1 = as framed), turn; uRmPad: 1 = only the kaleidoscope's wedge is drawn
  F4 uRmCamP, uRmCamF, uRmCamU;       // surface flight: camera position + height; forward + shape radius; up + on (w)
};

struct SceneU {
  float uFisheye, uPalMap, uPalShift, uVeil, uSheen, uFbScale;
  F2 uSheenL;
  int uTaps;
  int uSky, uSkyMirror;               // the sky places: on, and whether their wedge mirrors
  F4 uSkyView;                        // cos and sin of the sky's turn, 1 / its zoom, its symmetry
  F4 uSkyAt;                          // its centre (xy) and the hand's parallax (zw)
  F4 uSkyBox;                         // the wedge textures' extent in sky space
  F4 uSkyLens[8];                     // each finger's lens: where (in the wedge) and its Einstein radius squared
  int uSkyLensN;
};

struct DownU { F2 uTexel, uOut; float uThreshold; int uPrefilter; };
struct UpU { F2 uTexel, uOut; };
struct CopyU { F2 uOut; };
struct AdaptU { float uK; };
// The used part of each sampled render target by texture index (xy: its detail level's
// size / its allocation, zw: half a texel), and the highest mip level holding this
// frame's picture; fragment buffer 3.
struct TexK { F4 kh[4]; float maxLod, zero; };

struct FinalU {
  int uGuard;
  F2 uOut;
  float uBloom, uChroma, uGrain, uVignette, uExposure, uCap;
  U32 uFrame;
  // Native additions (HDR): see FINAL in post.metal.
  float uPeak;        // sRGB-encoded value of the brightest output (1 = SDR white)
  float uKs;          // max over x of SDR linear output / x
  float uG;           // max over x of (HDR - SDR linear output) / x at this peak
  float uBloomMeanK;  // bound on mean(scene + bloom) / mean(scene)
  int uLinearOut;     // 1: extended linear sRGB; 0: sRGB-encoded (8-bit stills, as the web canvas)
};

// The sky places (stars.metal). Names start with s so they never meet the GLSL-style
// uniform macros in prelude.metal. The layout is also a std140 block on Android
// (android/tools/make_stars.mjs), so only vec4-sized fields, then pairs, then scalars.
struct StarsU {
  F4 sCore[8];                        // each core: position.xy (in the wedge), mass, fourth value (a finger: 1 if hovering)
  F4 sVel[8];                         // each core's velocity.xy (a finger's sweep)
  F4 sBlast;                          // a burst: where (xy, in the wedge) and how hard (z)
  F4 sView;                           // how the sky is looked at: zoom, cos and sin of its turn
  F3 sPalA, sPalB, sPalC, sPalD;
  F4 sBB;                             // the wedge texture being drawn: its extent in sky space (x0, y0, x1, y1)
  F4 sBBPrev;                         // the trail texture's extent last frame
  F4 sFb;                             // the trails' own turn and zoom each frame (cos, sin, zoom) and fade (w)
  F2 sRes;                            // the target's size in texels
  F2 sDim;                            // the state texture's size: points = width × height
  int sCores, sAuto, sReset, sKind;   // sAuto: how many of the cores are the place's own (the rest are fingers)
  int sMirror, sHead;                 // the chamber mirrors; this draw is the sharp texture
  float sDt, sTime, sSeed, sBright, sPalShift, sSize;
  float sSym, sPulse, sHeadBright, sHeadSize, sPx;
  float sTexDen;                      // the target's texels per unit of sky
  float sTexK;                        // its texels per screen pixel (sizes are given in screen pixels)
  float sEchoKeep;                    // the share of points the soft pass draws (1 on the Mac; a phone draws fewer)
  float sReach;                       // how far the screen reaches into the sky (to its farthest corner): the gas, beams and rays fill it
};

struct InkU {
  F2 uRes; int uCopies; int uInkMirror;
  float uSym, uSpin, uGlow;
  F2 uCenter;
};

// The hands' looks (looks.metal): one block for the segments, strips and aura, bound to
// vertex and fragment buffer 1.
struct LookU {
  F2 uRes; int uCopies; int uLookMirror;
  float uSym, uSpin, uFish, uGain;
  F2 uCenter; float uT; int uKind;
  float uBreath; int uHands;
  F4 uHand[2];                        // per hand: palm xy, hand size, presence
  F2 uPts[42];                        // per hand: its 21 points
};

#undef F2
#undef F3
#undef F4
#undef M3
#undef U32
#endif
