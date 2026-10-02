#include "view.h"
#include "art.h"
#include "font.h"
#include "../core/wt_tables.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int touch_mode;

void view_set_touch(int on) { touch_mode = on; }

/* The well seen from above: a square rim, four walls sloping down in perspective to an 8x8
 * floor. Depth z runs 0 (rim) .. 1 (floor); the floor appears FLOOR_K times the rim's size,
 * like the original's well picture. */
#define FLOOR_K 0.36f
#define DEPTH_K (1.f / FLOOR_K - 1.f)

static float persp(float z) { return 1.f / (1.f + z * DEPTH_K); }

typedef struct {
    float cx, cy, r;
} proj_t;

/* (X, Y) on the unit square at depth z -> screen */
static void project(const proj_t *P, float X, float Y, float z, float *sx, float *sy)
{
    float s = persp(z);
    *sx = P->cx + X * P->r * s;
    *sy = P->cy + Y * P->r * s;
}

/* a point on wall w, u in 0..8 along it (direction the column index grows), at depth z */
static void wall_point(const proj_t *P, int w, float u, float z, float *sx, float *sy)
{
    float X, Y, t = u / 4.f;
    switch (w) {
    case 0: X = -1, Y = -1 + t; break; /* left, columns top to bottom */
    case 1: Y = 1, X = -1 + t; break;  /* bottom, left to right */
    case 2: X = 1, Y = 1 - t; break;   /* right, bottom to top */
    default: Y = -1, X = 1 - t; break; /* top, right to left */
    }
    project(P, X, Y, z, sx, sy);
}

static float row_z(int r) { return (float)(11 - r) / 12.f; } /* top edge of row r; row 11 at the rim */

static void wall_cell_quad(const proj_t *P, int col, int row, float inset, float *q)
{
    int w = col >> 3;
    float u0 = (float)(col & 7) + inset, u1 = (float)(col & 7) + 1 - inset;
    float z0 = row_z(row) + inset / 12.f, z1 = row_z(row) + (1 - inset) / 12.f;
    wall_point(P, w, u0, z0, &q[0], &q[1]);
    wall_point(P, w, u1, z0, &q[2], &q[3]);
    wall_point(P, w, u1, z1, &q[4], &q[5]);
    wall_point(P, w, u0, z1, &q[6], &q[7]);
}

/* floor cell x (screen row, top to bottom), y (screen column, left to right) */
static void floor_cell_quad(const proj_t *P, int x, int y, float inset, float *q)
{
    float X0 = -1 + ((float)y + inset) / 4, X1 = -1 + ((float)y + 1 - inset) / 4;
    float Y0 = -1 + ((float)x + inset) / 4, Y1 = -1 + ((float)x + 1 - inset) / 4;
    project(P, X0, Y0, 1, &q[0], &q[1]);
    project(P, X1, Y0, 1, &q[2], &q[3]);
    project(P, X1, Y1, 1, &q[4], &q[5]);
    project(P, X0, Y1, 1, &q[6], &q[7]);
}

/* the 16 EGA colours the pieces use, made a little richer */
static const unsigned cell_hex[16] = { 0x000000, 0x3b6cf0, 0x22c55e, 0x14b8c8, 0xe5483b, 0xb44fd6,
                                       0xe8892a, 0xc3cad6, 0x7d8899, 0x5aa9f5, 0x7ee787, 0x6ee7f0,
                                       0xff7a6e, 0xf472b6, 0xfacc4d, 0xf4f6fb };

rgba view_cell_color(int c) { return rgb_hex(cell_hex[c & 15], 1); }

/* light falls from the top left: the top and left walls are lit, the others in shade */
static const float wall_light[4] = { 1.0f, 0.62f, 0.72f, 0.88f };

