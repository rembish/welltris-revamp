/* Welltris game logic, reconstructed from WELLTRIS.EXE. Function comments give the original
 * address; see re/NOTES.md for the findings behind them. */
#include "wt_core.h"
#include "wt_tables.h"

#include <string.h>

/* ---- random numbers (Turbo C rand) ---- */

uint16_t wt_rand(wt_game *g)
{
    g->rng = g->rng * 0x015A4E35u + 1u;
    return (uint16_t)((g->rng >> 16) & 0x7fffu);
}

/* ---- time ---- */

uint32_t wt_ticks(const wt_game *g) { return (uint32_t)(g->clock / WT_TICK); }

static void set_clock(wt_game *g, uint64_t c)
{
    if (c > g->clock) g->clock = c;
    /* keys that have arrived by now go into the BIOS buffer; a full buffer drops them */
    while (g->q_len > 0 && g->q_at[g->q_head] <= g->clock) {
        if (g->kbuf_len < WT_KBUF) g->kbuf[g->kbuf_len++] = g->q_key[g->q_head];
        g->q_head = (g->q_head + 1) % WT_KEYQ;
        g->q_len--;
    }
}

/* ticks_after (66af) */
static uint32_t ticks_after(const wt_game *g, uint16_t n) { return wt_ticks(g) + n; }

/* ticks_reached (6689) */
static int ticks_reached(const wt_game *g, uint32_t t) { return wt_ticks(g) >= t; }

/* delay_ticks (011c): busy-waits until the tick counter reaches start + n */
static void delay(wt_game *g, uint16_t n)
{
    if (n) set_clock(g, (uint64_t)(wt_ticks(g) + n) * WT_TICK);
}

/* EGA page flip (69c7): waits for the start of the next vertical retrace */
static void vsync(wt_game *g)
{
    g->flipped = 1;
    set_clock(g, (g->clock / WT_FRAME + 1) * WT_FRAME);
}

/* sound_sweep (03fa): only its timing matters here, the frontend plays the sounds */
static void sweep(wt_game *g, int count, uint16_t gap, uint16_t len)
{
    for (; count > 0; count--) {
        delay(g, len);
        if (gap) delay(g, gap);
    }
}

/* ---- keyboard ---- */

void wt_push_key(wt_game *g, uint64_t at, uint16_t key)
{
    int i;
    if (g->q_len == WT_KEYQ) return;
    i = (g->q_head + g->q_len) % WT_KEYQ;
    g->q_at[i] = at;
    g->q_key[i] = key;
    g->q_len++;
}

/* get_key (0000) */
static uint16_t get_key(wt_game *g)
{
    uint16_t k;
    set_clock(g, g->clock);
    if (!g->kbuf_len) return 0;
    k = g->kbuf[0];
    g->kbuf_len--;
    memmove(g->kbuf, g->kbuf + 1, (size_t)g->kbuf_len * sizeof g->kbuf[0]);
    return k;
}

static void flush_keys(wt_game *g)
{
    while (get_key(g)) {}
}

/* ---- pieces ---- */

/* piece_next_cell (2706): follow the next direction code */
static int next_cell(const uint8_t *codes, int16_t *col, int16_t *row, uint8_t *skip, int16_t *idx)
{
    uint8_t c;
    ++*idx;
    c = codes[*idx];
    if (!c) return 0;
    *skip = 0;
    switch (c & 0x7f) {
    case 1:
        --*col;
        if (*col < 0) *col = 0x1f;
        break;
    case 2: --*row; break;
    case 3:
        ++*col;
        if (*col >= 0x20) *col = 0;
        break;
    case 4: ++*row; break;
    default: break;
    }
    *skip = c & 0x80;
    return 1;
}

/* wall_to_floor (2645) */
void wt_wall_to_floor(int *col, int *row)
{
    int w = *col >> 3, t;
    if (!w) w = 4;
    switch (w) {
    case 1:
        t = *row;
        *row = *col & 7;
        *col = t + 8;
        break;
    case 2:
        *row += 8;
        *col = (7 - *col) & 7;
        break;
    case 3:
        t = *row;
        *row = (7 - *col) & 7;
        *col = -t - 1;
        break;
    default: *row = -*row - 1; break;
    }
}

