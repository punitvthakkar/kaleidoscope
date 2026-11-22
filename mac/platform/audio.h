// Sound out and in, on Core Audio (web: Sound). The hardware runs only while there is
// something to play or film; the microphone is open only while listening.
// Control calls: main thread unless noted. Hush and unhush happen by themselves when the
// app is hidden, fully covered or quitting (web: visibilitychange, pagehide).
#pragma once
#include <cstdint>
#include <functional>
#include "../core/synth.h"

void audio_set_sound(bool on);                          // web: setSynth
void audio_set_root(int chamber);                       // web: setRoot
void audio_pluck(float x, float y, float velocity);     // any thread
void audio_update(float dt, float energy, float lung);  // frame loop, once per frame; lung < 0: no breathing guide
SynthLevels audio_levels();                             // frame loop, after audio_update (web: Sound.level)

// Listen. `done` runs once on the main queue with the outcome of turning it on.
enum class ListenResult { listening, denied, unavailable };
void audio_set_listen(bool on, std::function<void(ListenResult)> done = nullptr);
bool audio_listening();

// Films (any thread). has_sound decides whether the film gets a sound track at all
// (web: recordStream() returning null). The tap runs on the audio thread and must not
// block or allocate: float PCM at audio_sample_rate(), hostTime (mach ticks) is when the
// first frame is heard. It receives what is heard plus, when listening, the room,
// through the film's fader (silent 0.12 s, up by 0.5 s).
using AudioFilmTap = void (*)(const float* left, const float* right, int frames, uint64_t hostTime);
bool audio_film_has_sound();
double audio_sample_rate();
void audio_film_begin(AudioFilmTap tap);
void audio_film_fade(std::function<void()> silent);     // fades over 0.3 s; `silent` on the main queue after 0.42 s
void audio_film_end();                                  // no tap calls after this returns
