"""Run the original WELLTRIS.EXE play loop headless under Unicorn.

The image is loaded at segment 0x1000 (same addresses as the Ghidra project) and relocated.
Game routines are called directly; drawing and the speaker are stubbed, the timer, the
keyboard and the far heap are replaced by Python.

Time is kept in PIT input clocks (1193182 Hz). The game's tick counter (cs:6684, 187.17 Hz,
one tick = 6375 clocks) is written from it, so ticks_after / ticks_reached run natively.
Only delay_ticks and the EGA page flip (which waits for vertical retrace) advance the clock
inside a call; between calls the driver advances it to the next event.
"""
import os, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '..', 'original', 'welltris.exe')

CS = 0x1000
DS = CS + 0x0a39
SENTINEL = 0xfff0          # unused address in CS (gap above BSS, below the stack); reaching it ends a call
STACK_TOP = 0xfffe         # SS = DS, small model
HEAP_SEG = 0x5000          # far heap for farmalloc

TICK = 6375                # PIT clocks per game tick
FRAME = 19886              # PIT clocks per EGA frame (60 Hz)

# Routines replaced with an immediate `ret`. Each one only draws, loads images or drives the
# speaker; none writes state the game logic reads (checked against the decompiled callees).
STUBS = {
    0x787c: 'blit', 0x789d: 'copy_page', 0x78df: 'draw_wall_cell', 0x7900: 'blit_part',
    0x7921: 'draw_floor_cell', 0x7942: 'draw_number', 0x7963: 'fill_rect', 0x79a5: 'draw_cursor',
    0x7831: 'copy_rect', 0x77f1: 'copy_dirty', 0x7422: 'copy_rect_impl',
    0x316f: 'load_image', 0x3425: 'load_image_rle', 0x33af: 'free_image',
    0x5adf: 'draw_game_screen', 0x3949: 'draw_preview', 0x3c86: 'erase_preview',
    0xa22f: 'sound', 0xa25b: 'nosound', 0x79c6: 'sound_toggle_hook',
}


