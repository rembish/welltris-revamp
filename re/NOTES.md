# WELLTRIS.EXE reverse-engineering notes

Addresses: code = offset in segment `1000` (load-relative 0), data = `ds:XXXX` (DGROUP at
load-relative paragraph `0x0a39`, Ghidra segment `1a39`). Symbol names live in
`ghidra/names.txt`. Items marked **[verify]** still need a check (emulator or DOSBox-X).

## Binary layout

- Turbo C 2.0 (1988 copyright string), small model, no overlay, not packed.
  Game code `0000–79c7`, Turbo C startup at `79c8` (`entry`) and RTL after it.
- `main` = `1b12`: `parse_args`, `init`, title, `title_wait(200)`, `copy_protection`, preload
  images, then forever: `options_menu` → `game_init` → `game_iteration` until `game_over` →
  top score / game-over sounds → hall of fame.
- Command line: first letter of `argv[1]` picks the video mode (`ds:111b`): `E` EGA 0x10,
  `T` Tandy 9, `C` CGA 4, `H` Hercules 0x7f, `RC`/`RH` inverse CGA/Hercules. Otherwise the
  mode menu (`460b`) runs.
- `ds:0017` (`double_buffer`) = 1 for EGA (two pages, flip with vsync), 0 for every other
  mode (draw to back buffer, copy rectangles). Some timings differ between the two paths;
  **the port follows the EGA path**.
- Resources: `*.bin` (EGA) and `t-*.bin` (other modes) images, RLE (`0d53`: byte `0x80|n`
  then value = n copies, else literal). All game logic tables are in the EXE, not in the
  resources. Saved files: `scores.bin` (10 × 24 bytes) and `options.bin` (6 bytes).
- The draw thunks `787c–79a5` patch their own call site on first use to jump straight to the
  EGA (`69xx`) or non-EGA (`6exx`) routine.

## Copy protection (not ported)

Manual lookup: "Please enter the {name|area|capital} of this republic: [answer on p. N of
manual]". `5dbc`, called once from `main`: picks the question with `rand()%13` (republic)
and `rand()%3` (field), decodes a 6-byte record (`(b ^ 0x37) - 0x37`): `[0]` question
text index, `[1]` page, `[2..5]` expected first 4 letters. Two wrong answers → exit.
On success it stores the typed letters and the expected ones twice: `ds:0236`/`ds:023a`
and `ds:0725`/`ds:0729`.

Delayed checks: `spawn_piece` (`2378`, every piece) compares `0236`/`023a`, `options_menu`
(`4c32`) compares `0725`/`0729`; both call `fatal_exit(6)` on mismatch. Nothing else reads
them. The port drops all of it but keeps the two `rand()` calls (they advance the RNG).

`crack.exe` (1996) patches 3 bytes of `WELLTRIS.EXE` to bypass this.

## Timing

- `timer_install` (`66c5`) hooks INT 8 and programs PIT channel 0 with divisor `0x18e7`
  (6375): **187.17 Hz** ticks. ISR `6740`: `if (!paused) ticks++` (32-bit at `cs:6684`,
  pause flag `cs:6688`); chains to the BIOS handler every 10th tick.
- `ticks_after(n)` = now + n, `ticks_reached(t)` = now ≥ t. `delay_ticks(n)` busy-waits for
  `ticks ≥ start + n`, i.e. until the start of tick `now + n`.
- `title_wait(n)` (`014c`): `srand(time(NULL))`, then `rand()` on every poll until n ticks
  pass or a key is pressed. The RNG position at the first game therefore depends on the date,
  the CPU speed and the player. Nothing reseeds later: all games of a session share one stream.
- The play loop has no fixed rate. In EGA mode each `game_iteration` ends with a page flip
  (`69c7`) that sets the CRTC start address and **waits for the next vertical retrace**, so
  one iteration = one display frame (~60 Hz). Gravity and drops run on the 187 Hz ticks.
- `sound_sweep` (`03fa`) uses `delay_ticks` for every note whether sound is on or off, so
  sound effects take the same time in both cases.

### Fall speed

- `fall_delay` (`ds:0026`), ticks per row. Set by `options_menu` on exit:
  `0x119 - Σ level_speedup[0..level-1]`; `level_speedup` (`ds:0038`) = 75, 66, 66, 47
  (these bytes double as the `"KBB/"` prefix of the `"KBB/scores.bin"` string).
  Levels 0–4: 281, 206, 140, 74, 27 ticks (1.50 s … 0.14 s).
