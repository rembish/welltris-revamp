/*
 * The original only ever drives the PC speaker with Turbo C's sound(f): the PIT gets the integer
 * divisor 1193180 / f, so the tone is 1193180 / (1193180 / f) Hz, a square wave. The core logs
 * each change with the PIT clock it happened at; here the log is played back sample by sample
 * on the same clock, slightly behind real time so that changes are never late.
 */
#include "audio.h"
#include <SDL.h>
#include <math.h>
#include <string.h>

#define RATE    44100
#define LATENCY (WT_PIT_HZ / 20) /* render 50 ms behind */

static SDL_AudioDeviceID dev;
static float volume = 0.16f;
static uint64_t pos;      /* PIT clock rendered up to, times RATE (exact: one sample = PIT_HZ) */
static uint32_t rd;       /* speaker log entries consumed */
static double hz, phase;  /* current tone (0 = silent) */
static float lp;          /* one-pole low-pass state */

void audio_init(void)
{
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void audio_resume(void)
{
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void audio_set_volume(float v) { volume = v; }

static double tone(unsigned f)
{
    unsigned div;
    if (!f) return 0;
    div = 1193180u / f;
    return div ? 1193180.0 / div : 0;
}

void audio_sync(const wt_game *g, uint64_t clock)
{
    pos = clock * RATE;
    rd = g->spk_n;
    hz = 0;
    if (dev) SDL_ClearQueuedAudio(dev);
}

void audio_follow(const wt_game *g, uint64_t until)
{
    static float buf[RATE / 4];
    int n = 0;
    if (!dev) return;
    if (until < LATENCY) return;
    until -= LATENCY;
    if (g->spk_n - rd > WT_SPK) rd = g->spk_n - WT_SPK; /* fell behind: skip */
    while (pos < until * RATE && n < (int)(sizeof buf / sizeof buf[0])) {
        while (rd != g->spk_n && g->spk_at[rd % WT_SPK] * RATE <= pos) {
            hz = tone(g->spk_hz[rd % WT_SPK]);
            rd++;
        }
        float v = 0;
        if (hz > 0) {
            phase += hz / RATE;
            phase -= floor(phase);
            v = phase < 0.5 ? 1.f : -1.f;
        }
        lp += 0.45f * (v - lp); /* soften the edges a little, like a real speaker */
        buf[n++] = lp * volume;
        pos += WT_PIT_HZ;
    }
    if (n) SDL_QueueAudio(dev, buf, (Uint32)((size_t)n * sizeof buf[0]));
}
