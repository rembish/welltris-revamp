/* On-screen controls for touch screens. Buttons produce the same key words as the keyboard. */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include "view.h"

enum touch_screen { TOUCH_GAME, TOUCH_CONFIRM, TOUCH_NONE };

void touch_layout(const view_layout_t *L, int screen);
void touch_draw(float t);
/* returns the key of the button under (x, y) and marks it held by `finger`, or 0 */
uint16_t touch_press(float x, float y, int64_t finger);
/* returns the key that `finger` was holding, or 0 */
uint16_t touch_release(int64_t finger);
void touch_release_all(void);

#endif
