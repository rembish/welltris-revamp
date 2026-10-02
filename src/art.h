/* Optional artwork decoded from the player's own copy of the game (re/tools/assets.py) and
 * embedded only in local builds (-DWT_LOCAL_ASSETS=ON). Everything must work without it. */
#ifndef ART_H
#define ART_H

#include <SDL.h>

enum {
    ART_TITLE,
    ART_SETUP,
    ART_SCENE1, /* five scenes, one per speed */
    ART_SCENE2,
    ART_SCENE3,
    ART_SCENE4,
    ART_SCENE5,
    ART_WELL1,
    ART_WELL2,
    ART_HISCORE1,
    ART_HISCORE2,
    ART_DIALOG,
    ART_CREDITS,
    ART_COUNT
};

/* original screen coordinates are 640x350; the art is that, shown at 4:3 */
#define ART_SRC_W 640.f
#define ART_SRC_H 350.f

void art_init(SDL_Renderer *r);
int art_have(int id);
int art_any(void);
/* draws image id into the rectangle, stretched */
void art_draw(int id, float x, float y, float w, float h, float alpha);
/* source size in original pixels (e.g. 264x350 for a scene) */
void art_src_size(int id, float *w, float *h);

#endif
