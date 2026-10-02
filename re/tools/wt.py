"""Helpers for poking at WELLTRIS.EXE: image/DS offsets, strings, disassembly, xrefs."""
import struct, sys, os, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '..', 'original', 'welltris.exe')
raw = open(EXE, 'rb').read()
HDR = struct.unpack('<H', raw[8:10])[0] * 16
img = bytearray(raw[HDR:])
DSEG = 0x0a39            # load-relative data segment (Ghidra shows it as 1a39)
DSBASE = DSEG * 16
CODE_END = 0x79c8        # Turbo C startup (entry) and RTL follow


def ds(off, n=1):
    return bytes(img[DSBASE + off: DSBASE + off + n])

def w(off):  return struct.unpack('<h', ds(off, 2))[0]
def uw(off): return struct.unpack('<H', ds(off, 2))[0]

def cstr(off):
    b = img[DSBASE + off:]
    return b[:b.index(0)].decode('latin1')

def dis(start, end):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    yield from md.disasm(bytes(img[start:end]), start)

def strings(minlen=3):
    out, cur, st = [], b'', None
    for o in range(0, len(img) - DSBASE):
        c = img[DSBASE + o]
        if 32 <= c < 127 or c in (9, 10, 13):
            if st is None: st = o
            cur += bytes([c])
        else:
            if c == 0 and st is not None and len(cur) >= minlen: out.append((st, cur.decode('latin1')))
            cur, st = b'', None
    return out

def imm_refs(val):
    """Code addresses whose instruction mentions the 16-bit value (immediate or displacement)."""
    pat = struct.pack('<H', val)
    i = img.find(pat, 0, DSBASE)
    while 0 <= i < DSBASE:
        yield i
        i = img.find(pat, i + 1, DSBASE)

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'strings':
        for o, s in strings(): print(f'{o:04x} {s!r}')
    elif cmd == 's':
        for a in sys.argv[2:]: print(a, repr(cstr(int(a, 16))))
    elif cmd == 'refs':
        for a in sys.argv[2:]:
            print(a, ' '.join(f'{r:04x}' for r in imm_refs(int(a, 16))))
    elif cmd == 'dis':
        for i in dis(int(sys.argv[2], 16), int(sys.argv[3], 16)):
            print(f'{i.address:04x}: {i.mnemonic} {i.op_str}')
    elif cmd == 'hex':
        o, n = int(sys.argv[2], 16), int(sys.argv[3], 16)
        b = ds(o, n)
        for k in range(0, n, 16):
            print(f'{o+k:04x}: ' + ' '.join(f'{x:02x}' for x in b[k:k+16]))
