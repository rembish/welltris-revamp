/* Welltris game logic, reconstructed from WELLTRIS.EXE (Spectrum HoloByte, 1989).
 *
 * Deterministic and free of I/O. Time is counted in PIT input clocks (1193182 Hz): the game
 * tick is 6375 clocks (187.17 Hz, the rate the original programs into the PIT) and the
 * display frame is WT_FRAME clocks (the EGA page flip waits for vertical retrace).
 *
 * Driving it: wt_session_init once, wt_new_game per game, wt_push_key as keys arrive
 * (stamped with the clock they arrived at) and wt_iterate in a loop. Each wt_iterate runs one
 * pass of the original play loop; blocking effects (sounds, line-clear animations) advance
 * g->clock inside it, idle passes jump it to the next event (bounded by `limit`).
 */
#ifndef WT_CORE_H
#define WT_CORE_H

#include <stdint.h>

#define WT_TICK   6375u  /* PIT clocks per game tick */
#define WT_FRAME  19886u /* PIT clocks per 60 Hz frame */
#define WT_PIT_HZ 1193182u

#define WT_COLS  32 /* four walls of 8 columns, one ring */
#define WT_ROWS  12
#define WT_FLOOR 8
#define WT_SLOTS (WT_COLS * 15) /* stored wall pieces by (column, row + 3) */
#define WT_POOL  (WT_SLOTS + 2)
#define WT_KEYQ  256
#define WT_KBUF  15   /* BIOS type-ahead buffer */
#define WT_SPK   1024 /* speaker log entries */

/* get_key() codes: ASCII, or scan code | 0x8000 for extended keys */
#define WT_KEY_UP    0x8048u
#define WT_KEY_DOWN  0x8050u
#define WT_KEY_LEFT  0x804bu
#define WT_KEY_RIGHT 0x804du
#define WT_KEY_ALT_Q 0x8010u
#define WT_KEY_ALT_R 0x8013u
#define WT_KEY_ALT_I 0x8017u
#define WT_KEY_ALT_P 0x8019u
#define WT_KEY_ALT_A 0x801eu
#define WT_KEY_ALT_S 0x801fu
#define WT_KEY_ALT_N 0x8031u
#define WT_KEY_ALT_M 0x8032u

/* Piece, laid out like the original's 0x1a-byte struct. codes[] is the chain of direction
 * codes (1 left, 2 down, 3 right, 4 up, 5 origin; bit 7 = no cell here) ending in 0. */
typedef struct {
    uint8_t color, info, rot;
    uint8_t codes[15];
    uint8_t cls, pad;
    int16_t rec, col, row;
} wt_piece;

typedef struct {
    uint8_t piece_set; /* 0 easy, 1 normal (tetrominoes), 2 hard */
    uint8_t level;     /* starting level 0..4 */
    uint8_t preview;   /* next piece shown (costs 5 points per piece) */
    uint8_t fixed_keys;
    uint8_t sound;
} wt_options;

enum {
    WT_EV_NONE,
    WT_EV_PAUSE,   /* Alt-P: frontend pauses; wt_resume when done */
    WT_EV_ABORT,   /* Alt-A: frontend confirms, then wt_end_game(g, 0) */
    WT_EV_RESTART, /* Alt-R: frontend confirms, then wt_end_game(g, 1) */
    WT_EV_QUIT,    /* Alt-Q: frontend confirms and quits */
    WT_EV_GAME_OVER
};