- Space (`dropping`) switches to a 3-tick fall until the piece lands.

## Playfield

- Four walls of 8 columns × 12 rows, stored as one ring of 32 columns: `wall_cells[32][12]`
  (`ds:1128`); column `c` is on wall `c >> 3`, columns wrap 31 ↔ 0. Row 11 is the top, row
  0 the bottom of the wall.
- Floor `floor_cells[8][8]` (`ds:12b2`). Rows < 0 mean "on the floor": `wall_to_floor`
  (`2645`) maps (column, row) to floor x/y depending on the wall the column belongs to.
  `floor_limit` (`ds:1096`) = −8 normally (a piece can slide across the whole floor), −4
  while settling wall pieces.
- `wall_frozen[4]` (`ds:12aa`, pairs `{frozen, count}`): a wall touched by a piece that
  stopped on it is frozen; after 3 more pieces it thaws (`count > 3`) and the pieces stored
  on it are dropped again (`settle_wall_pieces`, `2b42`). Game over when all four walls are
  frozen at once.
- `wall_pieces[32][15]` (`ds:1320`): far pointers to copies of pieces stored on the walls,
  indexed by (column, row + 3).

## Pieces

- Piece record = colour byte, flags byte, then a chain of direction codes ending in 0:
  1 = left (column − 1, wraps), 2 = down (row − 1), 3 = right, 4 = up; bit 7 marks a cell
  already on the floor; code 5 = origin cell. `piece_next_cell` (`2706`) walks the chain.
- Rotation (`piece_rotate`, `1def`) adds 1 to each code (1→2→3→4→1, 5 untouched) and to the
  rotation counter (`+2`, mod 4). Records at offset `0x42` and `0x6e` never rotate.
- `piece_rotate(piece, n)` makes up to `n` quarter turns **while the piece fits**: it stops at
  the first turn that collides (or crosses walls on the floor, `1acc`) and undoes that one
  turn. The keyboard handler calls it without the second argument, so `n` is the caller's
  local key code: `5` = 53 turns ≡ 1 step, `K` = 75 ≡ 3 steps, `k` = 107 ≡ 3 steps (the other
  way round), unless a turn on the way collides. For the non-rotating records the result is
  an uninitialised local, which on the keyboard path is the 0 that `poll_key_global` left in
  the same stack slot, so only `1acc` decides whether the fail sound plays.
- `spawn_piece` (`2378`) calls it with `rand()%4 + 1`: a random start orientation.
- Classes (`ds:0206` counts, `ds:01fe` record sizes, `piece_sets` at `ds:130c`):
  class 0: 3 pieces at `ds:0102` (6 bytes), class 1: 7 at `ds:0114` (8), class 2: 18 at
  `ds:014c` (9), class 3: 1 at `ds:01ee` (15, level bonus piece).
- `choose_piece` (`2518`), by piece set option (`ds:109f`):
  - 0: class = `rand()%2`; class 0 → `rand()%3`, class 1 → `rand()%7`
  - 1: class 1, `rand()%7`
  - 2: class = `rand()%3`, then `%3` / `%7` / `%18`
  - level bonus (`ds:10f8`): class 2 for set 2 (`rand()%18`), class 3 index 0 for set 2
    (overrides), otherwise as above.
- Spawn: row 12 (top), column = `(rand()%4)*8 + rand()%3 + 3`; while that wall is frozen,
  column += 8 (wraps at > 32); then the spawn rotation.

## Play loop (`game_iteration`, `0dcc`) [verify]

Per call:
1. Copy the piece, `update_current_wall`.
2. Unless dropping: `handle_play_key` (`149f`) takes **one** key from the BIOS buffer,
   handles it, then drains and discards the rest of the buffer.
3. If the fall or drop deadline passed (or a key forced it):
   - If no piece is active (it landed last time): advance the frozen counters, thaw walls,
     settle, clear full floor lines/columns (`find_full_floor_lines`), score, level-up check,
     choose and spawn the next piece, reset deadlines, flush the keyboard.
   - Else `piece_fall` (`1cf4`); new deadline; `drop_rows_left` (`ds:111c`) counts down per
     natural fall step. If the piece cannot fall: stored on a wall (row ≥ 0) → freeze those
     walls; landed on the floor → becomes floor cells.
4. Level up when `lines_to_level` (`ds:131e`) ≤ 0 (levels 0–4); Alt-I forces it.
5. Page flip (vsync).

