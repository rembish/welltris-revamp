/* The PC speaker, re-synthesised from the core's speaker log. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include "../core/wt_core.h"

void audio_init(void);
void audio_resume(void);
void audio_set_volume(float v);
/* render the speaker up to core clock `until` (PIT clocks), following g's speaker log */
void audio_follow(const wt_game *g, uint64_t until);
/* jump to `clock` without playing anything in between (after a pause or between games) */
void audio_sync(const wt_game *g, uint64_t clock);

#endif