static void cell(const float *q, rgba base, float light, float glow)
{
    float in[8], cx = (q[0] + q[2] + q[4] + q[6]) / 4, cy = (q[1] + q[3] + q[5] + q[7]) / 4;
    rgba c = rgba_scale(base, light);
    rgba edge = rgba_scale(c, 0.55f);
    gfx_quad(q, edge);
    for (int i = 0; i < 4; i++) {
        in[2 * i] = q[2 * i] + (cx - q[2 * i]) * 0.16f;
        in[2 * i + 1] = q[2 * i + 1] + (cy - q[2 * i + 1]) * 0.16f;
    }
    rgba hi = rgba_mix(c, rgb_hex(0xffffff, 1), 0.35f + glow * 0.4f);
    rgba lo = rgba_scale(c, 0.85f);
    const rgba cols[4] = { hi, rgba_mix(hi, lo, 0.5f), lo, rgba_mix(hi, lo, 0.5f) };
    gfx_quad4(in, cols);
}

void view_well(const wt_game *g, box b, const view_fx *fx, int show_piece)
{
    proj_t P = { b.x + b.w / 2, b.y + b.h / 2, b.w / 2 * 0.94f };
    float q[8];
    int w, c, r, x, y;
    float t = fx ? fx->time : 0;

    /* rim */
    gfx_rect(b.x, b.y, b.w, b.h, rgb_hex(0x10131c, 1));
    gfx_rect_outline(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 2, rgb_hex(0x2a3142, 1));

    /* wall faces: dark, deeper is darker; frozen walls glow red like the original */
    for (w = 0; w < 4; w++) {
        int frozen = g->frozen[w] != 0;
        int lit = fx && fx->walls_blink && g->fx_walls[w];
        for (r = 0; r < WT_ROWS; r++) {
            float depth = row_z(r);
            rgba base = frozen ? rgb_hex(0x9b2c2c, 1) : rgb_hex(0x2b3446, 1);
            if (lit) base = fx->walls_blink > 0 ? rgb_hex(0xffd2c4, 1) : base;
            base = rgba_scale(base, wall_light[w] * (1.05f - depth * 0.45f));
            for (c = 0; c < 8; c++) {
                wall_cell_quad(&P, w * 8 + c, r, 0, q);
                gfx_quad(q, base);
            }
        }
        /* grid lines */
        for (c = 0; c <= 8; c++) {
            float x0, y0, x1, y1;
            wall_point(&P, w, (float)c, 0, &x0, &y0);
            wall_point(&P, w, (float)c, 1, &x1, &y1);
            gfx_line(x0, y0, x1, y1, 1.f, rgb_hex(0x0b0e14, 0.75f));
        }
        for (r = 0; r <= WT_ROWS; r++) {
            float x0, y0, x1, y1, z = (float)r / 12.f;
            wall_point(&P, w, 0, z, &x0, &y0);
            wall_point(&P, w, 8, z, &x1, &y1);
            gfx_line(x0, y0, x1, y1, 1.f, rgb_hex(0x0b0e14, 0.75f));
        }
    }
    /* floor */
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            floor_cell_quad(&P, x, y, 0, q);
            gfx_quad(q, rgb_hex((x + y) & 1 ? 0x141925 : 0x171d2a, 1));
        }
    {
        float x0, y0, x1, y1;
        project(&P, -1, -1, 1, &x0, &y0);
        project(&P, 1, 1, 1, &x1, &y1);
        gfx_rect_outline(x0, y0, x1 - x0, y1 - y0, 1.5f, rgb_hex(0x3a4560, 1));
    }

    /* settled cells */
    for (c = 0; c < WT_COLS; c++)
        for (r = 0; r < WT_ROWS; r++)
            if (g->wall[c][r]) {
                wall_cell_quad(&P, c, r, 0.04f, q);
                cell(q, view_cell_color(g->wall[c][r]), wall_light[c >> 3] * (1.05f - row_z(r) * 0.4f), 0);
            }
    for (x = 0; x < 8; x++)
        for (y = 0; y < 8; y++) {
            int v = g->floor[x][y], blink = 0;
            if (fx && fx->lines_blink && (g->row_full[x] || g->col_full[y])) blink = fx->lines_blink;
            if (blink < 0) continue;
            if (!v && !blink) continue;
            floor_cell_quad(&P, x, y, 0.05f, q);
            cell(q, blink > 0 ? rgb_hex(0xffffff, 1) : view_cell_color(v), 0.78f, blink > 0 ? 1.f : 0);
        }

    /* the falling piece */
    if (show_piece && g->piece_active) {
        int pc[32], pr[32], n = wt_piece_cells(&g->piece, pc, pr, 32);
        float pulse = 0.12f + 0.08f * sinf(t * 6.f);
        for (int i = 0; i < n; i++) {
            if (pr[i] >= WT_ROWS) continue;
            if (pr[i] >= 0) {
                wall_cell_quad(&P, pc[i], pr[i], 0.04f, q);
                cell(q, view_cell_color(g->piece.color), wall_light[pc[i] >> 3] * 1.1f, pulse);
            } else {
                int fxx = pc[i], fyy = pr[i];
                wt_wall_to_floor(&fxx, &fyy);
                if (fxx < 0 || fxx > 7 || fyy < 0 || fyy > 7) continue;
                floor_cell_quad(&P, fxx, fyy, 0.05f, q);
                cell(q, view_cell_color(g->piece.color), 0.95f, pulse);
            }
        }
    }
    gfx_flush();
}