static int on_floor(int x, int y) { return x >= 0 && x < WT_FLOOR && y >= 0 && y < WT_FLOOR; }

/* piece_collides (1f3b) */
static uint8_t collides(wt_game *g, const wt_piece *p)
{
    int16_t c = p->col, r = p->row, idx = -1;
    uint8_t skip = 0, hit = 0, cross = 0, below = p->row < 0 ? 0xff : 0;
    g->wall_cross = 0;
    while (next_cell(p->codes, &c, &r, &skip, &idx)) {
        if (skip) continue;
        if (r < WT_ROWS) {
            if (r < 0) {
                int x = c, y = r;
                wt_wall_to_floor(&x, &y);
                if (hit || r < g->floor_limit || !on_floor(x, y) || g->floor[x][y])
                    hit = 1;
                else
                    hit = 0;
            } else {
                hit |= g->wall[c][r];
            }
        }
        if ((c >> 3) != g->current_wall) cross = 0xff;
        hit |= g->frozen[c >> 3];
    }
    if (cross && below) g->wall_cross = 0xff;
    return hit;
}

/* draw_cell (3699) with its store side effect; returns what the original leaves in AX */
static uint8_t put_cell(wt_game *g, int col, int row, uint8_t color, uint8_t store)
{
    if (row >= 0 && row < WT_ROWS) {
        if (store) g->wall[col][row] = color;
        return 0xff;
    }
    if (row < 0) {
        wt_wall_to_floor(&col, &row);
        if (!on_floor(col, row)) {
            g->anomaly = 1;
            return 0;
        }
        if (store) g->floor[col][row] = color;
        return 0;
    }
    return color; /* rows above the wall: AX still holds the colour argument */
}

/* draw_piece (35de): 0 if the piece is all on the floor, else its highest row (at least 1) */
static uint8_t draw_piece(wt_game *g, const wt_piece *p, int with_color, uint8_t store)
{
    int16_t c = p->col, r = p->row, idx = 0;
    uint8_t skip = 0, acc = 0, top = 0, color = with_color ? p->color : 0;
    do {
        if (!skip) {
            acc |= put_cell(g, c, r, color, store);
            if ((int)top < r) top = (uint8_t)r;
        }
    } while (next_cell(p->codes, &c, &r, &skip, &idx));
    if (acc) acc = top ? top : 1;
    return acc;
}

int wt_piece_cells(const wt_piece *p, int *col, int *row, int max)
{
    int16_t c = p->col, r = p->row, idx = 0;
    uint8_t skip = 0;
    int n = 0;
    do {
        if (!skip && n < max) {
            col[n] = c;
            row[n] = r;
            n++;
        }
    } while (next_cell(p->codes, &c, &r, &skip, &idx));
    return n;
}

/* piece_rotate (1def): up to `tries` quarter turns while the piece fits. `garbage` is the
 * uninitialised result the original returns for pieces that never rotate. */
static uint8_t rotate(wt_game *g, wt_piece *p, uint16_t tries, uint8_t garbage)
{
    uint8_t hit = garbage;
    int i;
    if (p->rec != 0x42 && p->rec != 0x6e) {
        do {
            if (++p->rot > 3) p->rot = 0;
            for (i = 1; p->codes[i]; i++) {
                if ((p->codes[i] & 0x7f) == 5) continue;
                p->codes[i]++;
                if ((p->codes[i] & 0x7f) > 4) p->codes[i] = (uint8_t)((p->codes[i] & 0x80) | 1);
            }
            hit = collides(g, p);
        } while (!hit && !g->wall_cross && --tries != 0);
        if (hit || g->wall_cross) {
            if (p->rot-- == 0) p->rot = 3;
            for (i = 0; p->codes[i]; i++) {
                if ((p->codes[i] & 0x7f) == 5) continue;
                p->codes[i]--;
                if ((p->codes[i] & 0x7f) == 0) p->codes[i] = (uint8_t)((p->codes[i] & 0x80) | 4);
            }
        }
    }
    return (hit || g->wall_cross) ? 1 : 0;
}

