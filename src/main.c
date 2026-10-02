/* Welltris port: SDL2 frontend (native and Emscripten). */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "../core/wt_core.h"
#include "art.h"
#include "audio.h"
#include "font.h"
#include "gfx.h"
#include "hof.h"
#include "store.h"
#include "touch.h"
#include "view.h"

extern const unsigned char font_ttf[];
extern const int font_ttf_len;

#define REPO_URL     "https://github.com/rembish/welltris-revamp"
/* BIOS typematic defaults: 500 ms delay, 10.9 characters per second */
#define REPEAT_DELAY ((uint64_t)WT_PIT_HZ / 2)
#define REPEAT_RATE  ((uint64_t)(WT_PIT_HZ / 10.9))

#ifdef __EMSCRIPTEN__
/* clang-format off */
EM_JS(int, js_coarse_pointer, (void), {
    return (window.matchMedia && matchMedia('(pointer: coarse)').matches) ? 1 : 0;
});
EM_JS(int, js_prompt_name, (char *out, int max), {
    var s = window.prompt('Top score! Please enter your name:', '') || '';
    s = s.replace(/[^ -~]/g, '').slice(0, max);
    stringToUTF8(s, out, max + 1);
    return s.length;
});
/* clang-format on */
#endif

enum scr { S_TITLE, S_SETUP, S_GAME, S_HOF, S_CREDITS };
enum ovl { O_NONE, O_PAUSE, O_CONFIRM, O_OVER, O_TOP, O_QUIT };

/* options.bin: piece set ("LEVEL"), sound off, move mode 2, next piece off, speed, video */
enum { OPT_SET, OPT_SOUND_OFF, OPT_MOVE2, OPT_PREVIEW_OFF, OPT_SPEED, OPT_VIDEO };

static const char *const credits_text[] = {
    "Original Welltris",
    "Design Concept....Alexey Pajitnov",
    "                  Andrei Snegov",
    "",
    "American Version",
    "Product Design, Management",
    "and Programming...Dan Kaufman",
    "",
    "Additional",
    "Programming.......Kevin Seghetti",
    "                  Kus Pranawahadi",
    "                  Greg Marr",
    "",
    "Graphics..........Dan Guerra",
    "                  Jody Sather",
    "                  Matt Carlstrom",
    "",
    "Special Thanks to:",
    "Phil Adam, Anthony Chiang, Steve",
    "Hsieh, Gilman Louie, Ann McCue,",
    "Paul Mogg, Lars Norpchen, Marisa",
    "Ong, Steve Perrin, George Poston,",
    "Joe Scirica, Karen Sherman",
};

static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    int w, h;
    int running;
    double now;

    int scr, ovl;
    int confirm_what; /* 0 quit, 1 abort, 2 restart */
    char toast[96];
    double toast_until;

    /* the core and the clock that maps real time onto it */
    wt_game g;
    wt_game disp; /* what is on screen while the core runs ahead (blocking effects) */
    int busy;
    uint32_t busy_fx;
    double t0;
    uint64_t c0;
    uint64_t over_until; /* game over tunes */

    uint8_t opt[6];
    int cursor; /* setup menu 0..8 */
    double title_since;

    hof_table hof;
    int hof_row, hof_entry;
    char name[HOF_NAME + 1];
    int32_t last_score, last_lines;
    int has_last;

    int touch;
    uint16_t held_key;
    SDL_Keycode held_sym;
    int64_t held_finger;
    uint64_t next_repeat;
} A;

static uint64_t clock_now(void) { return A.c0 + (uint64_t)((A.now - A.t0) * WT_PIT_HZ); }

static void clock_set(uint64_t c)
{
    A.c0 = c;
    A.t0 = A.now;
}

