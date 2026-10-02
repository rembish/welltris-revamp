"""Differential test: original WELLTRIS.EXE (emulated) vs core on key scripts.

usage: difftest.py [n_runs] [first_seed]

Odd runs are played by a search bot (tests/botgen.c) that clears lines, even runs are random
keys with some prefilled floor lines. Each run plays one game from game_init to game over (or an iteration cap) and compares the
complete game state after every pass of the play loop.
"""
import os, random, subprocess, sys, tempfile
from wtemu import Game, FRAME, TICK

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
TMP = os.environ.get('WT_TMP', tempfile.gettempdir())
REPLAY = os.path.join(TMP, 'wt_replay')
BOTGEN = os.path.join(TMP, 'wt_botgen')

MOVE = [ord(c) for c in 'IiMmJjLl8246'] + [0x8048, 0x8050, 0x804b, 0x804d]
ROT = [ord('K'), ord('k'), ord('5')]
OTHER = [0x20, 0x8017, 0x8031, 0x8032, ord('x'), 0x8047]   # drop, Alt-I/N/M, unbound keys


def make_script(rng):
    hdr = dict(seed=rng.getrandbits(32), set=rng.randint(0, 2), level=rng.randint(0, 4),
               preview=rng.randint(0, 1), fixed=rng.choice([0, 0, 1]), clock=rng.randint(0, 1 << 30))
    fill = []
    if rng.random() < 0.5:                   # nearly full floor lines: forces line clears
        for _ in range(rng.randint(1, 4)):
            line, horiz = rng.randint(0, 7), rng.random() < 0.5
            holes = set(rng.sample(range(8), rng.randint(1, 2)))
            fill += [((line, k) if horiz else (k, line)) + (rng.randint(1, 15),) for k in range(8) if k not in holes]
    hdr['fill'] = fill
    keys, t = [], hdr['clock']
    rate = rng.choice([2, 8, 30])            # keys per second, on average
    drop = rng.choice([0.02, 0.1, 0.3])
    for _ in range(rng.randint(300, 3000)):
        t += int(rng.expovariate(rate) * 1193182) + 1
        r = rng.random()
        k = (0x20 if r < drop else rng.choice(ROT) if r < 0.35 else rng.choice(OTHER) if r < 0.4
             else rng.choice(MOVE))
        keys.append((t, k))
        if rng.random() < 0.05:              # bursts: several keys in one frame
            for _ in range(rng.randint(1, 20)):
                keys.append((t, rng.choice(MOVE + ROT)))
    return hdr, keys


def bot_script(rng):
    """A script from tests/botgen: a search bot that clears lines."""
    args = [rng.getrandbits(32), rng.randint(0, 2), rng.choice([0, 0, 1, 2, 3, 4]), rng.randint(0, 1),
            rng.choice([0, 0, 1]), rng.randint(0, 1 << 30), rng.randint(40, 400), rng.choice([0, 5, 20]),
            rng.getrandbits(31), int(rng.random() < 0.3)]
    out = subprocess.run([BOTGEN] + [str(a) for a in args], capture_output=True, text=True, check=True).stdout
    lines = out.strip().split('\n')
    h = list(map(int, lines[0].split()))
    hdr = dict(zip(['seed', 'set', 'level', 'preview', 'fixed', 'clock'], h))
    hdr['fill'] = [tuple(h[7 + 3 * i: 10 + 3 * i]) for i in range(h[6])]
    keys = [(int(a), int(b, 16)) for a, b in (l.split() for l in lines[1:])]
    return hdr, keys


def run_original(hdr, keys, max_iter):
    g = Game()
    g.setup(hdr['seed'], hdr['set'], hdr['level'], bool(hdr['preview']), bool(hdr['fixed']), hdr['clock'], hdr['fill'])
    log = g.run(keys, max_iter)
    return log, g.exit_code


def run_port(hdr, keys, path, max_iter):
    with open(path, 'w') as fp:
        fp.write('%(seed)d %(set)d %(level)d %(preview)d %(fixed)d %(clock)d ' % hdr)
        fp.write('%d %s\n' % (len(hdr['fill']), ' '.join('%d %d %d' % c for c in hdr['fill'])))
        for t, k in keys: fp.write('%d %x\n' % (t, k))
    out = subprocess.run([REPLAY, path, str(max_iter)], capture_output=True, text=True, check=True).stdout.split('\n')
    return [l for l in out if l and not l.startswith('end')], [l for l in out if l.startswith('end')]


def norm(line):
    """Normalise the original's piece structs: bytes after the code terminator and the pad
    byte are uninitialised stack garbage there."""
    f = line.split(' ')
    def piece(h):
        b = bytearray.fromhex(h)
        end = next(i for i in range(3, 0x12) if b[i] == 0)
        for i in range(end + 1, 0x12): b[i] = 0
        b[0x13] = 0
        return b.hex()
    f[2] = piece(f[2])
    if len(f) > 18 and f[18]:
        f[18] = ';'.join(s.split(':')[0] + ':' + piece(s.split(':')[1]) for s in f[18].split(';'))
    return ' '.join(f)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    cdir = os.environ.get('WT_CORE', os.path.join(ROOT, 'core'))   # mutation checks use a modified copy
    core = [os.path.join(cdir, 'wt_core.c'), os.path.join(cdir, 'wt_tables.c')]
    for exe, src, c in ((REPLAY, 'replay.c', cdir), (BOTGEN, 'botgen.c', os.path.join(ROOT, 'core'))):
        subprocess.run(['gcc', '-O2', '-I', c, '-o', exe, os.path.join(ROOT, 'tests', src),
                        os.path.join(c, 'wt_core.c'), os.path.join(c, 'wt_tables.c')], check=True)
    bad = 0
    for run in range(first, first + n):
        rng = random.Random(run)
        hdr, keys = bot_script(rng) if run % 2 else make_script(rng)
        max_iter = 20000
        orig, code = run_original(hdr, keys, max_iter)
        orig = [norm(l) for l in orig]
        port, end = run_port(hdr, keys, os.path.join(TMP, f'wt_script_{run}.txt'), max_iter)
        m = min(len(orig), len(port))
        diff = next((i for i in range(m) if orig[i] != port[i]), None)
        if diff is None and len(port) != len(orig): diff = m
        last = orig[-1].split(' ')
        status = 'OK ' if diff is None else 'BAD'
        h = {k: v for k, v in hdr.items() if k != 'fill'}
        print(f'{status} run {run}: {h} fill={len(hdr["fill"])} keys={len(keys)} iters={len(orig)}/{len(port)} exit={code} '
              f'score={last[6]} lines={last[7]} level={last[8]} {end[0] if end else ""}', flush=True)
        if diff is not None:
            bad += 1
            names = ['clock', 'over', 'piece', 'wall', 'floor', 'frozen', 'score', 'lines', 'level', 'ltl',
                     'fall_delay', 'fall_dl', 'drop_dl', 'flags', 'cur_wall', 'drop_rows', 'next', 'rng', 'stored']
            for i in range(max(0, diff - 2), min(diff + 1, m)):
                a, b = orig[i].split(' '), port[i].split(' ')
                print(f'   iter {i}:', ' '.join(f'{names[k]}: {a[k]} != {b[k]}' for k in range(len(a)) if k < len(b) and a[k] != b[k]) or 'same')
            if diff >= m: print('   lengths differ')
    print(f'{n - bad}/{n} matched')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