/* piece_fall (1cf4) */
static uint8_t piece_fall(wt_game *g, wt_piece *p)
{
    uint8_t hit;
    p->row--;
    hit = collides(g, p);
    if (hit) p->row++;
    return hit;
}

/* piece_left (1d21) / piece_right (1d88) */
static int move_lr(wt_game *g, wt_piece *p, int dir)
{
    uint8_t hit;
    if (g->lock_lr) return -1;
    p->col = (int16_t)((p->col + dir) & 0x1f);
    hit = collides(g, p);
    if (hit) p->col = (int16_t)((p->col - dir) & 0x1f);
    return hit;
}

/* build_piece (248d) */
static void build_piece_at(wt_piece *p, int16_t rec)
{
    const uint8_t *r = wt_piece_data + rec;
    int i = 1;
    memset(p, 0, sizeof *p);
    p->color = r[0];
    p->info = r[1];
    p->codes[0] = 5;
    for (r += 2; r[-1] && i < (int)sizeof p->codes; r++) p->codes[i++] = *r;
    p->rec = rec;
}

static int16_t rec_offset(int cls, int index)
{
    return (int16_t)(wt_class_offset[cls] + wt_class_rec_size[cls] * index);
}

void wt_build_piece(wt_piece *p, int cls, int index)
{
    build_piece_at(p, rec_offset(cls, index));
    p->cls = (uint8_t)cls;
}

/* choose_piece (2518): sets the next piece's class and index, returns its global index */
static uint16_t choose_piece(wt_game *g)
{
    uint8_t set = g->bonus_piece ? 2 : g->piece_set;
    int i, sum = 0;
    if (set == 0) {
        g->piece_class = (uint8_t)(wt_rand(g) % 2);
        if (g->piece_class == 0)
            g->piece_index = (uint8_t)(wt_rand(g) % 3);
        else
            g->piece_index = (uint8_t)(wt_rand(g) % 7);
    } else if (set == 1) {
        g->piece_class = 1;
        g->piece_index = (uint8_t)(wt_rand(g) % 7);
    } else {
        if (!g->bonus_piece)
            g->piece_class = (uint8_t)(wt_rand(g) % 3);
        else
            g->piece_class = 2;
        if (g->piece_class == 0)
            g->piece_index = (uint8_t)(wt_rand(g) % 3);
        else if (g->piece_class == 1)
            g->piece_index = (uint8_t)(wt_rand(g) % 7);
        else
            g->piece_index = (uint8_t)(wt_rand(g) % 18);
    }
    if (g->bonus_piece && g->piece_set == 2) {
        g->piece_index = 0;
        g->piece_class = 3;
    }
    for (i = 0; i < g->piece_class; i++) sum += wt_class_count[i];
    return (uint16_t)(g->piece_index + sum);
}

/* spawn_piece (2378), without its copy-protection check */
static void spawn_piece(wt_game *g)
{
    wt_piece *p = &g->piece;
    uint16_t tries;
    build_piece_at(p, rec_offset(g->piece_class, g->piece_index));
    p->row = 12;
    do {
        int a = (wt_rand(g) % 4) << 3;
        p->col = (int16_t)(a + wt_rand(g) % 3 + 3);
    } while (collides(g, p));
    while (g->frozen[p->col >> 3]) {
        p->col = (int16_t)(p->col + 8);
        if (p->col > 0x20) p->col = (int16_t)(p->col - 0x20);
    }
    tries = (uint16_t)(wt_rand(g) % 4 + 1);
    rotate(g, p, tries, 0);
    p->cls = g->piece_class;
}

/* ---- the well ---- */

static void free_piece(wt_game *g, int16_t i)
{
    if (i >= 0) g->pool_used[i] = 0;
}

/* store_wall_piece (2dc7): keep a copy, its floor cells marked as gone */
static void store_wall_piece(wt_game *g, const wt_piece *src)
{
    int16_t i, idx = 0;
    uint8_t skip = 0;
    int slot = src->col * 15 + src->row + 3;
    wt_piece *p;
    for (i = 0; i < WT_POOL && g->pool_used[i]; i++) {}
    if (i == WT_POOL || slot < 0 || slot >= WT_SLOTS) {
        g->anomaly = 1;
        return;
    }
    free_piece(g, g->slot[slot]);
    g->pool_used[i] = 1;
    g->slot[slot] = i;
    p = &g->pool[i];
    *p = *src;
    do {
        if (!skip && p->row < 0) p->codes[idx] |= 0x80;
    } while (next_cell(p->codes, &p->col, &p->row, &skip, &idx));
    p->col = src->col;
    p->row = src->row;
}

