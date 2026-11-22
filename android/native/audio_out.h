// Sound out on AAudio, running mac/core/synth (as mac/platform/audio.mm does on Core
// Audio). Internal to the host; the film tap API is declared in host.h.
// The stream is open exactly while there is something to play or film, and closed in
// the background once the hush has faded. Calls: main thread unless noted.
#pragma once

void audio_set_sound(bool on);            // web: setSynth
void audio_set_foreground(bool on);       // hush and close / unhush and reopen
void audio_set_listening(bool on);        // whether Listen is on (films record the room)
int audio_xruns();                        // underruns heard since launch; any thread
int audio_buffer_frames();                // the output buffer in use (0 when closed); any thread
