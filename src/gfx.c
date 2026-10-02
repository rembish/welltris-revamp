#include "gfx.h"
#include <math.h>

#define MAXV 65536

static SDL_Renderer *ren;
static SDL_Vertex verts[MAXV];
static int nv;

void gfx_init(SDL_Renderer *r)
{
    ren = r;
    nv = 0;
}

void gfx_flush(void)
{
    if (nv) SDL_RenderGeometry(ren, NULL, verts, nv, NULL, 0);
    nv = 0;
}

static SDL_Color col(rgba c)
{
    float a = c.a < 0 ? 0 : c.a > 1 ? 1 : c.a;
#define CL(v) (Uint8)((v) < 0 ? 0 : (v) > 1 ? 255 : (v) * 255.f + .5f)
    return (SDL_Color){ CL(c.r), CL(c.g), CL(c.b), CL(a) };
#undef CL
}

static void vtx(float x, float y, SDL_Color c)
{
    if (nv == MAXV) gfx_flush();
    verts[nv++] = (SDL_Vertex){ { x, y }, c, { 0, 0 } };
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, rgba c)
{
    if (nv > MAXV - 3) gfx_flush();
    SDL_Color k = col(c);
    vtx(x0, y0, k);
    vtx(x1, y1, k);
    vtx(x2, y2, k);
}

void gfx_quad(const float *p, rgba c)
{
    if (nv > MAXV - 6) gfx_flush();
    SDL_Color k = col(c);
    vtx(p[0], p[1], k);
    vtx(p[2], p[3], k);
    vtx(p[4], p[5], k);
    vtx(p[0], p[1], k);
    vtx(p[4], p[5], k);
    vtx(p[6], p[7], k);
}

void gfx_quad4(const float *p, const rgba *c)
{
    if (nv > MAXV - 6) gfx_flush();
    SDL_Color k[4] = { col(c[0]), col(c[1]), col(c[2]), col(c[3]) };
    vtx(p[0], p[1], k[0]);
    vtx(p[2], p[3], k[1]);
    vtx(p[4], p[5], k[2]);
    vtx(p[0], p[1], k[0]);
    vtx(p[4], p[5], k[2]);
    vtx(p[6], p[7], k[3]);
}

void gfx_rect(float x, float y, float w, float h, rgba c)
{
    const float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    gfx_quad(p, c);
}

void gfx_rect_v(float x, float y, float w, float h, rgba top, rgba bottom)
{
    const float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    rgba c[4] = { top, top, bottom, bottom };
    gfx_quad4(p, c);
}

/* A line is a core quad plus two feathered strips fading to transparent. */
void gfx_line(float x0, float y0, float x1, float y1, float w, rgba c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len, ny = dx / len;
    float core = w > 1 ? (w - 1) * .5f : 0, feather = 1.f;
    rgba t = rgba_alpha(c, 0);
    if (w < 1) c.a *= w;
    float a = core, b = core + feather;
    if (core > 0) {
        const float q[8] = { x0 + nx * a, y0 + ny * a, x1 + nx * a, y1 + ny * a,
                             x1 - nx * a, y1 - ny * a, x0 - nx * a, y0 - ny * a };
        gfx_quad(q, c);
    }
    const float s1[8] = { x0 + nx * b, y0 + ny * b, x1 + nx * b, y1 + ny * b,
                          x1 + nx * a, y1 + ny * a, x0 + nx * a, y0 + ny * a };
    const float s2[8] = { x0 - nx * a, y0 - ny * a, x1 - nx * a, y1 - ny * a,
                          x1 - nx * b, y1 - ny * b, x0 - nx * b, y0 - ny * b };
    rgba c1[4] = { t, t, c, c }, c2[4] = { c, c, t, t };
    gfx_quad4(s1, c1);
    gfx_quad4(s2, c2);
}

void gfx_rect_outline(float x, float y, float w, float h, float lw, rgba c)
{
    gfx_line(x, y, x + w, y, lw, c);
    gfx_line(x + w, y, x + w, y + h, lw, c);
    gfx_line(x + w, y + h, x, y + h, lw, c);
    gfx_line(x, y + h, x, y, lw, c);
}

/* Translucent: one triangle fan, so nothing is blended twice. */
#define FAN_SEG 8

static void round_rect_fan(float x, float y, float w, float h, float rad, rgba c)
{
    const int seg = FAN_SEG;
    const float cx[4] = { x + w - rad, x + w - rad, x + rad, x + rad };
    const float cy[4] = { y + rad, y + h - rad, y + h - rad, y + rad };
    const float a0[4] = { (float)M_PI * 1.5f, 0, (float)M_PI * .5f, (float)M_PI };
    float px[4 * (FAN_SEG + 1)], py[4 * (FAN_SEG + 1)];
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= seg; i++) {
            float t = a0[k] + (float)M_PI * .5f * i / seg;
            px[n] = cx[k] + cosf(t) * rad;
            py[n] = cy[k] + sinf(t) * rad;
            n++;
        }
    float mx = x + w / 2, my = y + h / 2;
    for (int i = 0; i < n; i++) gfx_tri(mx, my, px[i], py[i], px[(i + 1) % n], py[(i + 1) % n], c);
}

void gfx_round_rect(float x, float y, float w, float h, float rad, rgba c)
{
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    if (c.a < 0.999f) {
        round_rect_fan(x, y, w, h, rad, c);
        return;
    }
    /* pieces overlap by a pixel so rasteriser seams never show (fills are opaque) */
    gfx_rect(x + rad - 1, y, w - 2 * rad + 2, h, c);
    gfx_rect(x, y + rad - 1, rad + 1, h - 2 * rad + 2, c);
    gfx_rect(x + w - rad - 1, y + rad - 1, rad + 1, h - 2 * rad + 2, c);
    const int seg = 8;
    const float cx[4] = { x + rad, x + w - rad, x + w - rad, x + rad };
    const float cy[4] = { y + rad, y + rad, y + h - rad, y + h - rad };
    const float a0[4] = { (float)M_PI, (float)M_PI * 1.5f, 0, (float)M_PI * .5f };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < seg; i++) {
            float t0 = a0[k] + (float)M_PI * .5f * i / seg, t1 = a0[k] + (float)M_PI * .5f * (i + 1) / seg;
            float ox = k == 0 || k == 3 ? 1 : -1, oy = k < 2 ? 1 : -1;
            gfx_tri(cx[k] + ox, cy[k] + oy, cx[k] + cosf(t0) * rad, cy[k] + sinf(t0) * rad,
                    cx[k] + cosf(t1) * rad, cy[k] + sinf(t1) * rad, c);
        }
}
