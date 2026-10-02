"""Mutation check for the differential test: break the core in small ways and make sure
difftest.py notices each one.

usage: mutants.py [runs_per_mutant] [description substring]
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.join(HERE, '..', '..', 'core')

# (description, original text, replacement): each must occur exactly once in wt_core.c
# difftest seeds that reach rare paths (227: an empty-floor bonus, 36: a rotation across walls on the floor)
REGRESS = [227, 36]

MUTANTS = [
    ('drop bonus rows not counted', 'if (g->drop_rows) g->drop_rows--;', ';'),
    ('settling uses the normal floor limit', 'g->floor_limit = -4;', 'g->floor_limit = -8;'),
    ('empty floor bonus 499', 's += 500;', 's += 499;'),
    ('line clear pause one tick short', '    delay(g, 0x38);\n    i = collapse(g, rows, cols);', '    delay(g, 0x37);\n    i = collapse(g, rows, cols);'),
    ('upper half collapses one line less', 'for (i = 7; i > 3; i--) {\n            if (!g->row_full[i]) continue;', 'for (i = 7; i > 4; i--) {\n            if (!g->row_full[i]) continue;'),
    ('walls thaw after 4 pieces', '++g->frozen_count[i] > 3', '++g->frozen_count[i] > 4'),
    ('rotation tries once', 'rotate(g, &g->piece, key, 0)', 'rotate(g, &g->piece, 1, 0)'),
    ('no lock on the floor', 'if (g->piece.row < 0) g->lock_lr = 0xff;', ';'),
    ('preview free', 'if (g->preview_on) s = s < 5 ? 0 : s - 5;', ';'),
    ('one key per pass not enforced', '    if (*event == WT_EV_NONE) flush_keys(g);\n    return act;', '    return act;'),
    ('no vsync after a pass', '        vsync(g);\n        if (g->level_up)', '        if (g->level_up)'),
    ('spawn column off by one', 'p->col = (int16_t)(a + wt_rand(g) % 3 + 3);', 'p->col = (int16_t)(a + wt_rand(g) % 3 + 2);'),
    ('stored cells not marked', 'if (!skip && p->row < 0) p->codes[idx] |= 0x80;', ';'),
    ('level bonus tune shorter', 'sweep(g, 1, 10, 0xf); /* 42d2 */', 'sweep(g, 1, 9, 0xf); /* 42d2 */'),
    ('wall cross ignored', 'if (cross && below) g->wall_cross = 0xff;', ';'),
]


def main():
    runs = sys.argv[1] if len(sys.argv) > 1 else '12'
    src = open(os.path.join(CORE, 'wt_core.c')).read()
    survived = []
    only = sys.argv[2] if len(sys.argv) > 2 else ''
    for desc, a, b in MUTANTS:
        assert src.count(a) == 1, desc
        if only not in desc: continue
        d = tempfile.mkdtemp()
        for f in os.listdir(CORE): shutil.copy(os.path.join(CORE, f), d)
        open(os.path.join(d, 'wt_core.c'), 'w').write(src.replace(a, b))
        caught = 0
        for n, first in [(runs, '1')] + [('1', str(k)) for k in REGRESS]:
            r = subprocess.run([sys.executable, os.path.join(HERE, 'difftest.py'), n, first], capture_output=True,
                               text=True, env=dict(os.environ, WT_CORE=d))
            caught += r.stdout.count('BAD')
        print(f'{"caught  " if caught else "SURVIVED"} {caught:3d} runs  {desc}', flush=True)
        if not caught: survived.append(desc)
        shutil.rmtree(d)
    print(f'{len(MUTANTS) - len(survived)}/{len(MUTANTS)} mutants caught')
    sys.exit(1 if survived else 0)


if __name__ == '__main__':
    main()
