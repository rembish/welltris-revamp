"""Decode WELLTRIS.EXE's images from your own original/*.bin and build upscaled assets.

usage: assets.py decode        # assets-local/raw/*.png at native resolution
       assets.py build         # assets-local/hd/*.png, upscaled 3x to 4:3

Everything goes to assets-local/, which is git-ignored: the art is Spectrum HoloByte's and is
never committed or published. Builds embed it only with -DWT_LOCAL_ASSETS=ON.

Formats (see re/NOTES.md):
- load_image (08ba): {u16 size, u16 0, u16 bytes_per_row, u16 rows} + PCX-style RLE
  (byte >= 0xc0: run of (byte & 0x3f) copies of the next byte). Unpacked: 4 planes one after
  another, plane p = colour bit p, default EGA palette.
"""
import os, struct, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
ORIG = os.path.join(ROOT, 'original')
OUT = os.path.join(ROOT, 'assets-local')

EGA = [0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
       0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff]
IMAGES = ['title', 'setup', 'scene1', 'scene2', 'scene3', 'scene4', 'scene5', 'well1', 'well2',
          'hiscore1', 'hiscore2', 'dialog', 'credits']


def unrle_pcx(d):
    out, i = bytearray(), 0
    while i < len(d):
        b = d[i]; i += 1
        if b >= 0xc0:
            out += bytes([d[i]]) * (b & 0x3f); i += 1
        else:
            out.append(b)
    return out


def load_image(name):
    d = open(os.path.join(ORIG, name + '.bin'), 'rb').read()
    size, _, w, h = struct.unpack('<4H', d[:8])
    u = unrle_pcx(d[8:8 + size])
    assert len(u) == 4 * w * h, name
    im = Image.new('P', (w * 8, h))
    im.putpalette(sum(([c >> 16, (c >> 8) & 255, c & 255] for c in EGA), []))
    px = bytearray(w * 8 * h)
    for p in range(4):
        plane = u[p * w * h:(p + 1) * w * h]
        for y in range(h):
            row = plane[y * w:(y + 1) * w]
            base = y * w * 8
            for xb, b in enumerate(row):
                if not b: continue
                for bit in range(8):
                    if b & (0x80 >> bit): px[base + xb * 8 + bit] |= 1 << p
    im.putdata(px)
    return im


SCALE = 3
ASPECT = 480 / 350               # EGA 640x350 is shown at 4:3: pixels are 1.37 times taller
# per image: 'vector' (line art: traced, then rasterised) or 'photo' (dithered pictures:
# de-dithered and upscaled)
STYLE = dict(title='photo', setup='photo', scene1='photo', scene2='photo', scene3='photo', scene4='photo',
             scene5='photo', hiscore1='photo', hiscore2='photo', well1='vector', well2='vector',
             dialog='vector', credits='vector')


def hd_size(im):
    return im.width * SCALE, int(round(im.height * SCALE * ASPECT))


def upscale_photo(im):
    import cv2, numpy as np
    a = np.array(im.convert('RGB'))[:, :, ::-1].astype(np.float32)
    w, h = hd_size(im)
    a = cv2.GaussianBlur(a, (0, 0), 0.75)                 # dissolve the 1-pixel dither patterns
    a = cv2.resize(a, (w, h), interpolation=cv2.INTER_CUBIC)
    for _ in range(3): a = cv2.bilateralFilter(a, 7, 25, 5)   # flatten what is left, keep edges
    a = cv2.addWeighted(a, 2.0, cv2.GaussianBlur(a, (0, 0), 2.5), -1.0, 0)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)[:, :, ::-1])


def upscale_vector(im, tmp):
    import io, resvg_py, vtracer
    src, svg = os.path.join(tmp, 'trace.png'), os.path.join(tmp, 'trace.svg')
    im.convert('RGBA').resize((im.width * 2, int(round(im.height * 2 * ASPECT))), Image.NEAREST).save(src)
    # positional: this vtracer build crashes on keyword arguments
    vtracer.convert_image_to_svg_py(src, svg, 'color', 'stacked', 'spline', 2, 8, 6, 60, 4.0, 10, 45, 3)
    w, h = hd_size(im)
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_path=svg, width=w, height=h)))).convert('RGB')


def build():
    hd = os.path.join(OUT, 'hd')
    os.makedirs(hd, exist_ok=True)
    for n in IMAGES:
        im = load_image(n)
        out = upscale_photo(im) if STYLE[n] == 'photo' else upscale_vector(im, hd)
        out.save(os.path.join(hd, n + '.png'), optimize=True)
        print(n, STYLE[n], out.size)
    for f in ('trace.png', 'trace.svg'): os.remove(os.path.join(hd, f))


def decode():
    os.makedirs(os.path.join(OUT, 'raw'), exist_ok=True)
    for n in IMAGES:
        im = load_image(n)
        im.save(os.path.join(OUT, 'raw', n + '.png'))
        print(n, im.size)


if __name__ == '__main__':
    {'decode': decode, 'build': build}[sys.argv[1] if len(sys.argv) > 1 else 'decode']()