/* freeze_walls_of_piece (2eb9) */
static void freeze_walls(wt_game *g, const wt_piece *p, uint8_t *out)
{
    int16_t c = p->col, r = p->row, idx = -1;
    uint8_t skip = 0;
    memset(out, 0, 4);
    while (next_cell(p->codes, &c, &r, &skip, &idx)) {
        if (skip || r < 0) continue;
        g->frozen[c >> 3] = 0xff;
        out[c >> 3] = 0xff;
    }
}

/* update_current_wall (2f4a) */
static void update_current_wall(wt_game *g, const wt_piece *p)
{
    int16_t c = p->col, r = p->row, idx = -1;
    uint8_t skip = 0, found = 0;
    while (next_cell(p->codes, &c, &r, &skip, &idx)) {
        if (!skip && (c >> 3) == g->current_wall) found = 0xff;
        if (found) break;
    }
    if (!found) g->current_wall = (uint8_t)(c >> 3);
}

/* settle_wall_pieces (2b42): pieces stored on thawed walls slide down */
static uint8_t settle(wt_game *g)
{
    uint8_t moved = 0;
    int row, col;
    for (row = -3; row < WT_ROWS; row++) {
        for (col = 0; col < WT_COLS; col++) {
            int16_t c = (int16_t)col, r = (int16_t)row, idx = -1, pi = g->slot[col * 15 + row + 3], r0;
            uint8_t skip = 0, stuck = 0, kept = 0;
            wt_piece *p;
            if (pi < 0) continue;
            p = &g->pool[pi];
            while (next_cell(p->codes, &c, &r, &skip, &idx)) {
                if (skip || r < 0) continue;
                g->wall[c][r] = 0;
                if (g->frozen[c >> 3]) stuck = 0xff;
            }
            g->slot[col * 15 + row + 3] = -1;
            g->floor_limit = -4;
            r0 = p->row;
            if (!stuck)
                while (!piece_fall(g, p)) {}
            if (p->row != r0) moved = 0xff;
            g->floor_limit = -8;
            c = p->col;
            r = p->row;
            skip = 0;
            idx = -1;
            while (next_cell(p->codes, &c, &r, &skip, &idx)) {
                if (skip) continue;
                if (r >= 0) {
                    g->wall[c][r] = p->color;
                    kept = 0xff;
                } else {
                    int x = c, y = r;
                    wt_wall_to_floor(&x, &y);
                    if (on_floor(x, y))
                        g->floor[x][y] = p->color;
                    else
                        g->anomaly = 1;
                    p->codes[idx] |= 0x80;
                }
            }
            if (!kept) {
                free_piece(g, pi);
            } else {
                int s = p->col * 15 + p->row + 3;
                if (p->row + 3 < 0 || s >= WT_SLOTS) { /* fatal_exit(4): "Programmer Error" */
                    g->anomaly = 1;
                    free_piece(g, pi);
                } else {
                    free_piece(g, g->slot[s]);
                    g->slot[s] = pi;
                }
            }
        }
    }
    return moved;
}

/* find_full_floor_lines (27be): clears them, counts both directions */
static uint8_t find_full_lines(wt_game *g, uint8_t *rows, uint8_t *cols)
{
    int i, j;
    *rows = *cols = 0;
    memset(g->row_full, 0xff, sizeof g->row_full);
    memset(g->col_full, 0xff, sizeof g->col_full);
    for (i = 0; i < WT_FLOOR; i++)
        for (j = 0; j < WT_FLOOR; j++)
            if (!g->floor[i][j]) g->col_full[j] = g->row_full[i] = 0;
    for (i = 0; i < WT_FLOOR; i++) {
        if (g->row_full[i]) {
            ++*rows;
            for (j = 0; j < WT_FLOOR; j++) g->floor[i][j] = 0;
        }
        if (g->col_full[i]) {
            ++*cols;
            for (j = 0; j < WT_FLOOR; j++) g->floor[j][i] = 0;
        }
    }
    return *rows | *cols;
}

