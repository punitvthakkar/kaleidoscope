// Debug chambers (intent extra fx.chambers, never user-facing): renders each chamber
// of <host_files_dir()>/chambers.txt in turn through the real engine at the display's
// size, saves <host_files_dir()>/chambers/<slug>.png and logs, tag "fx", one line per
// chamber: "chamber <slug> gpu_ms=<mean of the last 60 frames> size=WxH".
// Any thread; the work runs on the render thread through host_post_render.
// First, as a reference, each chamber exactly as mac/render/test_render.mm renders it
// (its mini engine, 1120x704) to <host_files_dir()>/chambers_ref/<slug>.png, logging
// "chamber_ref <slug> ..." with the pass probe below.
#pragma once
#include <cstdint>
#include <vector>
struct Renderer;
// `only`: a comma-separated list of slugs ("galaxy,nebula"), or "" / "true" for every chamber.
void render_debug_chambers(const char* only);
// Debug only (render thread): out[0] 1x1 luminance, out[1] adapted luminance, then the
// mean RGB of the scene, the current echo image (level 0) and bloom level 0.
void renderer_debug_probe(Renderer*, float out[16]);
// Debug only (render thread): the last frame's still as PNG, read back at once.
std::vector<uint8_t> renderer_debug_snapshot(Renderer*, int w, int h);
// Debug looks (intent extra fx.looks, dev builds): each hand look (1..5, or the
// comma-separated list given) drawn from two synthetic hands over the current chamber
// through the real renderer at the app's size; saves <host_files_dir()>/looks/look<N>.png
// and logs "looks <N> gpu_ms=<with> base_ms=<without> seg= vert= strips= aura= feed= copies=".
void render_debug_looks(const char* which);
// Debug only: fx.skydbg's bit mask (see renderer.cpp).
void renderer_sky_debug(int mask);
