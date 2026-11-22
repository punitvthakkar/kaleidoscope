// The web version's Sound module (index.html, `const Sound`) as portable DSP: the same
// Web Audio graph rebuilt node for node. One instance per process.
//
// Threads:
//   synth_render                 the audio thread (no locks, no allocation)
//   synth_mic_push               the microphone's input thread
//   synth_update, synth_levels   the frame loop (one thread)
//   everything else              any thread; delivered to the audio thread through atomics
#pragma once
#include <cstdint>

// Allocates the delay lines for this rate. Call before the first synth_render, and again
// on a rate change, never while synth_render may run. Control state is kept; the sound
// itself starts afresh (fading in, as when it is turned on).
void synth_create(double sampleRate);

// One block of what the speakers play. When a film is being made and filmL/filmR are
// given, they receive the film's copy (what is heard plus the microphone, through the
// film's own fader) and true is returned.
bool synth_render(float* left, float* right, int frames, float* filmL = nullptr, float* filmR = nullptr);

void synth_set_on(bool on);       // web: setSynth
bool synth_is_on();
bool synth_is_silent();           // off and its fade has finished: the hardware may stop
void synth_set_root(int chamber); // web: setRoot
void synth_pluck(float x, float y, float velocity);
// Once per rendered frame. lung < 0 when the breathing guide is off.
void synth_update(float dt, float energy, float lung);
void synth_hush(bool fast);
void synth_unhush();

// Film fader (web: recordStream / fadeRecord / endRecord). fade takes 0.3 s.
void synth_film_begin();
void synth_film_fade();
void synth_film_end();

// Listening. mic_start before the first push; mono samples at that rate.
void synth_mic_start(double sampleRate);
void synth_mic_stop();
void synth_mic_push(const float* mono, int frames);
struct SynthLevels { float bass, mid, high; };
SynthLevels synth_levels();       // as of the last synth_update; zero when not listening

// Web Audio BiquadFilterNode coefficients (normalized by a0). type 0 lowpass, 1 highpass;
// Q in dB as the spec defines for these two types.
struct BiquadCoefs { double b0, b1, b2, a1, a2; };
BiquadCoefs synth_biquad(int type, double frequency, double qDb, double sampleRate);
