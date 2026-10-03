#include "art.h"
#include "gfx.h"

#include "../third_party/stb_image.h"

/* from the generated art_data.c */
extern const unsigned char *const art_blob[ART_COUNT];
extern const int art_blob_len[ART_COUNT];

static SDL_Renderer *ren;
static SDL_Texture *tex[ART_COUNT];
static int tex_w[ART_COUNT], tex_h[ART_COUNT];

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
            tex_w[i] = w;
            tex_h[i] = h;
            SDL_UpdateTexture(tex[i], NULL, px, w * 4);
            SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
        }
        stbi_image_free(px);
    }
}

int art_have(int id) { return id >= 0 && id < ART_COUNT && tex[id] != NULL; }

int art_complete(void)
{
    for (int i = ART_TITLE; i <= ART_SCENE5; i++)
        if (!tex[i]) return 0;
    return tex[ART_HISCORE1] != NULL;
}

int art_original(void) { return tex[ART_HISCORE2] && tex[ART_DIALOG] && tex[ART_CREDITS]; }

void art_draw(int id, float x, float y, float w, float h, float alpha)
{
    SDL_FRect d = { x, y, w, h };
    if (!art_have(id)) return;
    gfx_flush();
    SDL_SetTextureAlphaMod(tex[id], (Uint8)(alpha < 0 ? 0 : alpha > 1 ? 255 : alpha * 255.f));
    SDL_RenderCopyF(ren, tex[id], NULL, &d);
}

void art_size(int id, float *w, float *h)
{
    *w = (float)(id >= 0 && id < ART_COUNT ? tex_w[id] : 0);
    *h = (float)(id >= 0 && id < ART_COUNT ? tex_h[id] : 0);
}
