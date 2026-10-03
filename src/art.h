/* The pictures, embedded at build time. Two sets:
 * - the homage set in assets/art (Prague instead of Moscow), the default;
 * - the original artwork, upscaled from your own copy of the game by re/tools/assets.py into the
 *   git-ignored assets-local/ and used with -DWT_LOCAL_ASSETS=ON (private builds only).
 * The original set also has its dialog/credits frames and lays out its screens itself; the
 * homage pictures leave boards for the frontend to fill. */
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
    ART_HISCORE1, /* homage: the whole hall of fame picture */
    ART_HISCORE2, /* original only: its right half */
    ART_DIALOG,
    ART_CREDITS,
    ART_COUNT
};

void art_init(SDL_Renderer *r);
int art_have(int id);
/* every picture the frontend needs is there */
int art_complete(void);
/* this build has the original artwork (with its 640x350 screen layouts) */
int art_original(void);
/* size of image id in its own pixels */
void art_size(int id, float *w, float *h);
/* draws image id into the rectangle, stretched */
void art_draw(int id, float x, float y, float w, float h, float alpha);

#endif
