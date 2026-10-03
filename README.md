# Welltris (1989) — decompilation & multiplatform port

Welltris is Alexey Pajitnov's three-dimensional follow-up to Tetris: pieces slide down the four
walls of a well and onto its 8×8 floor, where full rows *and* columns clear. Walls that a piece
sticks to freeze for three turns; four frozen walls end the game. This repo is a
reverse-engineered port of the DOS version (Spectrum HoloByte, 1989) to portable C + SDL2: it
runs natively on Linux, Windows and macOS, and in a browser via WebAssembly. No original game
files are included or needed to play.

The goal has two halves:

1. **Game logic: decompiled faithfully.** Piece sets, the Turbo C random number generator and
   how it is used, spawning, moves and rotation (quirks included), gravity and drops on the
   original's 187 Hz timer, wall freezing and thawing, pieces sliding down thawed walls, floor
   line clears, scoring, level ups and the timing of every blocking effect. This is a
   deterministic, platform-independent C core that is checked pass by pass against the
   original machine code (see [How faithful is it?](#how-faithful-is-it)).
2. **Presentation: modernized.** The well and the panel are drawn fresh at any resolution. The
   pictures are a homage: the original's Moscow became Prague (the title over the Vltava, a
   hockey arena for the set-up screen, five Prague scenes for the five speeds, a pub for the hall
   of fame). The original artwork can be upscaled from *your own copy* into a private build.

## Layout

| Path        | Contents |
|-------------|----------|
| `core/`     | Game logic reconstructed from `WELLTRIS.EXE`: plain C99, no I/O, deterministic |
| `src/`      | SDL2 frontend: rendering, input, PC-speaker sound, menus, hall of fame, touch controls |
| `tests/`    | Replay tool and a search bot used by the differential test |
| `assets/`   | Font and the homage pictures (`assets/art`, see [`docs/art-brief.md`](docs/art-brief.md)) |
| `web/`      | HTML shell for the browser build |
| `re/`       | Reverse-engineering notes, Ghidra scripts, table generator, emulator harness, asset tool |

## The original

The tools in `re/` need your own copy of the DOS release in `original/` (git-ignored, never
distributed). The executable they were written against:

```
6a977c98cb17bd612cca4ffa293c65a1d9020236460a03bc313881f51f90bcd0  welltris.exe
```

- `welltris.exe` is the whole game: Turbo C 2.0, small model, one 50 KB executable.
- `*.bin` / `t-*.bin` are the pictures for EGA and for the 320×200 modes (RLE-packed planar
  images). All game rules live in the executable; none of them are in the pictures.
- The game asks a manual-lookup question at start ("the capital of this republic…") and checks
  the answer again on every piece and in the menu. The port leaves the protection out (it
  keeps the two random numbers it draws, so the piece sequence is unaffected).

## Playing

| Key | Action |
|-----|--------|
| Arrow keys / keypad 8 2 4 6 / I M J L | move the piece along the wall it is on |
| K / keypad 5 | rotate |
| Space | drop |
| Alt+P or Esc | pause |
| Alt+N | next piece preview on/off (the preview costs 5 points per piece) |
| Alt+M | move mode: arrows follow the screen (1) or always left/right (2) |
| Alt+S | sound on/off |
| Alt+A / Alt+R | abort / restart the game |
| Alt+Q | quit |
| F11 / Alt+Enter | fullscreen |

As in the original, `5` turns the piece one quarter and `K` three quarters (the key code is
passed on as the number of turns). On a touch screen the same actions are on-screen buttons.

The set-up screen is the original's: LEVEL 1–3 picks the piece set (small pieces and
tetrominoes, tetrominoes, everything up to pentominoes), SPEED 1.0–5.0 the starting speed. Scores
and options are stored per user (`~/.local/share/Welltris/Welltris/` on Linux,
`%APPDATA%\Welltris\Welltris\` on Windows, browser storage on the web) in the original formats,
so `scores.bin` and `options.bin` from the DOS version can be copied there. Unlike the original,
which only wrote scores when you quit cleanly, the port saves them straight away.

## Building

Native (Linux, macOS, Windows with any SDL2 package):

```sh
cmake -S . -B build
cmake --build build -j
./build/welltris
```

Browser (Emscripten):

```sh
source ~/tools/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web
cmake --build build-web -j --target welltris
cd build-web && python3 -m http.server   # open http://localhost:8000/welltris.html
```

### With the original artwork (private builds only)

```sh
python3 -m venv ~/tools/venv --system-site-packages
~/tools/venv/bin/pip install vtracer resvg-py opencv-python-headless numpy pillow
~/tools/venv/bin/python re/tools/assets.py build          # needs original/*.bin
cmake -S . -B build-local -DWT_LOCAL_ASSETS=ON && cmake --build build-local -j
```

`assets.py` decodes the pictures and upscales them 3× to 4:3 into the git-ignored
`assets-local/`: line art (frames, panels) is traced into vectors and re-rendered, dithered
pictures (title, scenes, set-up, hall of fame) are de-dithered and sharpened. Such a build uses
the original screens and layouts instead of the homage set. The artwork is Spectrum
HoloByte's: never commit it or publish a build that contains it.

## How faithful is it?

Everything that decides how the game plays was reconstructed from `WELLTRIS.EXE` and is checked
against the original machine code:

- `re/emu/wtemu.py` loads the original executable into the Unicorn CPU emulator, stubs out only
  drawing, image loading and the speaker, replaces the timer, keyboard and far heap, and runs
  the real `game_init` and play loop headless.
- `re/emu/difftest.py` feeds the same key scripts to the original and to `core/` and compares
  the complete game state after every pass of the play loop: walls, floor, stored pieces,
  score, lines, deadlines, the random number generator and the clock. Half of the scripts are
  random keys (with bursts and prefilled floor lines), half are games played by a search bot
  (`tests/botgen.c`) that clears lines and reaches bonus pieces and level ups.
- `re/emu/mutants.py` breaks the core in 15 small ways (timings, scoring, collisions, key
  handling) and checks that the difftest catches every one.

  ```sh
  python3 -m venv ~/tools/venv --system-site-packages && ~/tools/venv/bin/pip install unicorn capstone
  ~/tools/venv/bin/python re/emu/difftest.py 40
  ~/tools/venv/bin/python re/emu/mutants.py 30
  ```

Game tables come from the binary (`re/tools/gen_tables.py`), so no number was typed in by hand.
Findings and addresses are in [`re/NOTES.md`](re/NOTES.md).

Where the original depended on the machine, the port follows the EGA version: the play loop
paces itself on the page flip (60 Hz frames) while gravity, drops and effects run on the
187.17 Hz timer the game programs into the PIT, and the BIOS key repeat is emulated. Sounds are
the original's PC-speaker sweeps, played from the core's log with their exact timing. Pause,
the abort/restart/quit confirmations and the hall of fame follow the code but are not
emulator-tested.

## Tooling

| Tool | Used for | Install |
|------|----------|---------|
| Ghidra (headless) | Decompiling `WELLTRIS.EXE` (16-bit real mode); names in `re/ghidra/names.txt` | zip from GitHub into `~/tools/`, needs `openjdk-21-jdk` |
| Python 3 + capstone, Unicorn | Disassembly, the emulator harness and differential tests | `pip install unicorn capstone` in a venv |
| vtracer, resvg, OpenCV, Pillow | The local artwork tool | see above |
| DOSBox-X + Xvfb | Running the original as a reference, headless screenshots | `apt install dosbox-x xvfb` |
| gcc + SDL2 + CMake | Native build | `apt install libsdl2-dev cmake` |
| Emscripten (emsdk) | WebAssembly build | `git clone emsdk` into `~/tools/emsdk` |

## Code quality

The core and tests build with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion` and more (the frontend without the conversion warnings), and are kept
clean under clang-tidy (`.clang-tidy`), cppcheck and clang-format (`.clang-format`).
CI runs all of this on every push.

## Credits

Welltris © 1989 Doka; American version © 1989 Sphere, Inc. / Spectrum HoloByte. Original
design Alexey Pajitnov and Andrei Snegov; American version by Dan Kaufman, Kevin Seghetti,
Kus Pranawahadi, Greg Marr, Dan Guerra, Jody Sather and Matt Carlstrom. This is an
unofficial fan reimplementation for preservation; no original game files are distributed.
Homage pictures made for this port. Font: Exo 2 (SIL OFL). Text and image decoding:
stb_truetype, stb_image (public domain).