/* ---- panel ---- */

static void next_piece_draw(const wt_game *g, box b)
{
    wt_piece p;
    int cls = 0, idx = g->next_piece, pc[32], pr[32], n, i;
    int minc = 99, maxc = -99, minr = 99, maxr = -99;
    while (cls < 3 && idx >= (int)wt_class_count[cls]) idx -= (int)wt_class_count[cls++];
    wt_build_piece(&p, cls, idx);
    p.col = 16;
    p.row = 6;
    n = wt_piece_cells(&p, pc, pr, 32);
    for (i = 0; i < n; i++) {
        if (pc[i] < minc) minc = pc[i];
        if (pc[i] > maxc) maxc = pc[i];
        if (pr[i] < minr) minr = pr[i];
        if (pr[i] > maxr) maxr = pr[i];
    }
    float cw = fminf(b.w / 5.5f, b.h / 5.5f);
    float ox = b.x + (b.w - (float)(maxc - minc + 1) * cw) / 2,
          oy = b.y + (b.h - (float)(maxr - minr + 1) * cw) / 2;
    for (i = 0; i < n; i++) {
        float x = ox + (float)(pc[i] - minc) * cw, y = oy + (float)(maxr - pr[i]) * cw;
        const float q[8] = { x, y, x + cw, y, x + cw, y + cw, x, y + cw };
        cell(q, view_cell_color(p.color), 1.f, 0);
    }
}

static void panel(const wt_game *g, box b, const view_fx *fx)
{
    float s = fminf(b.h / 7.2f, b.w / 9.f);
    float x = b.x + s * 0.6f, y = b.y + s * 0.45f, vx = b.x + b.w * 0.62f;
    static const char *names[4] = { "LEVEL", "SPEED", "SCORE", "LINES" };
    char v[4][24];
    gfx_round_rect(b.x, b.y, b.w, b.h, s * 0.35f, rgb_hex(0x0d111a, 0.92f));
    gfx_rect_outline(b.x, b.y, b.w, b.h, 2, rgb_hex(0x3a4560, 1));
    snprintf(v[0], sizeof v[0], "%d", g->piece_set + 1);
    snprintf(v[1], sizeof v[1], "%d", g->level + 1);
    snprintf(v[2], sizeof v[2], "%lu", (unsigned long)g->score);
    snprintf(v[3], sizeof v[3], "%lu", (unsigned long)g->lines);
    for (int i = 0; i < 4; i++) {
        font_draw(x, y + (float)i * s * 1.15f, s * 0.78f, rgb_hex(0x35c3c8, 1), ALIGN_LEFT, names[i]);
        font_draw(vx, y + (float)i * s * 1.15f, s * 0.78f, rgb_hex(0xe9eef7, 1), ALIGN_RIGHT, v[i]);
    }
    if (fx && fx->hiscore > 0)
        font_drawf(x, y + 4.6f * s, s * 0.55f, rgb_hex(0x7f8aa3, 1), ALIGN_LEFT, "HIGH  %ld",
                   (long)fx->hiscore);
    box nb = { vx + s * 0.4f, b.y + s * 0.3f, b.x + b.w - vx - s * 0.7f, b.h - s * 0.6f };
    font_draw(nb.x + nb.w / 2, y, s * 0.6f, rgb_hex(0x35c3c8, 1), ALIGN_CENTER, "NEXT");
    if (g->preview_on && g->piece_active) {
        box pb = { nb.x, nb.y + s, nb.w, nb.h - s };
        next_piece_draw(g, pb);
    }
    gfx_flush();
}

