/* A search bot that plays Welltris through the core and prints its key script, so the
 * differential test reaches line clears, empty-floor bonuses and bonus pieces, which random
 * keys hardly ever do.
 *
 * usage: botgen seed piece_set level preview fixed_keys clock pieces noise noise_seed [fill]
 * fill = 1 starts with a full floor except a few holes in one line: filling them clears the
 * whole floor (one line plus all eight crossing ones).
 * Output: a replay script (see replay.c). */
#include "wt_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXKEYS 20000

static uint64_t out_at[MAXKEYS];
static uint16_t out_key[MAXKEYS];
static int nout;
static uint32_t noise_state;

static uint32_t noise(void)
{
    noise_state = noise_state * 1103515245u + 12345u;
    return (noise_state >> 16) & 0x7fff;
}

/* step the game until it has read all pending keys */
static void settle_keys(wt_game *g)
{
    int guard = 0;
    while ((g->q_len || g->kbuf_len) && !g->game_over && guard++ < 1000) wt_iterate(g, UINT64_MAX);
}

static void press(wt_game *g, uint16_t key, int record)
{
    uint64_t t = g->clock + 1 + noise() % WT_FRAME;
    wt_push_key(g, t, key);
    if (record && nout < MAXKEYS) {
        out_at[nout] = t;
        out_key[nout++] = key;
    }
    settle_keys(g);
}

static uint16_t move_key(const wt_game *g, int dir)
{
    static const uint16_t neg[4] = { 'I', 'J', 'M', 'L' }, pos[4] = { 'M', 'L', 'I', 'J' };
    if (g->fixed_keys) return dir < 0 ? 'J' : 'L';
    return dir < 0 ? neg[g->current_wall & 3] : pos[g->current_wall & 3];
}

/* play a plan: `rot` rotations, `moves` steps (sign = direction), then drop; returns once the
 * next piece is in play (or the game is over) */
static void play(wt_game *g, int rot, int moves, int record)
{
    int i, guard = 0;
    for (i = 0; i < rot; i++) press(g, '5', record);
    for (i = 0; i < abs(moves); i++) press(g, move_key(g, moves), record);
    press(g, ' ', record);
    while (g->piece_active && !g->game_over && guard++ < 10000) wt_iterate(g, UINT64_MAX);
    while (!g->piece_active && !g->game_over && guard++ < 10000) wt_iterate(g, UINT64_MAX);
}

static int floor_count(const wt_game *g)
{
    int x, y, n = 0;
    for (x = 0; x < WT_FLOOR; x++)
        for (y = 0; y < WT_FLOOR; y++) n += g->floor[x][y] != 0;
    return n;
}

static long evaluate(const wt_game *before, const wt_game *g)
{
    int i;
    long v = (long)(g->lines - before->lines) * 1000 + (long)(g->score - before->score);
    if (g->game_over) v -= 100000;
    for (i = 0; i < 4; i++) v -= g->frozen[i] ? 300 : 0;
    v -= floor_count(g) * 5;
    return v;
}

int main(int argc, char **argv)
{
    static wt_game g, t;
    wt_options o;
    unsigned long long clock;
    unsigned long seed, pieces, n;
    int i, noise_pct, fill = 0, nfill = 0;
    uint8_t fx[64] = { 0 }, fy[64] = { 0 }, fc[64] = { 0 };
    if (argc < 10) {
        fprintf(stderr, "usage: %s seed piece_set level preview fixed_keys clock pieces noise noise_seed\n",
                argv[0]);
        return 2;
    }
    seed = strtoul(argv[1], NULL, 10);
    memset(&o, 0, sizeof o);
    o.piece_set = (uint8_t)atoi(argv[2]);
    o.level = (uint8_t)atoi(argv[3]);
    o.preview = (uint8_t)atoi(argv[4]);
    o.fixed_keys = (uint8_t)atoi(argv[5]);
    clock = strtoull(argv[6], NULL, 10);
    pieces = strtoul(argv[7], NULL, 10);
    noise_pct = atoi(argv[8]);
    noise_state = (uint32_t)strtoul(argv[9], NULL, 10);
    if (argc > 10) fill = atoi(argv[10]);
    wt_session_init(&g, (uint32_t)seed, clock);
    wt_new_game(&g, &o);
    // cppcheck-suppress knownConditionTrueFalse ; set from argv above
    if (fill) {
        /* the gap is on an edge line, so a piece can slide off a wall into it */
        int line = noise() % 2 ? 7 : 0, horiz = (int)(noise() % 2), x, y, len = (int)(1 + noise() % 3);
        uint8_t holes = (uint8_t)(((1u << len) - 1u) << (noise() % (unsigned)(9 - len)));
        for (x = 0; x < WT_FLOOR; x++)
            for (y = 0; y < WT_FLOOR; y++) {
                int k = horiz ? y : x;
                if ((horiz ? x : y) == line && (holes & (1u << k))) continue;
                fx[nfill] = (uint8_t)x;
                fy[nfill] = (uint8_t)y;
                fc[nfill] = (uint8_t)(1 + noise() % 15);
                g.floor[x][y] = fc[nfill++];
            }
    }
    for (n = 0; n < pieces && !g.game_over; n++) {
        int best_r = 0, best_m = 0, r, m;
        long best = -1000000000L;
        uint32_t ns = noise_state;
        if ((int)(noise() % 100) < noise_pct) {
            /* a sloppy move now and then: random keys, sometimes alt keys */
            static const uint16_t junk[] = { 'K',          'k',         '5',       'I',         'M',
                                             'J',          'L',         '8',       '2',         '4',
                                             '6',          'x',         WT_KEY_UP, WT_KEY_DOWN, WT_KEY_LEFT,
                                             WT_KEY_RIGHT, WT_KEY_ALT_N };
            int k = (int)(noise() % 6);
            for (i = 0; i < k; i++) press(&g, junk[noise() % (sizeof junk / sizeof junk[0])], 1);
            if (!g.piece_active) {
                n--;
                continue;
            }
        }
        for (r = 0; r < 4; r++) {
            for (m = -10; m <= 10; m++) {
                long v;
                t = g;
                play(&t, r, m, 0);
                v = evaluate(&g, &t);
                if (v > best) {
                    best = v;
                    best_r = r;
                    best_m = m;
                }
            }
        }
        noise_state = ns;
        play(&g, best_r, best_m, 1);
    }
    printf("%lu %d %d %d %d %llu %d", seed, o.piece_set, o.level, o.preview, o.fixed_keys, clock, nfill);
    for (i = 0; i < nfill; i++) printf(" %d %d %d", fx[i], fy[i], fc[i]);
    printf("\n");
    for (i = 0; i < nout; i++) printf("%" PRIu64 " %x\n", out_at[i], out_key[i]);
    return 0;
}