/* collapse_floor_lines (28ed): cleared lines close up towards the centre */
static uint8_t collapse(wt_game *g, uint8_t rows, uint8_t cols)
{
    uint8_t moved = 0;
    int i, j, k;
    if (rows) {
        for (i = 0; i < 4; i++) {
            if (!g->row_full[i]) continue;
            for (j = i; j; j--)
                for (k = 0; k < WT_FLOOR; k++) moved |= g->floor[j][k] = g->floor[j - 1][k];
            for (k = 0; k < WT_FLOOR; k++) g->floor[0][k] = 0;
        }
        for (i = 7; i > 3; i--) {
            if (!g->row_full[i]) continue;
            for (j = i; j < 7; j++)
                for (k = 0; k < WT_FLOOR; k++) moved |= g->floor[j][k] = g->floor[j + 1][k];
            for (k = 0; k < WT_FLOOR; k++) g->floor[7][k] = 0;
        }
    }
    if (cols) {
        for (i = 0; i < 4; i++) {
            if (!g->col_full[i]) continue;
            for (j = i; j; j--)
                for (k = 0; k < WT_FLOOR; k++) moved |= g->floor[k][j] = g->floor[k][j - 1];
            for (k = 0; k < WT_FLOOR; k++) g->floor[k][0] = 0;
        }
        for (i = 7; i > 3; i--) {
            if (!g->col_full[i]) continue;
            for (j = i; j < 7; j++)
                for (k = 0; k < WT_FLOOR; k++) moved |= g->floor[k][j] = g->floor[k][j + 1];
            for (k = 0; k < WT_FLOOR; k++) g->floor[k][7] = 0;
        }
    }
    return moved;
}

/* check_floor_empty_bonus (2fd5) */
static uint8_t floor_empty_bonus(wt_game *g)
{
    int i, j;
    g->floor_bonus = 0xff;
    for (i = 0; i < WT_FLOOR; i++)
        for (j = 0; j < WT_FLOOR; j++)
            if (g->floor[i][j]) g->floor_bonus = 0;
    for (i = 0; i < 4; i++) g->floor_bonus = (!g->floor_bonus || g->frozen[i]) ? 0 : 1;
    return g->floor_bonus;
}

/* score_for_piece (2084) */
static uint32_t score_for_piece(wt_game *g, const wt_piece *p, uint8_t n, uint8_t bonus)
{
    uint32_t s = (uint32_t)wt_score_words[7 + g->level] + wt_score_words[12 + p->cls];
    if (p->rec == 0x10a) s += 30;
    s += (uint32_t)((g->drop_rows + 1) * 2 * (g->level + 1));
    s += (uint16_t)(wt_score_words[n & 15] + g->piece_set * n * 25);
    if (g->preview_on) s = s < 5 ? 0 : s - 5;
    if (g->lines_to_level == 0) {
        s += (uint32_t)(g->level * 25 + 50);
        if (!g->landed) s += g->piece_set == 2 ? 50 : 20;
    }
    if (bonus) s += g->piece_set == 2 ? 150 : 75;
    if (g->floor_bonus) {
        s += 500;
        g->floor_bonus = 0;
    }
    return s;
}

/* ---- effects (EGA timings) ---- */

/* clear_lines_effect (3afc) */
static void clear_lines_effect(wt_game *g, uint8_t rows, uint8_t cols)
{
    int i;
    g->effects |= WT_FX_LINES;
    for (i = 0; i < 0x1d; i++) {
        vsync(g);
        sweep(g, 1, 0, 1);
    }
    delay(g, 0x38);
    i = collapse(g, rows, cols);
    vsync(g);
    if (i) sweep(g, 8, 0, 2);
    delay(g, 0x38);
    i = settle(g);
    vsync(g);
    if (i) {
        g->effects |= WT_FX_SETTLE;
        sweep(g, 8, 0, 2);
    }
}