typedef struct {
    /* session state (survives games, like the original's globals) */
    uint32_t rng;
    uint64_t clock;
    int16_t floor_limit;  /* ds:1096 */
    uint8_t current_wall; /* ds:10f4 */
    uint8_t wall_cross;   /* ds:1acc */
    uint8_t skip_level;   /* ds:131c */
    uint8_t level_up;     /* ds:111f */
    uint8_t sound_on;     /* ds:000d */
    uint8_t preview_on;   /* ds:000f */
    uint8_t fixed_keys;   /* ds:0010 */
    uint8_t piece_set;    /* ds:109f */

    /* keyboard */
    uint64_t q_at[WT_KEYQ];
    uint16_t q_key[WT_KEYQ];
    int q_head, q_len;
    uint16_t kbuf[WT_KBUF];
    int kbuf_len;

    /* game state */
    uint8_t wall[WT_COLS][WT_ROWS];     /* ds:1128 */
    uint8_t floor[WT_FLOOR][WT_FLOOR];  /* ds:12b2 */
    uint8_t frozen[4], frozen_count[4]; /* ds:12aa */
    int16_t slot[WT_SLOTS];             /* ds:1320: index into pool or -1 */
    wt_piece pool[WT_POOL];
    uint8_t pool_used[WT_POOL];
    wt_piece piece;                   /* ds:12f2 */
    wt_piece old;                     /* ds:10fa: position drawn last pass */
    uint32_t score;                   /* ds:109a */
    uint32_t lines;                   /* ds:002c */
    uint8_t level;                    /* ds:1098 */
    int16_t lines_to_level;           /* ds:131e */
    uint16_t fall_delay;              /* ds:0026 */
    uint32_t fall_deadline;           /* ds:1120 */
    uint32_t drop_deadline;           /* ds:1124 */
    uint8_t game_over;                /* ds:000c */
    uint8_t piece_active;             /* ds:0011 */
    uint8_t dropping;                 /* ds:0012 */
    uint8_t need_next;                /* ds:0013 */
    uint8_t preview_drawn;            /* ds:0014 */
    uint8_t lock_lr;                  /* ds:0015 */
    uint8_t landed;                   /* ds:0016 */
    uint8_t stored;                   /* ds:12a8: last piece stopped on a wall */
    uint8_t drop_rows;                /* ds:111c */
    uint8_t floor_bonus;              /* ds:111d */
    uint8_t bonus_piece;              /* ds:10f8 */
    uint8_t overflow;                 /* ds:10a0 */
    uint8_t aborted;                  /* ds:1aa0 */
    uint8_t restart;                  /* ds:111a */
    uint8_t piece_index, piece_class; /* ds:1099, ds:10a1: next piece */
    uint16_t next_piece;              /* ds:1114: preview index */
    uint8_t row_full[8], col_full[8]; /* ds:1abc, ds:1ac4 */
    uint8_t anomaly;                  /* original would have hit "Programmer Error" or a wild write */
    uint8_t flipped;                  /* this pass ended in a page flip */

    /* presentation hints, set by the core for the frontend */
    uint32_t effects;    /* WT_FX_* since last cleared by the frontend */
    uint8_t fx_walls[4]; /* walls in the last freeze/thaw effect */
    uint64_t fx_at;      /* clock at which the last blocking effect started */
    /* PC speaker: every change (Hz, 0 = off) with the clock it happened at. The frontend
     * keeps its own read position; spk_n counts all entries ever written. */
    uint64_t spk_at[WT_SPK];
    uint16_t spk_hz[WT_SPK];
    uint32_t spk_n;
} wt_game;

enum {
    WT_FX_MOVE_FAIL = 1u << 0,
    WT_FX_LINES = 1u << 1,  /* floor lines cleared (row_full/col_full) */
    WT_FX_FREEZE = 1u << 2, /* walls frozen (fx_walls) */
    WT_FX_THAW = 1u << 3,   /* walls thawed (fx_walls) */
    WT_FX_SETTLE = 1u << 4, /* stored pieces slid down */
    WT_FX_FLOOR_EMPTY = 1u << 5,
    WT_FX_LEVEL_BONUS = 1u << 6, /* lines_to_level reached: bonus piece */
    WT_FX_LEVEL_UP = 1u << 7,
    WT_FX_LANDED = 1u << 8
};

void wt_session_init(wt_game *g, uint32_t rng_seed, uint64_t clock);
/* The two rand() calls of the (removed) copy protection, at program start. */
void wt_startup_rng(wt_game *g);
void wt_new_game(wt_game *g, const wt_options *o);
void wt_push_key(wt_game *g, uint64_t at, uint16_t key);
int wt_iterate(wt_game *g, uint64_t limit);
void wt_resume(wt_game *g, uint64_t clock);
void wt_end_game(wt_game *g, int restart);
uint32_t wt_ticks(const wt_game *g);
uint16_t wt_rand(wt_game *g);
/* sound_sweep (03fa): count notes from freq, stepping by step (up or down), each len ticks,
 * followed by gap ticks of silence when gap != 0; silence at the end if `silence`. Advances
 * the clock even with sound off, like the original. Used by the frontend for the tunes it
 * plays outside the play loop. */
void wt_sweep(wt_game *g, int freq, int step, int up, int count, uint16_t gap, uint16_t len, int silence);
/* cells of a piece as the original draws it: returns count, fills col/row */
int wt_piece_cells(const wt_piece *p, int *col, int *row, int max);
/* the piece that would be built for (class, index), at column 0 row 0 */
void wt_build_piece(wt_piece *p, int cls, int index);
/* floor x/y of a (column, row < 0) cell */
void wt_wall_to_floor(int *col, int *row);

#endif
