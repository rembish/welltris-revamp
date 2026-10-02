/* TrueType text via stb_truetype, rendered from a baked glyph atlas. */
#ifndef FONT_H
#define FONT_H

#include "gfx.h"

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

int font_init(SDL_Renderer *r, const unsigned char *ttf, int ttf_len);
float font_width(float size, const char *s);
/* (x, y) is the top-left / top-centre / top-right of the text box of height `size`. */
float font_draw(float x, float y, float size, rgba c, int align, const char *s);
float font_drawf(float x, float y, float size, rgba c, int align, const char *fmt, ...) WT_PRINTF(6, 7);

#endif