/* ---- scenes ---- */

static void dome(float x, float base, float w, float h, rgba c)
{
    /* an onion dome on a drum */
    gfx_rect(x - w * 0.32f, base - h * 0.45f, w * 0.64f, h * 0.45f, c);
    for (int i = 0; i < 16; i++) {
        float a0 = (float)i / 16 * (float)M_PI, a1 = (float)(i + 1) / 16 * (float)M_PI;
        float r0 = sinf(a0) * (1.f + 0.35f * sinf(a0 * 2)), r1 = sinf(a1) * (1.f + 0.35f * sinf(a1 * 2));
        float y0 = base - h * 0.45f - (1 - cosf(a0)) * h * 0.22f,
              y1 = base - h * 0.45f - (1 - cosf(a1)) * h * 0.22f;
        const float q[8] = { x - r0 * w * 0.5f, y0, x + r0 * w * 0.5f, y0,
                             x + r1 * w * 0.5f, y1, x - r1 * w * 0.5f, y1 };
        gfx_quad(q, c);
    }
    gfx_tri(x - w * 0.06f, base - h * 0.88f, x + w * 0.06f, base - h * 0.88f, x, base - h * 1.15f, c);
}

void view_scene(box b, int speed, float t)
{
    static const unsigned sky[5][2] = { { 0x2b5d9c, 0xf5b971 },
                                        { 0x1f4f7a, 0x8fd3f4 },
                                        { 0x3a2350, 0xf08a5d },
                                        { 0x0f1d3a, 0x5b7fb8 },
                                        { 0x08091a, 0x3b2d6b } };
    rgba top = rgb_hex(sky[speed % 5][0], 1), bot = rgb_hex(sky[speed % 5][1], 1);
    float base = b.y + b.h * 0.82f;
    (void)t;
    gfx_rect_v(b.x, b.y, b.w, b.h * 0.82f, top, bot);
    gfx_rect(b.x, base, b.w, b.y + b.h - base, rgb_hex(0x1a1414, 1));
    rgba wall = rgba_scale(rgb_hex(0x7a1f1f, 1), speed >= 3 ? 0.55f : 0.9f);
    rgba roof = rgba_scale(rgb_hex(0x1f5f4a, 1), speed >= 3 ? 0.55f : 0.9f);
    /* Kremlin wall with merlons */
    gfx_rect(b.x, base - b.h * 0.06f, b.w, b.h * 0.06f, wall);
    for (int i = 0; i < 18; i++)
        gfx_rect(b.x + b.w * (float)i / 18, base - b.h * 0.08f, b.w / 40, b.h * 0.02f, wall);
    /* a tower with a spire */
    float tx = b.x + b.w * 0.3f, tw = b.w * 0.16f;
    gfx_rect(tx - tw / 2, base - b.h * 0.42f, tw, b.h * 0.42f, wall);
    gfx_tri(tx - tw * 0.55f, base - b.h * 0.42f, tx + tw * 0.55f, base - b.h * 0.42f, tx, base - b.h * 0.66f,
            roof);
    gfx_tri(tx - b.w * 0.01f, base - b.h * 0.66f, tx + b.w * 0.01f, base - b.h * 0.66f, tx, base - b.h * 0.7f,
            rgb_hex(0xd94a3a, 1));
    /* a cathedral with onion domes */
    float cx = b.x + b.w * 0.72f;
    gfx_rect(cx - b.w * 0.17f, base - b.h * 0.22f, b.w * 0.34f, b.h * 0.22f, rgba_scale(wall, 0.9f));
    dome(cx, base - b.h * 0.22f, b.w * 0.14f, b.h * 0.26f, roof);
    dome(cx - b.w * 0.12f, base - b.h * 0.22f, b.w * 0.08f, b.h * 0.15f,
         rgba_scale(rgb_hex(0xd9a441, 1), 0.9f));
    dome(cx + b.w * 0.12f, base - b.h * 0.22f, b.w * 0.08f, b.h * 0.15f, rgb_hex(0x3a6dd9, 1));
    gfx_flush();
}