### Keys (`handle_play_key`)

`get_key` returns ASCII, or `scan | 0x8000` for extended keys.

| key | action |
|---|---|
| `K` `k` `5` | rotate |
| Space | drop (3-tick fall) |
| `I` `i` `8` Up / `M` `m` `2` Down | move along the left/right walls |
| `J` `j` `4` Left / `L` `l` `6` Right | move along the top/bottom walls |
| Alt-P | pause (until Alt-P) |
| Alt-A / Alt-R | abort / restart (with confirmation) |
| Alt-I | skip to next level |
| Alt-N | toggle next-piece preview |
| Alt-M | toggle fixed keys (left/right not remapped per wall) |
| Alt-S | sound on/off |
| Alt-Q | quit |

Without fixed keys the direction keys are remapped by `current_wall` (`ds:10f4`) so they
match the screen direction on each wall.

## Score (`score_for_piece`, `2084`) [verify]

```
s  = score_level[level] + score_class[class]        (ds:0224: 0,5,10,20,45; ds:022e: 5,10,15,20)
s += 30                       if record offset == 0x10a
s += (drop_rows_left + 1) * 2 * (level + 1)
s += score_lines[n] + piece_set * n * 25            (ds:0216: 0,25,50,100,150,250,400,…)
s  = max(s - 5, 0)            if preview on
if lines_to_level == 0: s += level*25 + 50, and if not landed: s += (piece_set == 2 ? 50 : 20)
if level bonus:         s += (piece_set == 2 ? 150 : 75)
if floor empty bonus:   s += 500
```
Score is a 32-bit total, game ends with a top-score message at ≥ 1 000 000 000.

## Options (`options.bin`, `options_menu` `4c32`)

6 bytes: `[0]` piece set (`ds:109f`), `[1]` sound off, `[2]` fixed keys (`ds:0010`),
`[3]` preview off (`ds:000f`), `[4]` start level (`ds:1098`), `[5]` video mode.
Menu entries 5–8: hall of fame, credits, save options, quit.

## Findings from the emulator harness

- `a262` is `tolower`: the protection's expected letters are stored lower case.
- `3699` (draw one cell) is also game logic: with `store` set it writes the cell into
  `wall_cells`/`floor_cells`, and its return value (0xffff for a wall cell, 0 for a floor
  cell, and for rows ≥ 12 whatever AX held at the call: the colour byte in `35de`) decides in
  `35de` whether a stopped piece is stored on the wall or ends the game.
- `piece_next_cell(codes, &col, &row, &skip, &idx)` (`2706`): `skip` = bit 7 of the code just
  walked; `35de` skips the cell after a flagged code.
- Pause (Alt-P) does not stop the tick counter: the ISR's pause flag `cs:6688` is never set
  (its setters `672c`/`6733` have no callers). Deadlines expire during a pause.

## Verification

`re/emu/difftest.py` compares the core with the emulated original after every pass of the
play loop (full board, walls, stored pieces, score, deadlines, RNG, clock). Odd runs are
played by a search bot (`tests/botgen.c`) that clears lines and reaches bonus pieces and level
ups; even runs are random keys (all move/rotate keys, Space, Alt-I/N/M, unbound keys, bursts)
with prefilled floor lines. `re/emu/mutants.py` breaks the core in 15 ways (timings, scoring,
collision, walker semantics, key handling) and checks that difftest notices each; rare paths
are pinned by regression seeds (227: empty-floor bonus, 36: rotation across walls).

## Images

`load_image` (`08ba`) files: `{u16 size, u16 0, u16 bytes_per_row, u16 rows}` then PCX-style
RLE (byte ≥ 0xc0: `byte & 0x3f` copies of the next byte). Unpacked: 4 bit planes one after the
other, plane *p* = colour bit *p*, default EGA palette (the game never reprograms it).
`clip.bin` (`load_clip`, 13 sub-images), `pieces.bin` and `data.bin` (`load_image_rle`,
`{u16 packed, u16 unpacked, 3 words}` + `0x80|n` RLE) hold the perspective cell sprites and UI
clips, which the port draws itself.

`re/tools/assets.py` decodes the images from your own copy and builds upscaled versions into
the git-ignored `assets-local/`: line art is traced (vtracer) and rasterised, dithered pictures
are de-dithered (blur, bilateral filtering, sharpening). Only builds made with
`-DWT_LOCAL_ASSETS=ON` embed them; nothing derived from the original art is committed.