/* freeze_walls_effect (3cc5) */
static void freeze_effect(wt_game *g, const uint8_t *walls, uint32_t fx)
{
    int i;
    g->effects |= fx;
    memcpy(g->fx_walls, walls, 4);
    for (i = 0; i < 0x1d; i++) {
        vsync(g);
        sweep(g, 1, 0, 1);
    }
}

/* ---- keys ---- */

static int alt_keys(wt_game *g, uint16_t key, int *event)
{
    switch (key) {
    case WT_KEY_ALT_A:
    case WT_KEY_ALT_R:
        delay(g, 8);
        *event = key == WT_KEY_ALT_A ? WT_EV_ABORT : WT_EV_RESTART;
        return 1;
    case WT_KEY_ALT_I: g->skip_level = 0xff; return 1;
    case WT_KEY_ALT_P:
        delay(g, 8);
        *event = WT_EV_PAUSE;
        return 1;
    case WT_KEY_ALT_N:
        g->preview_on ^= 0xff;
        if (g->preview_on) g->preview_drawn = 0;
        return 1;
    case WT_KEY_ALT_M: g->fixed_keys ^= 0xff; return 1;
    default: return 0;
    }
}

static int is_key(uint16_t k, uint16_t a, uint16_t b, uint16_t c, uint16_t d) { return k == a || k == b || k == c || k == d; }

/* handle_play_key (149f): one key, the rest of the buffer is thrown away */
static uint8_t play_key(wt_game *g, int *event)
{
    uint8_t act = 0;
    uint16_t key = get_key(g);
    int up, down, left, right, dir = 0;
    if (!key) return 0;
    if (key == WT_KEY_ALT_Q) {
        *event = WT_EV_QUIT;
        goto drain;
    }
    if (key == WT_KEY_ALT_S) {
        g->sound_on ^= 0xff;
        goto drain;
    }
    if (alt_keys(g, key, event) || !g->piece_active) goto drain;
    act = 0xff;
    if (key == 'K' || key == '5' || key == 'k') {
        /* the key code doubles as the number of quarter turns (see notes) */
        if (rotate(g, &g->piece, key, 0)) {
            g->effects |= WT_FX_MOVE_FAIL;
            sweep(g, 1, 0, 2);
        }
        goto drain;
    }
    if (key == ' ') {
        g->dropping = 0xff;
        g->drop_deadline = ticks_after(g, 3);
        goto drain;
    }
    up = is_key(key, 'I', '8', WT_KEY_UP, 'i');
    down = is_key(key, 'M', '2', WT_KEY_DOWN, 'm');
    left = is_key(key, 'J', '4', WT_KEY_LEFT, 'j');
    right = is_key(key, 'L', '6', WT_KEY_RIGHT, 'l');
    if (g->fixed_keys) {
        dir = left ? -1 : right ? 1 : 0;
        if (!dir) act = 0;
    } else {
        switch (g->current_wall) {
        case 0: dir = up ? -1 : down ? 1 : 0; break;
        case 1: dir = left ? -1 : right ? 1 : 0; break;
        case 2: dir = up ? 1 : down ? -1 : 0; break;
        case 3: dir = left ? 1 : right ? -1 : 0; break;
        default: act = 0; break;
        }
    }
    if (dir && move_lr(g, &g->piece, dir)) {
        g->effects |= WT_FX_MOVE_FAIL;
        sweep(g, 1, 0, 2);
        act = 0;
    }
drain:
    if (*event == WT_EV_NONE) flush_keys(g);
    return act;
}

/* ---- game ---- */

void wt_session_init(wt_game *g, uint32_t rng_seed, uint64_t clock)
{
    memset(g, 0, sizeof *g);
    g->rng = rng_seed;
    g->clock = clock;
    g->floor_limit = -8;
    g->sound_on = 0xff;
    memset(g->slot, 0xff, sizeof g->slot);
}

void wt_startup_rng(wt_game *g)
{
    wt_rand(g);
    wt_rand(g);
}

