# Art brief: homage artwork for the public build

*Done: the set in `assets/art` moved the theme from Moscow to Prague. Kept as the reference for
redoing or adding pictures. The frontend takes each picture's own size; boards it fills in
(set-up menu, hall of fame) are located by pixel coordinates in `src/main.c`, so a redrawn
`setup` or `highscore` needs those updated.*

The original pictures (title, set-up screen, five scenes, hall of fame) are Spectrum HoloByte's
and only ever go into private builds (`re/tools/assets.py`, `-DWT_LOCAL_ASSETS=ON`). The public
build gets its own homage set: new pictures on the same themes, made from text prompts.

## Rules

- **Text prompts only.** Don't upload the original screens or ask to "redraw this": an
  image-to-image result is a derivative of the original art and can't be committed either.
  Describe the theme instead (the prompts below do).
- **No text, letters or logos in the pictures.** Generators garble them, and the game draws its
  own text (menu labels, scores, the WELLTRIS title) on top.
- **No real people.** The fifth scene is a nod to the game's creator, but keep it a generic
  person, not a likeness.
- **One style for the whole set:** pick a style suffix, keep it on every prompt, and reuse the
  same seed or style reference (Midjourney `--sref`) after the first picture you like.

Suggested style suffix:

> late-1980s Soviet glasnost era, painterly gouache illustration, bold shapes, warm saturated
> colours, soft film grain, cinematic light, no text, no letters, no logos

## Files

Put the finished pictures in `assets/art/` under these names (PNG or JPEG, sRGB). Sizes are
minimums; larger is fine.

| File | Aspect | Min size | Used for | Keep clear |
|---|---|---|---|---|
| `title` | 4:3 | 2048×1536 | Title screen | upper third (the WELLTRIS logo goes there) |
| `setup` | 4:3 | 2048×1536 | Set-up menu background | left 55% calm and darker (menu panel) |
| `scene1`–`scene5` | 11:20 | 1100×2000 | Picture beside the well, one per speed | nothing |
| `hiscore` | 4:3 | 2048×1536 | Hall of fame background | left half calm (score table) |
| `credits` | 4:3 | 2048×1536 | Credits background | centre calm (text panel) |

The frames, panels and dialogs are drawn in code to match.

## Prompts

Append the style suffix to each. Midjourney: add `--ar 4:3` or `--ar 11:20`, and `--sref` once
you have a picture whose style you want everywhere.

1. **title**: Moscow skyline at golden sunset seen across the river, St Basil's Cathedral
   onion domes and Kremlin towers in silhouette along the lower third, dramatic pink, orange
   and gold clouds filling the upper sky, calm water reflecting the light.
2. **setup**: indoor ice arena in the 1980s, a pair of figure skaters mid-spin in sequinned red
   and gold costumes on the right, spotlights from the roof, flags hanging from the rafters,
   crowd in the dim stands; the left side is dark stands and empty ice.
3. **scene1** (speed 1): Kremlin Trinity Tower with its red brick and green spire, the stone
   bridge leading up to it, tourists in 1980s clothes walking towards the viewer, bright blue
   sky with white clouds, vertical composition.
4. **scene2** (speed 2): sunny Moscow park on a summer day, a white food van with a red stripe
   selling pizza, people queueing, a child flying a kite, birch trees, vertical composition.
5. **scene3** (speed 3): romantic cinema poster mood without text, a young couple embracing in
   the foreground, a woman's face in large soft monochrome portrait behind them, red brick wall
   at the edge, vertical composition.
6. **scene4** (speed 4): Soviet rock concert, long-haired guitarist singing on stage under a red
   spotlight, raised hands and banners in the crowd below, smoke and glare, vertical composition.
7. **scene5** (speed 5): a bearded programmer in a blue sweater smiling at the viewer, a 1980s
   personal computer with a colourful falling-blocks game on its screen beside him, abstract
   painting on the wall, cozy office, vertical composition.
8. **hiscore**: warm evening celebration at a Moscow apartment, friends around a table with
   food, glasses raised in a toast, window behind showing the city at night with a Stalinist
   tower; the left half is the dim wall of the room.
9. **credits**: Red Square at night after snowfall, lamps glowing, Kremlin wall and St Basil's
   softly lit, quiet and empty.
