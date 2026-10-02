# WELLTRIS.EXE reverse-engineering notes

Addresses: code = offset in segment `1000` (load-relative 0), data = `ds:XXXX` (DGROUP at
load-relative paragraph `0x0a39`, Ghidra segment `1a39`). Symbol names live in
`ghidra/names.txt`. Items marked **[verify]** still need a check in DOSBox-X.

## Binary layout

- Turbo C 2.0 (1988 copyright string), small model, no overlay, not packed.
  Game code `0000–79c7`, Turbo C startup at `79c8` (`entry`) and RTL after it.
- `main` = `1b12`. Init, then forever: menu (`4c32` until 0) → game.
- Resources: `*.bin` (EGA/Hercules) and `t-*.bin` (Tandy/CGA 320×200) images, RLE packed;
  `data.bin` holds game data [verify]. Saved files: `KBB/scores.bin`, `options.bin`.

## Copy protection (not ported)

Manual lookup: "Please enter the {name|area|capital} of this republic: [answer on p. N of
manual]". `5dbc`, called once from `main`: picks the question with `rand()%13` (republic)
and `rand()%3` (field), decodes a 6-byte record (`(b ^ 0x37) - 0x37`): `[0]` question
text index, `[1]` page, `[2..5]` expected first 4 letters. Two wrong answers → exit.
On success it stores the typed letters at `ds:0236` and the expected ones at `ds:023a`.

Delayed check: `2378` (start of every piece) compares `ds:0236` with `ds:023a` again and
calls `fatal_exit(6)` (`0029`) on mismatch. Neither check touches game state, but the two
`rand()` calls in `5dbc` advance the RNG once at startup before the first game; the port must
keep those two calls for identical piece sequences [verify against seeding].

`crack.exe` (1996) patches 3 bytes of `WELLTRIS.EXE` to bypass this.