/* options_menu (4c32) on exit, then game_init (0739) */
void wt_new_game(wt_game *g, const wt_options *o)
{
    int i;
    g->piece_set = o->piece_set;
    g->sound_on = o->sound ? 0xff : 0;
    g->fixed_keys = o->fixed_keys ? 0xff : 0;
    g->preview_on = o->preview ? 0xff : 0;
    g->level = o->level;
    g->fall_delay = wt_fall_delay0;
    for (i = 0; i < o->level; i++) g->fall_delay = (uint16_t)(g->fall_delay - wt_level_speedup[i]);

    g->stored = g->dropping = g->lock_lr = g->landed = 0;
    g->restart = g->overflow = g->bonus_piece = 0;
    g->preview_drawn = g->need_next = g->piece_active = 0xff;
    g->score = g->lines = 0;
    g->game_over = g->aborted = g->floor_bonus = 0;
    g->old.col = -1;
    memset(g->wall, 0, sizeof g->wall);
    memset(g->floor, 0, sizeof g->floor);
    memset(g->slot, 0xff, sizeof g->slot); /* clear_well: copies are not freed, just forgotten */
    memset(g->pool_used, 0, sizeof g->pool_used);
    memset(g->frozen, 0, sizeof g->frozen);
    memset(g->frozen_count, 0, sizeof g->frozen_count);
    choose_piece(g);
    spawn_piece(g);
    g->drop_rows = (uint8_t)g->piece.row;
    g->lines_to_level = (int16_t)((g->level + 1) * 15);
    if (g->level > 2) g->lines_to_level = (int16_t)(g->lines_to_level + (g->level - 2) * 5);
    g->fall_deadline = ticks_after(g, g->fall_delay);
    flush_keys(g);
}

void wt_resume(wt_game *g, uint64_t clock) { set_clock(g, clock); }

void wt_end_game(wt_game *g, int restart)
{
    if (g->sound_on) { /* 1ac8: the "abort" tune */
        sweep(g, 0xf, 1, 2);
        sweep(g, 0x19, 1, 2);
    }
    g->game_over = 0xff;
    g->aborted = 0xff;
    if (restart) g->restart = 0xff;
}

/* the piece stopped last pass: thaw walls, clear lines, score, next piece */
static void next_turn(wt_game *g)
{
    uint8_t thawed[4], rows, cols, n;
    int i, any = 0;
    for (i = 0; i < 4; i++) {
        thawed[i] = 0;
        if (g->frozen[i] && ++g->frozen_count[i] > 3) {
            g->frozen_count[i] = 0;
            g->frozen[i] = 0;
            any = 1;
            thawed[i] = 0xff;
        }
    }
    if (any) {
        freeze_effect(g, thawed, WT_FX_THAW);
        delay(g, 0x38);
        i = settle(g);
        vsync(g);
        if (i) {
            g->effects |= WT_FX_SETTLE;
            sweep(g, 8, 0, 2);
        }
    }
    if (find_full_lines(g, &rows, &cols)) {
        clear_lines_effect(g, rows, cols);
        g->lines_to_level = (int16_t)(g->lines_to_level - (rows + cols));
    }
    n = (uint8_t)(rows + cols);
    if (!g->stored) {
        uint32_t s;
        if (n && floor_empty_bonus(g)) {
            g->effects |= WT_FX_FLOOR_EMPTY;
            sweep(g, 8, 1, 4);
            delay(g, 8);
            sweep(g, 0xf, 1, 4);
            sweep(g, 1, 0, 0x3c);
        }
        s = g->score + score_for_piece(g, &g->piece, n, g->bonus_piece);
        g->score = s;
        if (s >= 1000000000u) g->overflow = 0xff;
    }
    if (n) g->lines += n;
    if (g->bonus_piece) {
        sweep(g, 5, 1, 10);
        g->level_up = 0xff;
        g->bonus_piece = 0;
    }
    if (g->lines_to_level < 1 && !g->overflow) {
        uint8_t idx = g->piece_index, cls = g->piece_class;
        g->bonus_piece = 0xff;
        g->effects |= WT_FX_LEVEL_BONUS;
        sweep(g, 1, 10, 0xf); /* 42d2 */
        sweep(g, 1, 0, 0xf);
        sweep(g, 1, 0, 0x28);
        choose_piece(g);
        spawn_piece(g);
        g->piece_index = idx;
        g->piece_class = cls;
        g->lines_to_level = (int16_t)(g->lines_to_level + (g->level > 2 ? 0x14 : 0xf));
    } else {
        spawn_piece(g);
        g->need_next = 0xff;
    }
    g->old = g->piece;
    g->stored = g->dropping = g->lock_lr = g->landed = 0;
    g->fall_deadline = ticks_after(g, g->fall_delay);
    g->preview_drawn = g->piece_active = 0xff;
    g->drop_rows = (uint8_t)g->piece.row;
    flush_keys(g);
}

