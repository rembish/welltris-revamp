/* Replay a key script through the core, printing the state after every pass of the play loop
 * in the same format as re/emu/wtemu.py, for re/emu/difftest.py.
 *
 * Script: "seed piece_set level preview fixed_keys clock", "n x y colour..." (prefilled floor
 * cells), then "clock key_hex" lines. */
#include "wt_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex(const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) printf("%02x", b[i]);
}

/* the piece as 0x1a bytes, codes after the terminator zeroed (the original leaves garbage) */
static void piece_hex(const wt_piece *p)
{
    uint8_t b[0x1a];
    int i, end = 0;
    memset(b, 0, sizeof b);
    b[0] = p->color;
    b[1] = p->info;
    b[2] = p->rot;
    for (i = 0; i < 15 && !end; i++) {
        b[3 + i] = p->codes[i];
        end = !p->codes[i];
    }
    b[0x12] = p->cls;
    b[0x14] = (uint8_t)p->rec;
    b[0x15] = (uint8_t)((uint16_t)p->rec >> 8);
    b[0x16] = (uint8_t)p->col;
    b[0x17] = (uint8_t)((uint16_t)p->col >> 8);
    b[0x18] = (uint8_t)p->row;
    b[0x19] = (uint8_t)((uint16_t)p->row >> 8);
    hex(b, sizeof b);
}

static void snapshot(const wt_game *g)
{
    uint8_t fr[8], flags[6];
    int i, first = 1;
    for (i = 0; i < 4; i++) {
        fr[2 * i] = g->frozen[i];
        fr[2 * i + 1] = g->frozen_count[i];
    }
    flags[0] = g->piece_active;
    flags[1] = g->dropping;
    flags[2] = g->need_next;
    flags[3] = g->preview_drawn;
    flags[4] = g->lock_lr;
    flags[5] = g->landed;
    printf("%" PRIu64 " %d ", g->clock, g->game_over);
    piece_hex(&g->piece);
    putchar(' ');
    hex(&g->wall[0][0], sizeof g->wall);
    putchar(' ');
    hex(&g->floor[0][0], sizeof g->floor);
    putchar(' ');
    hex(fr, sizeof fr);
    printf(" %" PRId32 " %" PRId32 " %d %d %d %" PRId32 " %" PRId32 " ", (int32_t)g->score, (int32_t)g->lines,
           g->level, g->lines_to_level, (int16_t)g->fall_delay, (int32_t)g->fall_deadline,
           (int32_t)g->drop_deadline);
    hex(flags, sizeof flags);
    printf(" %d %d %d %" PRId32 " ", g->current_wall, g->drop_rows, (int16_t)g->next_piece, (int32_t)g->rng);
    for (i = 0; i < WT_SLOTS; i++) {
        if (g->slot[i] < 0) continue;
        printf("%s%d,%d:", first ? "" : ";", i / 15, i % 15 - 3);
        piece_hex(&g->pool[g->slot[i]]);
        first = 0;
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    static wt_game g;
    wt_options o;
    FILE *f;
    unsigned long seed, set, level, preview, fixed, max_iter;
    unsigned long long clock;
    unsigned key;
    unsigned long n, nk = 0, ki = 0, nfill, i;
    unsigned fx[64], fy[64], fc[64];
    unsigned long fxc[16] = { 0 };
    int b;
    static uint64_t at[1 << 16];
    static uint16_t keys[1 << 16];
    if (argc < 3) {
        fprintf(stderr, "usage: %s script max_iterations\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "r");
    if (!f) return 1;
    if (fscanf(f, "%lu %lu %lu %lu %lu %llu %lu", &seed, &set, &level, &preview, &fixed, &clock, &nfill) !=
        7) {
        fclose(f);
        return 1;
    }
    for (i = 0; i < nfill && i < 64; i++)
        if (fscanf(f, "%u %u %u", &fx[i], &fy[i], &fc[i]) != 3) {
            fclose(f);
            return 1;
        }
    max_iter = strtoul(argv[2], NULL, 10);
    wt_session_init(&g, (uint32_t)seed, clock);
    memset(&o, 0, sizeof o);
    o.piece_set = (uint8_t)set;
    o.level = (uint8_t)level;
    o.preview = (uint8_t)preview;
    o.fixed_keys = (uint8_t)fixed;
    while (nk < (1 << 16) && fscanf(f, "%llu %x", &clock, &key) == 2) {
        at[nk] = clock;
        keys[nk++] = (uint16_t)key;
    }
    fclose(f);
    /* the core's queue is small (a frontend pushes keys as they come): keep it topped up */
    for (; ki < nk && g.q_len < WT_KEYQ; ki++) wt_push_key(&g, at[ki], keys[ki]);
    wt_new_game(&g, &o);
    for (i = 0; i < nfill && i < 64; i++) g.floor[fx[i] & 7][fy[i] & 7] = (uint8_t)fc[i];
    snapshot(&g);
    for (n = 0; n < max_iter && !g.game_over; n++) {
        for (; ki < nk && g.q_len < WT_KEYQ; ki++) wt_push_key(&g, at[ki], keys[ki]);
        wt_iterate(&g, UINT64_MAX);
        snapshot(&g);
        for (b = 0; b < 16; b++)
            if (g.effects & (1u << b)) fxc[b]++;
        g.effects = 0;
    }
    printf(
        "end anomaly=%d fail=%lu lines=%lu freeze=%lu thaw=%lu settle=%lu empty=%lu bonus=%lu levelup=%lu\n",
        g.anomaly, fxc[0], fxc[1], fxc[2], fxc[3], fxc[4], fxc[5], fxc[6], fxc[7]);
    return 0;
}
