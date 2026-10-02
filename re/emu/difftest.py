"""Differential test: original WELLTRIS.EXE (emulated) vs core on random key scripts.

usage: difftest.py [n_runs] [first_seed]

Each run plays one game from game_init to game over (or an iteration cap) and compares the
complete game state after every pass of the play loop.
"""
import os, random, subprocess, sys, tempfile
from wtemu import Game, FRAME, TICK

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
TMP = os.environ.get('WT_TMP', tempfile.gettempdir())
REPLAY = os.path.join(TMP, 'wt_replay')

MOVE = [ord(c) for c in 'IiMmJjLl8246'] + [0x8048, 0x8050, 0x804b, 0x804d]
ROT = [ord('K'), ord('k'), ord('5')]
OTHER = [0x20, 0x8017, 0x8031, 0x8032, ord('x'), 0x8047]   # drop, Alt-I/N/M, unbound keys


def make_script(rng):
    hdr = dict(seed=rng.getrandbits(32), set=rng.randint(0, 2), level=rng.randint(0, 4),
               preview=rng.randint(0, 1), fixed=rng.choice([0, 0, 1]), clock=rng.randint(0, 1 << 30))
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


def run_original(hdr, keys, max_iter):
    g = Game()
    g.setup(hdr['seed'], hdr['set'], hdr['level'], bool(hdr['preview']), bool(hdr['fixed']), hdr['clock'])
    log = g.run(keys, max_iter)
    return log, g.exit_code


def run_port(hdr, keys, path, max_iter):
    with open(path, 'w') as fp:
        fp.write('%(seed)d %(set)d %(level)d %(preview)d %(fixed)d %(clock)d\n' % hdr)
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
    subprocess.run(['gcc', '-O2', '-I', os.path.join(ROOT, 'core'), '-o', REPLAY, os.path.join(ROOT, 'tests', 'replay.c'),
                    os.path.join(ROOT, 'core', 'wt_core.c'), os.path.join(ROOT, 'core', 'wt_tables.c')], check=True)
    bad = 0
    for run in range(first, first + n):
        rng = random.Random(run)
        hdr, keys = make_script(rng)
        max_iter = 20000
        orig, code = run_original(hdr, keys, max_iter)
        orig = [norm(l) for l in orig]
        port, end = run_port(hdr, keys, os.path.join(TMP, f'wt_script_{run}.txt'), max_iter)
        m = min(len(orig), len(port))
        diff = next((i for i in range(m) if orig[i] != port[i]), None)
        if diff is None and len(port) != len(orig): diff = m
        last = orig[-1].split(' ')
        status = 'OK ' if diff is None else 'BAD'
        print(f'{status} run {run}: {hdr} keys={len(keys)} iters={len(orig)}/{len(port)} exit={code} '
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
