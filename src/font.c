#include "font.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../third_party/stb_truetype.h"

#define ATLAS  1024
#define FIRST  32
#define COUNT  96
#define NSIZES 3

/* Glyphs are baked at a few sizes; text uses the closest larger one, which keeps
   small labels crisp (SDL2 has no mipmaps). */
static const float bake_px[NSIZES] = { 18.f, 36.f, 72.f };
static SDL_Renderer *ren;
static SDL_Texture *tex;
static stbtt_packedchar chars[NSIZES][COUNT];
static float ascent_k; /* ascent / pixel height */

static int pick(float size)
{
    for (int i = 0; i < NSIZES; i++)
        if (bake_px[i] >= size * 0.9f) return i;
    return NSIZES - 1;
}

int font_init(SDL_Renderer *r, const unsigned char *ttf, int ttf_len)
{
    (void)ttf_len;
    ren = r;
    unsigned char *bmp = calloc(ATLAS, ATLAS);
    Uint32 *pixels = malloc(ATLAS * ATLAS * 4);
    stbtt_pack_context pc;
    if (!bmp || !pixels || !stbtt_PackBegin(&pc, bmp, ATLAS, ATLAS, 0, 2, NULL)) {
        free(bmp);
        free(pixels);
        return 0;
    }
    for (int i = 0; i < NSIZES; i++) {
        int os = bake_px[i] < 40 ? 2 : 1;
        stbtt_PackSetOversampling(&pc, os, os);
        stbtt_PackFontRange(&pc, ttf, 0, bake_px[i], FIRST, COUNT, chars[i]);
    }
    stbtt_PackEnd(&pc);

    stbtt_fontinfo fi;
    stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
    int a, d, g;
    stbtt_GetFontVMetrics(&fi, &a, &d, &g);
    ascent_k = a * stbtt_ScaleForPixelHeight(&fi, 1.f);

    for (int i = 0; i < ATLAS * ATLAS; i++) pixels[i] = 0x00ffffffu | ((Uint32)bmp[i] << 24);
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ATLAS, ATLAS);
    SDL_UpdateTexture(tex, NULL, pixels, ATLAS * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    free(pixels);
    free(bmp);
    return 1;
}

float font_width(float size, const char *s)
{
    int z = pick(size);
    float k = size / bake_px[z], w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < FIRST || c >= FIRST + COUNT) c = '?';
        w += chars[z][c - FIRST].xadvance;
    }
    return w * k;
}

float font_draw(float x, float y, float size, rgba col, int align, const char *s)
{
    gfx_flush();
    int z = pick(size);
    float k = size / bake_px[z], w = font_width(size, s);
    if (align == ALIGN_CENTER)
        x -= w / 2;
    else if (align == ALIGN_RIGHT)
        x -= w;
    SDL_Color c = { (Uint8)(col.r * 255), (Uint8)(col.g * 255), (Uint8)(col.b * 255), (Uint8)(col.a * 255) };
    static SDL_Vertex v[6 * 256];
    int n = 0;
    float px = 0, base = ascent_k * bake_px[z] * 0.88f; /* visually centre caps in the box */
    for (; *s && n < 6 * 256; s++) {
        int ch = (unsigned char)*s;
        if (ch < FIRST || ch >= FIRST + COUNT) ch = '?';
        stbtt_aligned_quad q;
        float py = 0;
        stbtt_GetPackedQuad(chars[z], ATLAS, ATLAS, ch - FIRST, &px, &py, &q, 0);
        float x0 = x + q.x0 * k, x1 = x + q.x1 * k;
        float y0 = y + (q.y0 + base) * k, y1 = y + (q.y1 + base) * k;
        SDL_Vertex a = { { x0, y0 }, c, { q.s0, q.t0 } }, b = { { x1, y0 }, c, { q.s1, q.t0 } };
        SDL_Vertex d = { { x1, y1 }, c, { q.s1, q.t1 } }, e = { { x0, y1 }, c, { q.s0, q.t1 } };
        v[n++] = a;
        v[n++] = b;
        v[n++] = d;
        v[n++] = a;
        v[n++] = d;
        v[n++] = e;
    }
    SDL_RenderGeometry(ren, tex, v, n, NULL, 0);
    return w;
}

float font_drawf(float x, float y, float size, rgba c, int align, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return font_draw(x, y, size, c, align, buf);
}
