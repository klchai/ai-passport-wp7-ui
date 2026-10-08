#pragma once

#include <stdbool.h>

/* Timer chime on the ES8311 speaker. Playback runs in its own task; these
   calls only signal it, so they are safe from the LVGL task. */
void wp7_sound_init(void);
/* Rings until silenced, for at most a minute. Ignored if audio is unavailable. */
void wp7_sound_ring(void);
/* Stops the ringing; returns whether it was ringing. */
bool wp7_sound_silence(void);
bool wp7_sound_ringing(void);
