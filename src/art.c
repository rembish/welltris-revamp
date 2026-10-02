#include "art.h"
#include "gfx.h"

#include "../third_party/stb_image.h"

/* from the generated art_data.c (empty unless WT_LOCAL_ASSETS) */
extern const unsigned char *const art_blob[ART_COUNT];
extern const int art_blob_len[ART_COUNT];

static const float src_w[ART_COUNT] = { 640, 640, 264, 264, 264, 264, 264, 376, 376, 320, 320, 216, 368 };
static const float src_h[ART_COUNT] = { 350, 350, 350, 350, 350, 350, 350, 64, 286, 350, 350, 83, 250 };

static SDL_Renderer *ren;
static SDL_Texture *tex[ART_COUNT];

void art_init(SDL_Renderer *r)
{
    ren = r;
    for (int i = 0; i < ART_COUNT; i++) {
        int w, h, n;
        unsigned char *px;
        if (!art_blob[i] || art_blob_len[i] <= 0) continue;
        px = stbi_load_from_memory(art_blob[i], art_blob_len[i], &w, &h, &n, 4);
        if (!px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (tex[i]) {
            SDL_UpdateTexture(tex[i], NULL, px, w * 4);
            SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
        }
        stbi_image_free(px);
    }
}

int art_have(int id) { return id >= 0 && id < ART_COUNT && tex[id] != NULL; }

int art_any(void)
{
    for (int i = 0; i < ART_COUNT; i++)
        if (tex[i]) return 1;
    return 0;
}

void art_draw(int id, float x, float y, float w, float h, float alpha)
{
    SDL_FRect d = { x, y, w, h };
    if (!art_have(id)) return;
    gfx_flush();
    SDL_SetTextureAlphaMod(tex[id], (Uint8)(alpha < 0 ? 0 : alpha > 1 ? 255 : alpha * 255.f));
    SDL_RenderCopyF(ren, tex[id], NULL, &d);
}

void art_src_size(int id, float *w, float *h)
{
    *w = src_w[id];
    *h = src_h[id];
}
