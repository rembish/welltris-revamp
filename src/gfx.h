/* Small immediate-mode 2D helpers on top of SDL_RenderGeometry. */
#ifndef GFX_H
#define GFX_H

#include <SDL.h>

#if defined(__GNUC__) || defined(__clang__)
#define WT_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define WT_PRINTF(fmt, args)
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float r, g, b, a;
} rgba;

static inline rgba rgb_hex(unsigned hex, float a)
{
    return (rgba){ ((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a };
}
static inline rgba rgba_scale(rgba c, float k) { return (rgba){ c.r * k, c.g * k, c.b * k, c.a }; }
static inline rgba rgba_mix(rgba a, rgba b, float t)
{
    return (rgba){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                   a.a + (b.a - a.a) * t };
}
static inline rgba rgba_alpha(rgba c, float a)
{
    c.a = a;
    return c;
}

void gfx_init(SDL_Renderer *r);
void gfx_flush(void); /* submit batched untextured triangles */
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, rgba c);
void gfx_quad(const float *p, rgba c);         /* 4 points, convex, in order */
void gfx_quad4(const float *p, const rgba *c); /* per-vertex colours */
void gfx_rect(float x, float y, float w, float h, rgba c);
void gfx_rect_v(float x, float y, float w, float h, rgba top, rgba bottom);
void gfx_line(float x0, float y0, float x1, float y1, float w, rgba c); /* anti-aliased */
void gfx_rect_outline(float x, float y, float w, float h, float lw, rgba c);
void gfx_round_rect(float x, float y, float w, float h, float rad, rgba c);

#endif