static double seconds(void)
{
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void toast(const char *msg)
{
    snprintf(A.toast, sizeof A.toast, "%s", msg);
    A.toast_until = A.now + 2.0;
}

/* ---- options ---------------------------------------------------------------- */

static const int opt_count[5] = { 3, 2, 2, 2, 5 };

static void load_options(void)
{
    memset(A.opt, 0, sizeof A.opt);
    A.opt[OPT_SPEED] = 0;
    if (store_read("options.bin", A.opt, 6) != 6) memset(A.opt, 0, sizeof A.opt);
    for (int i = 0; i < 5; i++)
        if (A.opt[i] >= opt_count[i]) A.opt[i] = 0;
}

static int save_options(void) { return store_write("options.bin", A.opt, 6) == 6; }

/* ---- game start / end ------------------------------------------------------- */

static void session_start(void)
{
    /* title_wait: srand(time(NULL)), then rand() on every keyboard poll until a key or 200
       ticks; the number of polls depends on the machine, so take it from the time spent */
    uint32_t polls = (uint32_t)((A.now - A.title_since) * 100000.0) + 1;
    wt_session_init(&A.g, (uint32_t)time(NULL) & 0xffffu, clock_now());
    for (uint32_t i = 0; i < polls; i++) wt_rand(&A.g);
    wt_startup_rng(&A.g); /* the copy protection's two picks */
    audio_sync(&A.g, A.g.clock);
}

static void start_game(void)
{
    wt_options o;
    o.piece_set = A.opt[OPT_SET];
    o.sound = !A.opt[OPT_SOUND_OFF];
    o.fixed_keys = A.opt[OPT_MOVE2] != 0;
    o.preview = !A.opt[OPT_PREVIEW_OFF];
    o.level = A.opt[OPT_SPEED];
    wt_resume(&A.g, clock_now());
    clock_set(A.g.clock);
    A.g.q_len = A.g.kbuf_len = 0;
    wt_new_game(&A.g, &o);
    audio_sync(&A.g, A.g.clock);
    A.busy = 0;
    A.held_key = 0;
    touch_release_all();
    A.scr = S_GAME;
    A.ovl = O_NONE;
}

static void enter_setup(void)
{
    A.scr = S_SETUP;
    A.ovl = O_NONE;
    SDL_StopTextInput();
}

static void hof_screen(int after_game)
{
    hof_load(&A.hof);
    A.hof_row = -1;
    A.hof_entry = 0;
    if (after_game) {
        A.last_score = (int32_t)A.g.score;
        A.last_lines = (int32_t)A.g.lines;
        A.has_last = 1;
        if (hof_qualifies(&A.hof, A.last_score)) {
            A.hof_entry = 1;
            A.name[0] = 0;
            SDL_StartTextInput();
#ifdef __EMSCRIPTEN__
            if (A.touch) {
                js_prompt_name(A.name, HOF_NAME);
                A.hof_row = hof_insert(&A.hof, A.name, A.last_score, A.last_lines);
                if (!hof_save(&A.hof)) toast("Could not save the scores");
                A.hof_entry = 0;
                SDL_StopTextInput();
            }
#endif
        }
    }
    A.scr = S_HOF;
    A.ovl = O_NONE;
}

static void game_over(void)
{
    /* main(): the top score message, or the game over tune; then the hall of fame unless the
       game was restarted */
    if (A.g.overflow) {
        A.ovl = O_TOP;
        return;
    }
    if (!A.g.aborted && A.g.sound_on) {
        wt_sweep(&A.g, 400, 10, 0, 0x1e, 1, 5, 1);
        wt_sweep(&A.g, 0x50, 0, 0, 0x28, 1, 1, 1);
    }
    A.over_until = A.g.clock;
    A.ovl = O_OVER;
}

static void after_game(void)
{
    if (A.g.restart)
        enter_setup();
    else
        hof_screen(1);
}

/* ---- running the core ---------------------------------------------------------- */

static void advance_game(void)
{
    uint64_t now = clock_now();
    if (A.ovl == O_OVER) {
        audio_follow(&A.g, now);
        if (now >= A.over_until) after_game();
        return;
    }
    if (A.ovl != O_NONE || A.g.game_over) return;
    /* after a stall (hidden tab, debugger), don't let gravity catch up */
    if (!A.busy && now > A.g.clock + WT_PIT_HZ / 2) {
        clock_set(A.g.clock);
        now = A.g.clock;
    }
    /* typematic repeat of the held key */
    while (A.held_key && A.next_repeat <= now) {
        wt_push_key(&A.g, A.next_repeat, A.held_key);
        A.next_repeat += REPEAT_RATE;
    }
    if (A.busy && A.g.clock <= now) A.busy = 0;
    for (int guard = 0; !A.busy && guard < 1000 && A.g.clock < now; guard++) {
        uint32_t fx0;
        int ev;
        A.disp = A.g;
        A.g.effects = 0;
        fx0 = 0;
        ev = wt_iterate(&A.g, now);
        if (A.g.clock > now &&
            (A.g.effects & (WT_FX_LINES | WT_FX_FREEZE | WT_FX_THAW | WT_FX_LEVEL_BONUS))) {
            A.busy = 1;
            A.busy_fx = A.g.effects & ~fx0;
            memcpy(A.disp.row_full, A.g.row_full, sizeof A.disp.row_full);
            memcpy(A.disp.col_full, A.g.col_full, sizeof A.disp.col_full);
        }
        if (ev == WT_EV_PAUSE) {
            A.ovl = O_PAUSE;
            A.held_key = 0;
        } else if (ev == WT_EV_ABORT || ev == WT_EV_RESTART || ev == WT_EV_QUIT) {
            A.ovl = O_CONFIRM;
            A.confirm_what = ev == WT_EV_QUIT ? 0 : ev == WT_EV_ABORT ? 1 : 2;
            A.held_key = 0;
        }
        if (A.ovl != O_NONE) break;
        if (ev == WT_EV_GAME_OVER) {
            game_over();
            break;
        }
    }
    audio_follow(&A.g, now);
}

static void resume_play(void)
{
    /* the original's pause and confirm loops eat every key; on return the tick counter has
       kept running */
    A.ovl = O_NONE;
    A.g.q_len = A.g.kbuf_len = 0;
    wt_resume(&A.g, clock_now());
    audio_sync(&A.g, clock_now());
}

/* ---- input ------------------------------------------------------------------------ */

/* SDL key -> get_key() word. The game forces NumLock on, so the keypad gives digits. */
static uint16_t map_key(const SDL_Keysym *k)
{
    int shift = (k->mod & KMOD_SHIFT) != 0, alt = (k->mod & KMOD_ALT) != 0;
    if (alt) {
        switch (k->sym) {
        case SDLK_q: return WT_KEY_ALT_Q;
        case SDLK_r: return WT_KEY_ALT_R;
        case SDLK_i: return WT_KEY_ALT_I;
        case SDLK_p: return WT_KEY_ALT_P;
        case SDLK_a: return WT_KEY_ALT_A;
        case SDLK_s: return WT_KEY_ALT_S;
        case SDLK_n: return WT_KEY_ALT_N;
        case SDLK_m: return WT_KEY_ALT_M;
        default: return 0;
        }
    }
    switch (k->sym) {
    case SDLK_UP: return WT_KEY_UP;
    case SDLK_DOWN: return WT_KEY_DOWN;
    case SDLK_LEFT: return WT_KEY_LEFT;
    case SDLK_RIGHT: return WT_KEY_RIGHT;
    case SDLK_SPACE:
    case SDLK_KP_0: return ' ';
    case SDLK_ESCAPE: return WT_KEY_ALT_P; /* convenience: Esc pauses */
    default: break;
    }
    if (k->sym >= SDLK_KP_1 && k->sym <= SDLK_KP_9) return (uint16_t)('1' + (k->sym - SDLK_KP_1));
    if (k->sym >= SDLK_0 && k->sym <= SDLK_9) return (uint16_t)k->sym;
    if (k->sym >= SDLK_a && k->sym <= SDLK_z) return (uint16_t)(k->sym - SDLK_a + (shift ? 'A' : 'a'));
    return 0;
}

static void game_key(uint16_t key, SDL_Keycode sym)
{
    uint64_t now = clock_now();
    if (!key || A.ovl != O_NONE) return;
    wt_push_key(&A.g, now, key);
    A.held_key = key;
    A.held_sym = sym;
    A.next_repeat = now + REPEAT_DELAY;
}

static void confirm_answer(int yes)
{
    if (!yes) {
        resume_play();
        return;
    }
    if (A.confirm_what == 0) {
        A.running = 0;
        return;
    }
    resume_play();
    wt_end_game(&A.g, A.confirm_what == 2);
    game_over();
}

static void setup_move(int d) { A.cursor = (A.cursor + d + 9) % 9; }

static void setup_value(int d)
{
    if (A.cursor < 5)
        A.opt[A.cursor] = (uint8_t)((A.opt[A.cursor] + opt_count[A.cursor] + d) % opt_count[A.cursor]);
}

static void setup_activate(void)
{
    switch (A.cursor) {
    case 5: hof_screen(0); break;
    case 6: A.scr = S_CREDITS; break;
    case 7: toast(save_options() ? "Options saved" : "Could not save the options"); break;
    case 8:
#ifdef __EMSCRIPTEN__
        if (SDL_OpenURL(REPO_URL) != 0) toast(REPO_URL);
#else
        A.ovl = O_QUIT;
#endif
        break;
    default: setup_move(1); break;
    }
}

static void setup_key(const SDL_Keysym *k)
{
    if (A.ovl == O_QUIT) {
        if (k->sym == SDLK_y) A.running = 0;
        if (k->sym == SDLK_n || k->sym == SDLK_ESCAPE) A.ovl = O_NONE;
        return;
    }
    switch (k->sym) {
    case SDLK_UP:
    case SDLK_KP_8:
    case SDLK_8:
    case SDLK_i: setup_move(-1); break;
    case SDLK_DOWN:
    case SDLK_KP_2:
    case SDLK_2:
    case SDLK_m: setup_move(1); break;
    case SDLK_LEFT:
    case SDLK_KP_4:
    case SDLK_4:
    case SDLK_j: setup_value(-1); break;
    case SDLK_RIGHT:
    case SDLK_KP_6:
    case SDLK_6:
    case SDLK_l: setup_value(1); break;
    case SDLK_TAB:
        if (A.cursor < 4)
            A.cursor += 5;
        else if (A.cursor > 4)
            A.cursor -= 5;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: setup_activate(); break;
    case SDLK_SPACE: start_game(); break;
    case SDLK_ESCAPE:
#ifndef __EMSCRIPTEN__
        A.ovl = O_QUIT;
#endif
        break;
    case SDLK_q:
        if (k->mod & KMOD_ALT) A.running = 0;
        break;
    default: break;
    }
}

static void hof_key(const SDL_Keysym *k)
{
    if (A.hof_entry) {
        size_t n = strlen(A.name);
        if (k->sym == SDLK_BACKSPACE && n)
            A.name[n - 1] = 0;
        else if (k->sym == SDLK_RETURN || k->sym == SDLK_KP_ENTER) {
            A.hof_row = hof_insert(&A.hof, A.name, A.last_score, A.last_lines);
            if (!hof_save(&A.hof)) toast("Could not save the scores");
            A.hof_entry = 0;
            SDL_StopTextInput();
        }
        return;
    }
    enter_setup();
}

static void text_input(const char *t)
{
    if (!(A.scr == S_HOF && A.hof_entry)) return;
    for (; *t; t++) {
        size_t n = strlen(A.name);
        if (n < HOF_NAME && *t >= 32 && *t < 127) {
            A.name[n] = *t;
            A.name[n + 1] = 0;
        }
    }
}

static void key_down(const SDL_Keysym *k, int repeat)
{
    audio_resume();
    if (k->sym == SDLK_F11 || (k->sym == SDLK_RETURN && (k->mod & KMOD_ALT))) {
        Uint32 fs = SDL_GetWindowFlags(A.win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen(A.win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    switch (A.scr) {
    case S_TITLE:
        session_start();
        enter_setup();
        break;
    case S_SETUP: setup_key(k); break;
    case S_CREDITS: enter_setup(); break;
    case S_HOF: hof_key(k); break;
    case S_GAME:
        if (repeat) break; /* the BIOS repeat is emulated on the game clock */
        if (A.ovl == O_PAUSE) {
            if (map_key(k) == WT_KEY_ALT_P) resume_play();
            if (map_key(k) == WT_KEY_ALT_Q) {
                A.ovl = O_CONFIRM;
                A.confirm_what = 0;
            }
        } else if (A.ovl == O_CONFIRM) {
            if (k->sym == SDLK_y) confirm_answer(1);
            if (k->sym == SDLK_n || k->sym == SDLK_ESCAPE) confirm_answer(0);
        } else if (A.ovl == O_TOP) {
            A.ovl = O_NONE;
            after_game();
        } else {
            game_key(map_key(k), k->sym);
        }
        break;
    default: break;
    }
}

/* ---- touch -------------------------------------------------------------------------- */

static void setup_point(float x, float y);

static void set_touch(int on)
{
    A.touch = on;
    view_set_touch(on);
}

static void finger_down(float nx, float ny, int64_t finger)
{
    if (!A.touch) set_touch(1);
    audio_resume();
    uint16_t key = touch_press(nx * (float)A.w, ny * (float)A.h, finger);
    if (A.scr != S_GAME) {
        touch_release(finger);
        if (A.scr == S_TITLE) {
            session_start();
            enter_setup();
        } else if (A.scr == S_SETUP)
            setup_point(nx * (float)A.w, ny * (float)A.h);
        else if (!(A.scr == S_HOF && A.hof_entry))
            enter_setup();
        return;
    }
    if (A.ovl == O_PAUSE) {
        resume_play();
        return;
    }
    if (A.ovl == O_TOP) {
        A.ovl = O_NONE;
        after_game();
        return;
    }
    if (A.ovl == O_CONFIRM) {
        confirm_answer(key == 'y');
        return;
    }
    if (!key) return;
    game_key(key, SDLK_UNKNOWN);
    A.held_finger = finger;
}

static void finger_up(int64_t finger, SDL_TouchID device)
{
    touch_release(finger);
    if (finger == A.held_finger) A.held_key = 0;
    if (SDL_GetNumTouchFingers(device) == 0) {
        touch_release_all();
        A.held_key = 0;
    }
}

/* ---- rendering ------------------------------------------------------------------------- */

/* a 640x350 original screen, shown at 4:3 and fitted into the window */
typedef struct {
    float x, y, k; /* screen = (x + ox * k, y + oy * k * 480/350) */
} screen43;

static screen43 fit43(void)
{
    screen43 s;
    float w = (float)A.w, h = (float)A.h, sw = w, sh = w * 0.75f;
    if (sh > h) {
        sh = h;
        sw = h / 0.75f;
    }
    s.k = sw / 640.f;
    s.x = (w - sw) / 2;
    s.y = (h - sh) / 2;
    return s;
}

static box map43(const screen43 *s, float x, float y, float w, float h)
{
    const float a = 480.f / 350.f;
    return (box){ s->x + x * s->k, s->y + y * s->k * a, w * s->k, h * s->k * a };
}

static void glow_box(box b, rgba c, float t, int strong)
{
    float p = 0.55f + 0.45f * sinf(t * 5.f);
    gfx_rect(b.x, b.y, b.w, b.h, rgba_alpha(c, strong ? 0.28f * p + 0.1f : 0.16f));
    gfx_rect_outline(b.x - 1, b.y - 1, b.w + 2, b.h + 2, strong ? 2.5f : 1.5f,
                     rgba_alpha(c, strong ? p : 0.7f));
}

/* setup value boxes and cursor positions, from the tables at ds:0476 and ds:03bc */
static const float val_x[5][5] = {
    { 128, 152, 176 }, { 128, 168 }, { 168, 192 }, { 80, 128 }, { 88, 128, 168, 208, 248 }
};
static const float val_y[5] = { 58, 88, 118, 171, 244 };
static const float val_w[5] = { 16, 32, 16, 32, 24 };
static const float val_wk[2][5] = { { 16, 32, 16, 32, 24 }, { 16, 40, 16, 40, 24 } };
static const float item_box[9][4] = { { 40, 56, 160, 20 },  { 40, 86, 160, 20 },  { 40, 116, 170, 20 },
                                      { 40, 146, 170, 44 }, { 80, 238, 200, 26 }, { 228, 56, 92, 20 },
                                      { 246, 86, 50, 20 },  { 230, 116, 84, 36 }, { 246, 168, 50, 20 } };

/* menu hit areas, recorded while drawing (mouse and touch) */
static box hit_item[9], hit_val[5][5], hit_start;

static int inside(box b, float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

static void draw_setup_art(float t)
{
    screen43 s = fit43();
    art_draw(ART_SETUP, s.x, s.y, 640 * s.k, 480 * s.k, 1);
    for (int i = 0; i < 5; i++) {
        int v = A.opt[i];
        float w = i == 1 || i == 3 ? val_wk[v][i] : val_w[i];
        glow_box(map43(&s, val_x[i][v] - 1, val_y[i] - 1, w + 2, (i == 4 ? 12 : 15) + 2),
                 rgb_hex(0x6ff7ff, 1), t, 0);
        for (int k = 0; k < opt_count[i]; k++)
            hit_val[i][k] = map43(&s, val_x[i][k] - 2, val_y[i] - 3,
                                  (i == 1 || i == 3 ? val_wk[k][i] : val_w[i]) + 4, (i == 4 ? 12 : 15) + 6);
    }
    for (int i = 0; i < 9; i++)
        hit_item[i] = map43(&s, item_box[i][0], item_box[i][1], item_box[i][2], item_box[i][3]);
    hit_start = map43(&s, 354, 102, 110, 34);
    const float *b = item_box[A.cursor];
    glow_box(map43(&s, b[0], b[1], b[2], b[3]), rgb_hex(0xffd166, 1), t, 1);
}

static void draw_setup_modern(float t)
{
    static const char *labels[9] = { "LEVEL",      "SOUND",    "MOVE MODE", "NEXT PIECE",
                                     "SPEED",      "HI SCORE", "INFO",      "SAVE OPTIONS",
#ifdef __EMSCRIPTEN__
                                     "SOURCE CODE"
#else
                                     "QUIT"
#endif
    };
    static const char *vals[5][5] = { { "1", "2", "3" },
                                      { "ON", "OFF" },
                                      { "1", "2" },
                                      { "ON", "OFF" },
                                      { "1.0", "2.0", "3.0", "4.0", "5.0" } };
    float W = (float)A.w, H = (float)A.h, s = fminf(W / 30.f, H / 22.f);
    box p = { (W - s * 26) / 2, (H - s * 17.5f) / 2, s * 26, s * 17.5f };
    view_scene((box){ 0, 0, W, H }, A.opt[OPT_SPEED], t);
    gfx_rect(0, 0, W, H, rgb_hex(0x05070d, 0.55f));
    gfx_round_rect(p.x, p.y, p.w, p.h, s * 0.5f, rgb_hex(0x0c1220, 0.94f));
    gfx_rect_outline(p.x, p.y, p.w, p.h, 2, rgb_hex(0x2ab7c0, 1));
    font_draw(p.x + p.w / 2, p.y + s * 0.6f, s * 1.3f, rgb_hex(0xffd166, 1), ALIGN_CENTER, "SET UP GAME");
    font_draw(p.x + p.w / 2, p.y + s * 2.2f, s * 0.55f, rgb_hex(0x7f8aa3, 1), ALIGN_CENTER,
              "UP/DOWN, TAB: select field     LEFT/RIGHT: select value     SPACE: start game");
    for (int i = 0; i < 9; i++) {
        int col = i < 5 ? 0 : 1, row = i < 5 ? i : i - 5;
        float x = p.x + s * (col ? 17.5f : 1.2f), y = p.y + s * (3.6f + (float)row * 2.4f);
        box hb = { x - s * 0.4f, y - s * 0.3f, col ? s * 7.6f : s * 15.6f, s * 1.6f };
        hit_item[i] = hb;
        if (i == A.cursor) glow_box(hb, rgb_hex(0xffd166, 1), t, 1);
        font_draw(x, y, s * 0.9f, rgb_hex(0xe9eef7, 1), ALIGN_LEFT, labels[i]);
        if (i < 5)
            for (int v = 0; v < opt_count[i]; v++) {
                float vx = x + s * (6.2f + (float)v * (i == 4 ? 1.9f : 2.3f));
                int on = A.opt[i] == v;
                hit_val[i][v] =
                    (box){ vx - s * 0.25f, y - s * 0.15f, s * (i == 4 ? 1.75f : 2.0f), s * 1.25f };
                if (on)
                    glow_box((box){ vx - s * 0.25f, y - s * 0.15f, s * (i == 4 ? 1.75f : 2.0f), s * 1.25f },
                             rgb_hex(0x6ff7ff, 1), t, 0);
                font_draw(vx + s * (i == 4 ? 0.62f : 0.75f), y, s * 0.9f,
                          on ? rgb_hex(0x6ff7ff, 1) : rgb_hex(0x5d6680, 1), ALIGN_CENTER, vals[i][v]);
            }
    }
    box sb = { p.x + s * 17.1f, p.y + p.h - s * 3.2f, s * 7.8f, s * 2.2f };
    hit_start = sb;
    gfx_round_rect(sb.x, sb.y, sb.w, sb.h, s * 0.3f, rgb_hex(0x7a1f1f, 1));
    font_draw(sb.x + sb.w / 2, sb.y + s * 0.25f, s * 0.9f, rgb_hex(0xffffff, 1), ALIGN_CENTER, "START GAME");
    font_draw(sb.x + sb.w / 2, sb.y + s * 1.25f, s * 0.55f, rgb_hex(0xffd2c4, 1), ALIGN_CENTER,
              "(SPACE BAR)");
}

static void draw_title(float t)
{
    float W = (float)A.w, H = (float)A.h;
    if (art_have(ART_TITLE)) {
        screen43 s = fit43();
        art_draw(ART_TITLE, s.x, s.y, 640 * s.k, 480 * s.k, 1);
    } else {
        float s = fminf(W, H * 1.4f);
        view_scene((box){ 0, 0, W, H }, 0, t);
        font_draw(W / 2, H * 0.12f, s * 0.04f, rgb_hex(0x1a1414, 0.85f), ALIGN_CENTER,
                  "Spectrum HoloByte presents...");
        font_draw(W / 2 + s * 0.006f, H * 0.2f + s * 0.006f, s * 0.15f, rgb_hex(0x2a0a0a, 0.8f), ALIGN_CENTER,
                  "WELLTRIS");
        font_draw(W / 2, H * 0.2f, s * 0.15f, rgb_hex(0xb3261e, 1), ALIGN_CENTER, "WELLTRIS");
    }
    if (!art_have(ART_TITLE))
        font_draw(W / 2, H * 0.93f, fminf(W, H) * 0.03f, rgb_hex(0xffffff, 0.55f + 0.4f * sinf(t * 3)),
                  ALIGN_CENTER, A.touch ? "tap to continue" : "press any key");
}

static void draw_credits(float t)
{
    float W = (float)A.w, H = (float)A.h;
    box b;
    if (art_have(ART_CREDITS)) {
        screen43 s = fit43();
        b = map43(&s, 136, 50, 368, 250);
        gfx_rect(0, 0, W, H, rgb_hex(0x000000, 1));
        art_draw(ART_CREDITS, b.x, b.y, b.w, b.h, 1);
    } else {
        float s = fminf(W, H) * 0.9f;
        b = (box){ (W - s) / 2, (H - s * 0.7f) / 2, s, s * 0.7f };
        view_scene((box){ 0, 0, W, H }, 2, t);
        gfx_round_rect(b.x, b.y, b.w, b.h, s * 0.02f, rgb_hex(0x8a1212, 0.95f));
        gfx_rect_outline(b.x, b.y, b.w, b.h, 3, rgb_hex(0xd9a441, 1));
    }
    int n = (int)(sizeof credits_text / sizeof credits_text[0]);
    float fs = b.h / ((float)n + 4.5f);
    font_draw(b.x + b.w / 2, b.y + fs * 0.9f, fs * 1.2f, rgb_hex(0xffd166, 1), ALIGN_CENTER, "WELLTRIS");
    for (int i = 0; i < n; i++)
        font_draw(b.x + b.w * 0.09f, b.y + fs * (2.8f + (float)i), fs * 0.8f, rgb_hex(0xf2f2f2, 1),
                  ALIGN_LEFT, credits_text[i]);
    font_draw(W / 2, b.y + b.h + fs * 0.5f, fs * 0.8f, rgb_hex(0xffffff, 0.6f), ALIGN_CENTER,
              "< press any key >");
}

static void draw_hof(float t)
{
    float W = (float)A.w, H = (float)A.h;
    int art = art_have(ART_HISCORE1) && art_have(ART_HISCORE2);
    box names, lines, last;
    float fs;
    if (art) {
        screen43 s = fit43();
        gfx_rect(0, 0, W, H, rgb_hex(0x000000, 1));
        box l = map43(&s, 0, 0, 320, 350), r = map43(&s, 320, 0, 320, 350);
        art_draw(ART_HISCORE1, l.x, l.y, l.w, l.h, 1);
        art_draw(ART_HISCORE2, r.x, r.y, r.w, r.h, 1);
        names = map43(&s, 98, 13, 202, 112);
        lines = map43(&s, 320, 13, 76, 112);
        last = map43(&s, 76, 140, 224, 20);
        fs = names.h / 10.f * 0.85f;
    } else {
        float s = fminf(W / 18.f, H / 16.f);
        view_scene((box){ 0, 0, W, H }, 4, t);
        gfx_rect(0, 0, W, H, rgb_hex(0x05070d, 0.5f));
        box p = { (W - s * 15) / 2, (H - s * 14) / 2, s * 15, s * 14 };
        gfx_round_rect(p.x, p.y, p.w, p.h, s * 0.4f, rgb_hex(0x0c1220, 0.94f));
        gfx_rect_outline(p.x, p.y, p.w, p.h, 2, rgb_hex(0xd9a441, 1));
        font_draw(p.x + s * 1.6f, p.y + s * 0.5f, s * 0.8f, rgb_hex(0xffd166, 1), ALIGN_LEFT, "NAME");
        font_draw(p.x + s * 10.2f, p.y + s * 0.5f, s * 0.8f, rgb_hex(0xffd166, 1), ALIGN_RIGHT, "SCORE");
        font_draw(p.x + p.w - s * 0.6f, p.y + s * 0.5f, s * 0.8f, rgb_hex(0xffd166, 1), ALIGN_RIGHT, "LINES");
        names = (box){ p.x + s * 0.4f, p.y + s * 1.8f, s * 9.8f, s * 9.5f };
        lines = (box){ p.x + s * 10.2f, p.y + s * 1.8f, s * 4.2f, s * 9.5f };
        last = (box){ p.x + s * 0.4f, p.y + s * 11.9f, s * 9.8f, s * 1.2f };
        font_draw(p.x + s * 1.6f, p.y + s * 11.3f, s * 0.6f, rgb_hex(0xffd166, 1), ALIGN_LEFT, "LAST GAME");
        fs = s * 0.75f;
    }
    float rowh = names.h / 10.f;
    for (int i = 0; i < HOF_MAX; i++) {
        float y = names.y + rowh * (float)i;
        rgba c = i == A.hof_row ? rgb_hex(0x6ff7ff, 1) : rgb_hex(0xf2f2f2, 1);
        if (!art) font_drawf(names.x + fs * 1.6f, y, fs, rgb_hex(0xffd166, 1), ALIGN_RIGHT, "%d", i + 1);
        if (i < A.hof.count) {
            font_draw(names.x + (art ? fs * 0.3f : fs * 2.2f), y, fs, c, ALIGN_LEFT, A.hof.e[i].name);
            font_drawf(names.x + names.w, y, fs, c, ALIGN_RIGHT, "%ld", (long)A.hof.e[i].score);
            font_drawf(lines.x + lines.w, y, fs, c, ALIGN_RIGHT, "%ld", (long)A.hof.e[i].lines);
        }
    }
    if (A.has_last)
        font_drawf(last.x + last.w, last.y, fs, rgb_hex(0xf2f2f2, 1), ALIGN_RIGHT, "%ld  /  %ld lines",
                   (long)A.last_score, (long)A.last_lines);
    if (A.hof_entry) {
        float s = fminf(W, H) * 0.045f;
        box d = { W / 2 - s * 8, H / 2 - s * 2, s * 16, s * 4 };
        gfx_round_rect(d.x, d.y, d.w, d.h, s * 0.3f, rgb_hex(0x8a1212, 0.97f));
        gfx_rect_outline(d.x, d.y, d.w, d.h, 3, rgb_hex(0xd9a441, 1));
        font_draw(W / 2, d.y + s * 0.4f, s * 0.8f, rgb_hex(0xffd166, 1), ALIGN_CENTER,
                  "Please Enter Your Name:");
        font_drawf(W / 2, d.y + s * 1.9f, s, rgb_hex(0xffffff, 1), ALIGN_CENTER, "%s%s", A.name,
                   fmodf(t, 1.f) < 0.5f ? "_" : " ");
    } else {
        font_draw(W / 2, H * 0.95f, fminf(W, H) * 0.028f, rgb_hex(0xffffff, 0.6f), ALIGN_CENTER,
                  "< press any key >");
    }
}

static void dialog(const char *line1, const char *line2, const char *line3)
{
    float W = (float)A.w, H = (float)A.h, s = fminf(W, H) * 0.04f;
    box d = { W / 2 - s * 8, H / 2 - s * 2.6f, s * 16, s * 5.2f };
    if (art_have(ART_DIALOG))
        art_draw(ART_DIALOG, d.x, d.y, d.w, d.h, 1);
    else {
        gfx_round_rect(d.x, d.y, d.w, d.h, s * 0.3f, rgb_hex(0xa8acb4, 0.97f));
        gfx_rect_outline(d.x, d.y, d.w, d.h, 3, rgb_hex(0x50555e, 1));
    }
    rgba c = rgb_hex(0x10131a, 1);
    font_draw(W / 2, d.y + s * 0.9f, s * 0.85f, c, ALIGN_CENTER, line1);
    if (line2) font_draw(W / 2, d.y + s * 2.1f, s * 0.85f, c, ALIGN_CENTER, line2);
    if (line3) font_draw(W / 2, d.y + s * 3.4f, s * 0.7f, rgb_hex(0x8a1212, 1), ALIGN_CENTER, line3);
}

static void draw_game(float t)
{
    view_layout_t L;
    view_fx fx;
    const wt_game *g = &A.g;
    uint64_t now = clock_now();
    memset(&fx, 0, sizeof fx);
    fx.time = t;
    view_layout(A.w, A.h, &L);
    if (A.busy) {
        uint64_t ph = now > A.g.fx_at ? now - A.g.fx_at : 0;
        int flashing = ph < 0x1d * (uint64_t)(WT_FRAME + WT_TICK);
        int on = (int)((ph / WT_FRAME) & 2) != 0;
        if (A.busy_fx & WT_FX_LINES) {
            g = &A.disp;
            fx.lines_blink = flashing ? (on ? 1 : -1) : -1;
        } else if (A.busy_fx & (WT_FX_FREEZE | WT_FX_THAW)) {
            fx.walls_blink = flashing ? (on ? 1 : -1) : 0;
        } else if (A.busy_fx & WT_FX_LEVEL_BONUS) {
            fx.message = "BONUS PIECE";
        }
    }
    if (A.ovl == O_OVER || g->game_over) { fx.message = "GAME OVER"; }
    view_game(g, &L, &fx);
    if (A.ovl == O_PAUSE) dialog("-- Game Paused --", "press: alt-P to resume", "alt-Q to quit");
    if (A.ovl == O_CONFIRM) {
        static const char *what[3] = { "to quit?", "to abort game?", "to restart game?" };
        dialog("Are you sure you want", what[A.confirm_what], "[ y or n ]");
    }
    if (A.ovl == O_TOP) dialog("Congratulations!!", "Top Score Attained", "[press any key]");
    touch_layout(&L, A.ovl == O_CONFIRM                 ? TOUCH_CONFIRM
                     : A.ovl == O_NONE && !g->game_over ? TOUCH_GAME
                                                        : TOUCH_NONE);
    touch_draw(t);
}

static void render(void)
{
    float t = (float)A.now;
    SDL_GetRendererOutputSize(A.ren, &A.w, &A.h);
    SDL_SetRenderDrawColor(A.ren, 0, 0, 0, 255);
    SDL_RenderClear(A.ren);
    view_background(A.w, A.h);
    switch (A.scr) {
    case S_TITLE: draw_title(t); break;
    case S_SETUP:
        if (art_have(ART_SETUP)) {
            gfx_rect(0, 0, (float)A.w, (float)A.h, rgb_hex(0x000000, 1));
            draw_setup_art(t);
        } else {
            draw_setup_modern(t);
        }
        if (A.ovl == O_QUIT) dialog("Are you sure you want", "to quit?", "[ y or n ]");
        break;
    case S_CREDITS: draw_credits(t); break;
    case S_HOF: draw_hof(t); break;
    case S_GAME: draw_game(t); break;
    default: break;
    }
    if (A.toast_until > A.now) {
        float s = fminf((float)A.w, (float)A.h) * 0.035f, tw = font_width(s, A.toast) + s * 2;
        gfx_round_rect(((float)A.w - tw) / 2, (float)A.h - s * 3, tw, s * 1.8f, s * 0.4f,
                       rgb_hex(0x2ab7c0, 0.95f));
        font_draw((float)A.w / 2, (float)A.h - s * 2.6f, s, rgb_hex(0x04101a, 1), ALIGN_CENTER, A.toast);
    }
    gfx_flush();
    SDL_RenderPresent(A.ren);
}

/* ---- main loop -------------------------------------------------------------------------- */

/* a click or tap on the setup screen */
static void setup_point(float x, float y)
{
    if (A.ovl == O_QUIT) {
        A.ovl = O_NONE;
        return;
    }
    if (inside(hit_start, x, y)) {
        start_game();
        return;
    }
    for (int i = 0; i < 5; i++)
        for (int k = 0; k < opt_count[i]; k++)
            if (inside(hit_val[i][k], x, y)) {
                A.cursor = i;
                A.opt[i] = (uint8_t)k;
                return;
            }
    for (int i = 0; i < 9; i++)
        if (inside(hit_item[i], x, y)) {
            if (A.cursor == i || i >= 5) {
                A.cursor = i;
                if (i < 5)
                    setup_value(1);
                else
                    setup_activate();
            }
            A.cursor = i;
            return;
        }
}

static void mouse_click(int x, int y)
{
    int ww, wh;
    float fx, fy;
    SDL_GetWindowSize(A.win, &ww, &wh);
    fx = (float)x * (float)A.w / (float)ww;
    fy = (float)y * (float)A.h / (float)wh;
    if (A.scr == S_SETUP) {
        setup_point(fx, fy);
        return;
    }
    if (A.scr == S_TITLE) {
        session_start();
        enter_setup();
    } else if (A.scr == S_CREDITS || (A.scr == S_HOF && !A.hof_entry))
        enter_setup();
}

static void frame(void)
{
    SDL_Event e;
    A.now = seconds();
#ifdef __EMSCRIPTEN__
    {
        double cw, ch;
        int ww, wh;
        emscripten_get_element_css_size("#canvas", &cw, &ch);
        SDL_GetWindowSize(A.win, &ww, &wh);
        if ((int)cw != ww || (int)ch != wh) SDL_SetWindowSize(A.win, (int)cw, (int)ch);
    }
#endif
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: A.running = 0; break;
        case SDL_KEYDOWN: key_down(&e.key.keysym, e.key.repeat); break;
        case SDL_KEYUP:
            if (e.key.keysym.sym == A.held_sym && A.held_sym != SDLK_UNKNOWN) A.held_key = 0;
            break;
        case SDL_TEXTINPUT: text_input(e.text.text); break;
        case SDL_FINGERDOWN: finger_down(e.tfinger.x, e.tfinger.y, (int64_t)e.tfinger.fingerId); break;
        case SDL_FINGERUP: finger_up((int64_t)e.tfinger.fingerId, e.tfinger.touchId); break;
        case SDL_MOUSEBUTTONDOWN:
            audio_resume();
            if (e.button.which != SDL_TOUCH_MOUSEID) mouse_click(e.button.x, e.button.y);
            break;
        default: break;
        }
    }
    if (A.scr == S_GAME) advance_game();
    /* title_wait(200): the title stays for 200 ticks or until a key */
    if (A.scr == S_TITLE && A.now - A.title_since > 200.0 * WT_TICK / WT_PIT_HZ + 1.5) {
        session_start();
        enter_setup();
    }
    render();
#ifdef __EMSCRIPTEN__
    if (!A.running) emscripten_cancel_main_loop();
#endif
}

/* --shot out.bmp [--screen title|setup|game|hof|credits] [--seed S] [--secs N] [--keys t:key,...]
   [--opts set,speed]: render headless, save a screenshot and exit */
static int shot_mode(int argc, char **argv)
{
    const char *out = NULL, *screen = "game", *keys = "";
    double secs = 20;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--shot"))
            out = argv[++i];
        else if (!strcmp(argv[i], "--screen"))
            screen = argv[++i];
        else if (!strcmp(argv[i], "--seed"))
            seed = (unsigned)atol(argv[++i]);
        else if (!strcmp(argv[i], "--secs"))
            secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "--keys"))
            keys = argv[++i];
        else if (!strcmp(argv[i], "--opts")) {
            int a = 0, b = 0;
            if (sscanf(argv[++i], "%d,%d", &a, &b) == 2) {
                A.opt[OPT_SET] = (uint8_t)a;
                A.opt[OPT_SPEED] = (uint8_t)b;
            }
        }
    }
    if (!out) return 0;
    A.now = 1.0;
    A.t0 = 1.0;
    if (!strcmp(screen, "game")) {
        wt_session_init(&A.g, seed, 0);
        start_game();
        /* play: keys "seconds:hexkey,..." at their times, a bot-free random walk otherwise */
        const char *k = keys;
        for (long f = 0; f < (long)(secs * 60) && A.scr == S_GAME; f++) {
            double t = (double)f / 60;
            A.now = 1.0 + t;
            while (*k) {
                double kt;
                unsigned kv;
                int n;
                if (sscanf(k, "%lf:%x%n", &kt, &kv, &n) != 2 || kt > t) break;
                wt_push_key(&A.g, clock_now(), (uint16_t)kv);
                k += n;
                if (*k == ',') k++;
            }
            advance_game();
        }
        A.scr = S_GAME;
    } else if (!strcmp(screen, "setup"))
        A.scr = S_SETUP;
    else if (!strcmp(screen, "hof")) {
        hof_screen(0);
        A.last_score = 1234;
        A.last_lines = 12;
        A.has_last = 1;
    } else if (!strcmp(screen, "credits"))
        A.scr = S_CREDITS;
    else
        A.scr = S_TITLE;
    render();
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, A.w, A.h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(A.ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, out);
    printf("score=%lu lines=%lu level=%d over=%d\n", (unsigned long)A.g.score, (unsigned long)A.g.lines,
           A.g.level, A.g.game_over);
    return 1;
}

int main(int argc, char **argv)
{
    int w = 1280, h = 800;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--size")) sscanf(argv[i + 1], "%dx%d", &w, &h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    A.win = SDL_CreateWindow("Welltris", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    A.ren =
        A.win ? SDL_CreateRenderer(A.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (A.win && !A.ren) A.ren = SDL_CreateRenderer(A.win, -1, 0);
    if (!A.win || !A.ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(A.ren, SDL_BLENDMODE_BLEND);
    gfx_init(A.ren);
    if (!font_init(A.ren, font_ttf, font_ttf_len)) {
        fprintf(stderr, "font initialisation failed\n");
        return 1;
    }
    art_init(A.ren);
    audio_init();
    store_init();
    load_options();
    SDL_StopTextInput();
#ifdef __EMSCRIPTEN__
    set_touch(js_coarse_pointer());
#endif
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--touch")) set_touch(1);
    if (shot_mode(argc, argv)) return 0;
    A.now = seconds();
    A.t0 = A.now;
    A.title_since = A.now;
    A.scr = S_TITLE;
    A.running = 1;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (A.running) frame();
    SDL_DestroyRenderer(A.ren);
    SDL_DestroyWindow(A.win);
    SDL_Quit();
#endif
    return 0;
}
