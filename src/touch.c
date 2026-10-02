#include "touch.h"
#include "font.h"
#include <math.h>
#include <string.h>

#define MAXB 16

enum kind { K_ARROW, K_TEXT, K_SMALL };

typedef struct {
    box r;
    uint16_t key;
    int kind;
    float dir_x, dir_y;
    const char *label;
    int64_t finger;
    int held;
} tbtn;

static tbtn btn[MAXB];
static int nb;

static void add(box r, uint16_t key, int kind, float a, float b, const char *label)
{
    if (nb == MAXB) return;
    btn[nb++] = (tbtn){ r, key, kind, a, b, label, 0, 0 };
}

static box inset(box b, float k) { return (box){ b.x + b.w * k, b.y + b.h * k, b.w * (1 - 2 * k), b.h * (1 - 2 * k) }; }

/* the arrow keys: the game turns them into moves along whichever wall the piece is on */
static void dpad(box a)
{
    float s = fminf(a.w, a.h) / 3.f, x0 = a.x + (a.w - 3 * s) / 2, y0 = a.y + (a.h - 3 * s) / 2;
    add(inset((box){ x0 + s, y0, s, s }, 0.04f), WT_KEY_UP, K_ARROW, 0, 1, NULL);
    add(inset((box){ x0, y0 + s, s, s }, 0.04f), WT_KEY_LEFT, K_ARROW, -1, 0, NULL);
    add(inset((box){ x0 + 2 * s, y0 + s, s, s }, 0.04f), WT_KEY_RIGHT, K_ARROW, 1, 0, NULL);
    add(inset((box){ x0 + s, y0 + 2 * s, s, s }, 0.04f), WT_KEY_DOWN, K_ARROW, 0, -1, NULL);
}

static void actions(box a)
{
    float h = fminf(a.h * 0.4f, a.w * 0.9f);
    add(inset((box){ a.x, a.y + a.h * 0.1f, a.w, h }, 0.04f), '5', K_TEXT, 0, 0, "ROTATE");
    add(inset((box){ a.x, a.y + a.h * 0.1f + h * 1.1f, a.w, h * 0.8f }, 0.04f), ' ', K_TEXT, 0, 0, "DROP");
}

static void small_row(box a)
{
    float w = fminf(a.h * 2.6f, a.w / 3.3f), h = fminf(a.h, w * 0.5f);
    add((box){ a.x, a.y, w, h }, WT_KEY_ALT_P, K_SMALL, 0, 0, "PAUSE");
    add((box){ a.x + w * 1.15f, a.y, w, h }, WT_KEY_ALT_S, K_SMALL, 0, 0, "SOUND");
    add((box){ a.x + w * 2.3f, a.y, w, h }, WT_KEY_ALT_N, K_SMALL, 0, 0, "NEXT");
}

void touch_layout(const view_layout_t *L, int screen)
{
    /* finger ids are arbitrary (negative on iOS in a 32-bit build): carry a flag, not a sentinel */
    int64_t keep_finger[MAXB];
    int keep_held[MAXB], keep_n = nb;
    for (int i = 0; i < nb; i++) {
        keep_held[i] = btn[i].held;
        keep_finger[i] = btn[i].finger;
    }
    nb = 0;
    if (screen == TOUCH_NONE || L->left.w <= 0) return;
    if (screen == TOUCH_CONFIRM) {
        add(inset(L->left, 0.15f), 'n', K_TEXT, 0, 0, "NO");
        add(inset(L->right, 0.15f), 'y', K_TEXT, 0, 0, "YES");
    } else {
        float row = L->portrait ? L->left.h * 0.14f : L->left.w * 0.2f;
        box top = L->portrait ? (box){ L->left.x, L->left.y, L->right.x + L->right.w - L->left.x, row }
                              : (box){ L->left.x, L->left.y - row * 1.3f, L->left.w * 2.5f, row };
        if (L->portrait) small_row(top);
        else small_row((box){ L->left.x, 8, L->left.w * 3.4f, row });
        float off = L->portrait ? row * 1.3f : 0;
        dpad((box){ L->left.x, L->left.y + off, L->left.w, L->left.h - off });
        actions((box){ L->right.x, L->right.y + off, L->right.w, L->right.h - off });
    }
    for (int i = 0; i < nb && i < keep_n; i++)
        if (keep_held[i]) {
            btn[i].held = 1;
            btn[i].finger = keep_finger[i];
        }
}

void touch_draw(float t)
{
    (void)t;
    for (int i = 0; i < nb; i++) {
        tbtn *b = &btn[i];
        float rad = fminf(b->r.w, b->r.h) * 0.22f, s = fminf(b->r.w, b->r.h);
        float cx = b->r.x + b->r.w / 2, cy = b->r.y + b->r.h / 2;
        rgba face = b->held ? rgb_hex(0x2ab7c0, 0.5f) : rgb_hex(0x1a2234, 0.75f);
        rgba ink = b->held ? rgb_hex(0xffffff, 1) : rgb_hex(0xd5dcea, 0.9f);
        gfx_round_rect(b->r.x, b->r.y, b->r.w, b->r.h, rad, face);
        if (b->kind == K_ARROW) {
            float dx = b->dir_x, dy = -b->dir_y, k = s * 0.24f, bw = k * 0.8f;
            gfx_tri(cx + dx * k, cy + dy * k, cx - dx * k * 0.6f - dy * bw, cy - dy * k * 0.6f + dx * bw,
                    cx - dx * k * 0.6f + dy * bw, cy - dy * k * 0.6f - dx * bw, ink);
        } else {
            float fs = b->kind == K_SMALL ? s * 0.36f : fminf(s * 0.3f, b->r.w / 6.f);
            font_draw(cx, cy - fs / 2, fs, ink, ALIGN_CENTER, b->label);
        }
    }
    gfx_flush();
}

uint16_t touch_press(float x, float y, int64_t finger)
{
    for (int i = 0; i < nb; i++) {
        tbtn *b = &btn[i];
        if (x >= b->r.x && x < b->r.x + b->r.w && y >= b->r.y && y < b->r.y + b->r.h) {
            b->held = 1;
            b->finger = finger;
            return b->key;
        }
    }
    return 0;
}

uint16_t touch_release(int64_t finger)
{
    uint16_t k = 0;
    for (int i = 0; i < nb; i++)
        if (btn[i].held && btn[i].finger == finger) {
            btn[i].held = 0;
            k = btn[i].key;
        }
    return k;
}

void touch_release_all(void)
{
    for (int i = 0; i < nb; i++) btn[i].held = 0;
}
