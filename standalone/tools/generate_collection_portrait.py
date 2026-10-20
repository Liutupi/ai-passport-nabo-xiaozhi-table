"""Convert the existing NABO idle frame, without changing its pose or identity.

Input is one unmodified palette/row-index/RLE frame from the public NABO pack.
Remove only the black matte connected to the image boundary, then composite
onto the launcher's navy. Interior dark hair and glasses are preserved.
Requires Pillow only for build-time downsampling. Firmware reads constant RGB565.
"""
import json
import struct
from collections import deque
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def generate():
    info = json.loads((ROOT/'art/nabo-white-coat-idle.json').read_text('utf-8'))
    raw = (ROOT/'art/nabo-white-coat-idle.rle').read_bytes()
    palette = struct.unpack_from('<256H', raw)
    offsets = struct.unpack_from('<225I', raw, 512)
    image = Image.new('RGBA', (224, 224))
    for y in range(224):
        pos, end = offsets[y], offsets[y + 1]
        indices = []
        while pos < end:
            token = raw[1412 + pos]; pos += 1
            repeat = bool(token & 128)
            count = (token & 127) + (2 if repeat else 1)
            indices.extend([raw[1412 + pos]] * count if repeat else raw[1412 + pos:1412 + pos + count])
            pos += 1 if repeat else count
        assert len(indices) == 224 and pos == end
        for x, index in enumerate(indices):
            value = palette[index]
            image.putpixel((x, y), ((value >> 11)*255//31, ((value >> 5)&63)*255//63,
                                    (value & 31)*255//31, 255))
    edge = deque([(x, y) for x in range(224) for y in (0, 223)] +
                 [(x, y) for y in range(224) for x in (0, 223)])
    seen = set()
    while edge:
        x, y = edge.popleft()
        if (x, y) in seen or not (0 <= x < 224 and 0 <= y < 224):
            continue
        seen.add((x, y))
        if image.getpixel((x, y))[:3] != (0, 0, 0):
            continue
        image.putpixel((x, y), (0, 0, 0, 0))
        edge.extend(((x-1, y), (x+1, y), (x, y-1), (x, y+1)))
    image = image.resize((84, 84), Image.Resampling.LANCZOS)
    # INK = 0x1927, expanded exactly to 8-bit RGB before composition.
    ink = ((0x1927 >> 11)*255//31, ((0x1927 >> 5)&63)*255//63, (0x1927&31)*255//31)
    canvas = Image.new('RGBA', image.size, ink + (255,))
    canvas.alpha_composite(image)
    pixels = [(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3) for r, g, b, a in canvas.get_flattened_data()]
    lines = ['// Generated from the established NABO white-coat idle pose; art is not MIT licensed.',
             '#include "collection_portrait.h"', 'const uint16_t collection_portrait[] = {']
    lines += ['    ' + ','.join(f'0x{v:04x}' for v in pixels[i:i+16]) + ',' for i in range(0, len(pixels), 16)]
    lines += ['};', '']
    (ROOT/'firmware/main/collection_portrait.c').write_text('\n'.join(lines), 'utf-8')
    folder = ROOT/'private/previews'; folder.mkdir(parents=True, exist_ok=True)
    canvas.convert('RGB').save(folder/'white-coat-portrait.png')
    print('Converted unchanged source pose', info['source_frame'], 'to 84x84 RGB565')


if __name__ == '__main__':
    generate()