class WT:
    def __init__(self):
        raw = open(EXE, 'rb').read()
        hdr = struct.unpack('<H', raw[8:10])[0] * 16
        img = bytearray(raw[hdr:])
        nrel = struct.unpack('<H', raw[6:8])[0]
        rtab = struct.unpack('<H', raw[0x18:0x1a])[0]
        for k in range(nrel):
            off, seg = struct.unpack('<HH', raw[rtab + 4 * k: rtab + 4 * k + 4])
            p = seg * 16 + off
            v = struct.unpack('<H', img[p:p + 2])[0]
            img[p:p + 2] = struct.pack('<H', (v + CS) & 0xffff)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(CS * 16, bytes(img))
        self.clock = 0             # PIT clocks
        self.keys = []             # [(clock, keyword)], sorted
        self.kbuf = []             # BIOS type-ahead buffer (15 entries)
        self.heap = HEAP_SEG
        self.flipped = False
        self.exit_code = None
        for a in STUBS:
            mu.mem_write(CS * 16 + a, b'\xc3')
        mu.hook_add(UC_HOOK_INTR, self._intr)
        self.pyfuncs = {
            0x0000: WT.get_key,
            0x011c: lambda e, a: e.delay(a[0]),
            0x78be: lambda e, a: e.vsync(),
            0x7d52: lambda e, a: e.farmalloc(a[0]),
            0x94b3: lambda e, a: 0,                  # farfree
            0x0029: WT.fatal_exit,
        }
        for a in self.pyfuncs:
            mu.hook_add(UC_HOOK_CODE, self._py, begin=CS * 16 + a, end=CS * 16 + a)

    # ---- memory helpers (DGROUP offsets) ----
    def rb(self, off, n=1): return bytes(self.mu.mem_read(DS * 16 + off, n))
    def r8(self, off): return self.rb(off)[0]
    def s8(self, off): return struct.unpack('<b', self.rb(off))[0]
    def r16(self, off): return struct.unpack('<h', self.rb(off, 2))[0]
    def ru16(self, off): return struct.unpack('<H', self.rb(off, 2))[0]
    def r32(self, off): return struct.unpack('<i', self.rb(off, 4))[0]
    def wb(self, off, data): self.mu.mem_write(DS * 16 + off, bytes(data))
    def w8(self, off, v): self.wb(off, [v & 0xff])
    def w16(self, off, v): self.wb(off, struct.pack('<H', v & 0xffff))
    def w32(self, off, v): self.wb(off, struct.pack('<I', v & 0xffffffff))
    def far(self, seg, off, n): return bytes(self.mu.mem_read(seg * 16 + off, n))

    # ---- time ----
    def ticks(self): return self.clock // TICK

    def sync_ticks(self):
        self.mu.mem_write(CS * 16 + 0x6684, struct.pack('<I', self.ticks()))

    def set_clock(self, c):
        assert c >= self.clock
        self.clock = c
        self.sync_ticks()
        while self.keys and self.keys[0][0] <= self.clock:
            k = self.keys.pop(0)[1]
            if len(self.kbuf) < 15: self.kbuf.append(k)

    def delay(self, n):
        """delay_ticks(n): busy-waits until the tick counter reaches start + n."""
        if n: self.set_clock((self.ticks() + n) * TICK)

    def vsync(self):
        """EGA page flip: waits for the start of the next vertical retrace."""
        self.flipped = True
        self.set_clock((self.clock // FRAME + 1) * FRAME)

    # ---- keyboard ----
    def get_key(self, args):
        self.set_clock(self.clock)
        return self.kbuf.pop(0) if self.kbuf else 0

    def fatal_exit(self, args):
        self.exit_code = args[0]
        self.mu.emu_stop()
        return 0

    # ---- heap ----
    def farmalloc(self, n):
        seg = self.heap
        self.heap += (n + 15) // 16
        assert self.heap < 0x9f00, 'fake heap exhausted'
        self.mu.mem_write(seg * 16, bytes(n))
        self.mu.reg_write(UC_X86_REG_DX, seg)
        return 0

    def _intr(self, mu, intno, _):
        ax = mu.reg_read(UC_X86_REG_AX)
        raise RuntimeError(f'unhandled int {intno:02x} ax={ax:04x} at {mu.reg_read(UC_X86_REG_IP):04x}')

    def _py(self, mu, addr, size, _):
        sp = mu.reg_read(UC_X86_REG_SP)
        ss = mu.reg_read(UC_X86_REG_SS)
        rd = lambda o: struct.unpack('<H', bytes(mu.mem_read(ss * 16 + ((sp + o) & 0xffff), 2)))[0]
        ret = rd(0)
        args = [rd(2 + 2 * k) for k in range(4)]
        ax = self.pyfuncs[addr - CS * 16](self, args)
        if self.exit_code is not None: return
        mu.reg_write(UC_X86_REG_AX, (ax or 0) & 0xffff)
        mu.reg_write(UC_X86_REG_SP, (sp + 2) & 0xffff)
        mu.reg_write(UC_X86_REG_IP, ret)

    # ---- calling ----
    def call(self, addr, *args):
        mu = self.mu
        for seg in (UC_X86_REG_DS, UC_X86_REG_SS, UC_X86_REG_ES):
            mu.reg_write(seg, DS)
        mu.reg_write(UC_X86_REG_CS, CS)
        sp = STACK_TOP
        for a in reversed(args):
            sp -= 2; self.w16(sp, a)
        sp -= 2; self.w16(sp, SENTINEL)
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_BP, 0)
        mu.emu_start(CS * 16 + addr, CS * 16 + SENTINEL, count=50_000_000)
        return mu.reg_read(UC_X86_REG_AX), mu.reg_read(UC_X86_REG_DX)


class Game(WT):
    """A game as main() plays it: options as the menu leaves them, game_init, then
    game_iteration until game_over. Between iterations the clock jumps to the next event
    (next key or the active deadline) when the iteration did nothing visible."""

    def __init__(self):
        super().__init__()
        self.log = []

    def setup(self, seed, piece_set=0, level=0, preview=True, fixed_keys=False, clock=0):
        self.clock = clock
        self.sync_ticks()
        self.w32(0x1080, seed)                       # Turbo C rand() state
        self.w8(0x109f, piece_set); self.w8(0x1098, level)
        self.w8(0x000d, 0)                           # sound off (timing is the same either way)
        self.w8(0x000f, 0 if preview else 0xff)
        self.w8(0x0010, 0xff if fixed_keys else 0)
        d = 0x119
        for i in range(level): d -= self.r8(0x0038 + i)
        self.w16(0x0026, d)
        self.w16(0x1096, 0xfff8)                     # floor_limit, set by init()
        self.wb(0x0236, b'wellwell')                 # copy protection: answer == expected
        self.wb(0x0725, b'wellwell')
        self.call(0x0739)                            # game_init

    def next_event(self):
        """Clock of the next thing that can change the game: a key or the active deadline."""
        if self.r8(0x0012):                          # dropping: keys are not read
            return max((self.r32(0x1124) & 0xffffffff) * TICK, self.clock)
        if self.kbuf: return self.clock
        t = (self.r32(0x1120) & 0xffffffff) * TICK
        if self.keys: t = min(t, self.keys[0][0])
        return max(t, self.clock)

    def iterate(self):
        self.flipped = False
        self.call(0x0dcc)
        if self.exit_code is None and not self.flipped:
            self.set_clock(self.next_event())

    def snapshot(self):
        piece = self.rb(0x12f2, 0x1a)
        stored = []
        for c in range(32):
            for r in range(15):
                o, s = self.ru16(0x1320 + 4 * (c * 15 + r)), self.ru16(0x1322 + 4 * (c * 15 + r))
                if o or s: stored.append('%d,%d:%s' % (c, r - 3, self.far(s, o, 0x1a).hex()))
        return ' '.join([
            str(self.clock), str(self.r8(0x000c)), piece.hex(),
            self.rb(0x1128, 384).hex(), self.rb(0x12b2, 64).hex(), self.rb(0x12aa, 8).hex(),
            str(self.r32(0x109a)), str(self.r32(0x002c)), str(self.r8(0x1098)), str(self.r16(0x131e)),
            str(self.r16(0x0026)), str(self.r32(0x1120)), str(self.r32(0x1124)),
            self.rb(0x0011, 6).hex(), str(self.r8(0x10f4)), str(self.r8(0x111c)), str(self.r16(0x1114)),
            str(self.r32(0x1080)), ';'.join(stored)])

    def run(self, keys, max_iter=100000):
        self.keys = sorted(keys)
        self.log.append(self.snapshot())
        for _ in range(max_iter):
            if self.r8(0x000c) or self.exit_code is not None: break
            self.iterate()
            self.log.append(self.snapshot())
        return self.log


if __name__ == '__main__':
    import random, sys
    g = Game()
    g.setup(seed=int(sys.argv[1]) if len(sys.argv) > 1 else 1)
    rng = random.Random(5)
    ks = [(rng.randint(0, 600 * FRAME), rng.choice([0x6b, 0x4b, 0x35, 0x20, 0x8048, 0x8050, 0x804b, 0x804d]))
          for _ in range(800)]
    log = g.run(ks, 20000)
    print(len(log), 'iterations, exit', g.exit_code)
    for l in log[:5] + log[-3:]:
        f = l.split(' ')
        print(f[0], f[1], f[2], 'score', f[6], 'lines', f[7], 'lvl', f[8], 'frozen', f[5])
