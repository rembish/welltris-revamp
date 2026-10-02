/* Game screen rendering. */
#ifndef VIEW_H
#define VIEW_H

#include "gfx.h"
#include "../core/wt_core.h"

typedef struct {
    float x, y, w, h;
} box;

typedef struct {
    box well;  /* square around the well's rim */
    box panel; /* level / speed / score / lines / next */
    box scene; /* the picture of the current speed */
    box left, right; /* touch control areas (empty without touch) */
    int portrait;
} view_layout_t;

typedef struct {
    float time;            /* seconds, for subtle effects */
    int lines_blink;       /* cleared floor lines: 1 shown lit, -1 hidden, 0 normal */
    int walls_blink;       /* walls in fx_walls: 1 lit, -1 normal, 0 none */
    const char *message;   /* big overlay text or NULL */
    const char *submessage;
    int32_t hiscore;
} view_fx;

void view_set_touch(int on);
void view_layout(int w, int h, view_layout_t *L);
void view_background(int w, int h);
void view_game(const wt_game *g, const view_layout_t *L, const view_fx *fx);
void view_well(const wt_game *g, box b, const view_fx *fx, int show_piece);
/* the procedural stand-in for a scene picture, speed 0..4 */
void view_scene(box b, int speed, float t);
rgba view_cell_color(int c);

#endif