/* ---- layout ---- */

void view_layout(int w, int h, view_layout_t *L)
{
    float W = (float)w, H = (float)h, m = fminf(W, H) * 0.025f;
    memset(L, 0, sizeof *L);
    L->portrait = H > W * 1.15f;
    if (L->portrait) {
        float s = fminf(W - 2 * m, H * 0.56f);
        L->well = (box){ (W - s) / 2, m, s, s };
        float py = L->well.y + s + m, ph = fminf(H * 0.14f, s * 0.3f);
        L->panel = (box){ L->well.x, py, s * 0.6f, ph };
        L->scene = (box){ L->well.x + s * 0.62f, py, s * 0.38f, ph };
        if (touch_mode) {
            float ty = py + ph + m;
            L->left = (box){ m, ty, W / 2 - 1.5f * m, H - ty - m };
            L->right = (box){ W / 2 + m / 2, ty, W / 2 - 1.5f * m, H - ty - m };
        }
        return;
    }
    float side = touch_mode ? W * 0.17f : 0;
    float avail = W - 2 * side;
    float s = fminf(H - 2 * m, avail * 0.6f);
    float total = s + m + s * 0.62f;
    float x0 = side + (avail - total) / 2;
    L->well = (box){ x0, (H - s) / 2, s, s };
    float px = x0 + s + m, pw = s * 0.62f;
    L->panel = (box){ px, L->well.y, pw, s * 0.3f };
    L->scene = (box){ px, L->well.y + s * 0.3f + m, pw, s - s * 0.3f - m };
    if (touch_mode) {
        L->left = (box){ m, H * 0.3f, side - 2 * m, H * 0.65f };
        L->right = (box){ W - side + m, H * 0.3f, side - 2 * m, H * 0.65f };
    }
}

void view_background(int w, int h)
{
    gfx_rect_v(0, 0, (float)w, (float)h, rgb_hex(0x07090f, 1), rgb_hex(0x111726, 1));
}

void view_game(const wt_game *g, const view_layout_t *L, const view_fx *fx)
{
    view_well(g, L->well, fx, 1);
    panel(g, L->panel, fx);
    int sp = g->level > 4 ? 4 : g->level;
    if (art_have(ART_SCENE1 + sp)) {
        /* keep the picture's 264:350 (shown at 4:3) proportions, centred, cover the box */
        gfx_rect(L->scene.x, L->scene.y, L->scene.w, L->scene.h, rgb_hex(0x000000, 1));
        float aw = 264.f, ah = 480.f;
        float k = fminf(L->scene.w / aw, L->scene.h / ah);
        art_draw(ART_SCENE1 + sp, L->scene.x + (L->scene.w - aw * k) / 2,
                 L->scene.y + (L->scene.h - ah * k) / 2, aw * k, ah * k, 1);
    } else {
        view_scene(L->scene, sp, fx ? fx->time : 0);
    }
    gfx_rect_outline(L->scene.x, L->scene.y, L->scene.w, L->scene.h, 2, rgb_hex(0x3a4560, 1));
    if (fx && fx->message) {
        box b = L->well;
        float s = b.w * 0.07f;
        gfx_round_rect(b.x + b.w * 0.12f, b.y + b.h * 0.38f, b.w * 0.76f,
                       b.h * (fx->submessage ? 0.26f : 0.2f), s * 0.3f, rgb_hex(0x0b0e16, 0.88f));
        font_draw(b.x + b.w / 2, b.y + b.h * 0.42f, s, rgb_hex(0xffd166, 1), ALIGN_CENTER, fx->message);
        if (fx->submessage)
            font_draw(b.x + b.w / 2, b.y + b.h * 0.42f + s * 1.4f, s * 0.45f, rgb_hex(0xc9d6f5, 1),
                      ALIGN_CENTER, fx->submessage);
    }
    gfx_flush();
}