/* game_iteration (0dcc) */
static void iteration(wt_game *g, int *event)
{
    wt_piece start = g->piece;
    uint8_t forced = 0, top, walls[4];
    int reached = 0;
    update_current_wall(g, &g->piece);
    if (!g->dropping) {
        forced = play_key(g, event);
        if (*event != WT_EV_NONE) return;
    }
    if (g->game_over) return;
    if (g->need_next) {
        g->next_piece = choose_piece(g);
        g->preview_drawn = 0;
        g->need_next = 0;
    }
    if (g->preview_on && !g->preview_drawn) g->preview_drawn = 0xff;
    if (!g->dropping) reached = ticks_reached(g, g->fall_deadline);
    if (reached || (g->dropping && ticks_reached(g, g->drop_deadline)) || forced) {
        if (!g->piece_active) {
            next_turn(g);
        } else {
            if (reached || g->dropping) {
                g->landed = piece_fall(g, &g->piece);
                if (!g->dropping) {
                    g->fall_deadline = ticks_after(g, g->fall_delay);
                    if (g->drop_rows) g->drop_rows--;
                } else {
                    g->drop_deadline = ticks_after(g, 3);
                }
                if (g->piece.row < 0) g->lock_lr = 0xff;
                if (g->landed) {
                    g->piece_active = 0;
                    g->effects |= WT_FX_LANDED;
                }
            }
            if (g->old.col != -1) draw_piece(g, &g->old, 0, 0xff);
            g->old = start;
        }
        top = draw_piece(g, &g->piece, 1, g->landed);
        if (top && g->landed) {
            if (top < 0xb) {
                store_wall_piece(g, &g->piece);
                freeze_walls(g, &g->piece, walls);
                freeze_effect(g, walls, WT_FX_FREEZE);
                g->stored = 0xff;
            } else {
                g->game_over = 0xff;
            }
        }
        if ((g->skip_level && !g->piece_active) || g->level_up) {
            if (g->level < 4) {
                int16_t v = g->lines_to_level;
                if (!g->level_up) v = (int16_t)(g->lines_to_level + (g->level > 1 ? 0x14 : 0xf));
                g->lines_to_level = v;
                g->level_up = 0xff;
                g->fall_delay = (uint16_t)(g->fall_delay - wt_level_speedup[g->level]);
                g->level++;
                g->effects |= WT_FX_LEVEL_UP;
            } else {
                g->level_up = 0;
                g->skip_level = 0;
            }
        }
        vsync(g);
        if (g->level_up) g->level_up = g->skip_level = 0;
    }
    if (!g->game_over) g->game_over = g->frozen[0] & g->frozen[1] & g->frozen[2] & g->frozen[3];
}

/* the clock of the next thing that can change the game */
static uint64_t next_event(const wt_game *g)
{
    uint64_t t;
    if (g->dropping) return (uint64_t)g->drop_deadline * WT_TICK;
    if (g->kbuf_len) return g->clock;
    t = (uint64_t)g->fall_deadline * WT_TICK;
    if (g->q_len && g->q_at[g->q_head] < t) t = g->q_at[g->q_head];
    return t;
}

int wt_iterate(wt_game *g, uint64_t limit)
{
    int event = WT_EV_NONE;
    if (g->game_over) return WT_EV_GAME_OVER;
    g->flipped = 0;
    iteration(g, &event);
    if (event != WT_EV_NONE) return event;
    if (g->game_over) return WT_EV_GAME_OVER;
    if (!g->flipped) { /* nothing happened: the original spins until the next event */
        uint64_t t = next_event(g);
        set_clock(g, t < limit ? t : limit);
    }
    return WT_EV_NONE;
}
