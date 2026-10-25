"""T28 (V1): decode the textures dumped by the skin-texdump build
(sdmc:/switch/dbz-budokai-3-switch/t28-tex/u<unit>_<off>_f<fmt>_<w>x<h>_p<pitch>.bin)
into PNGs next to them, plus an enlarged copy and the colour/alpha split.
Usage: python scripts/t28_decode.py build/logs/dbz3/dev/runNN/t28-tex"""
import re, struct, sys, pathlib
from PIL import Image

def c565(c):
    return ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)

def color_block(b, opaque4):
    c0, c1, idx = struct.unpack('<HHI', b)
    p0, p1 = c565(c0), c565(c1)
    if c0 > c1 or opaque4:
        pal = [p0, p1, tuple((2*a+b)//3 for a, b in zip(p0, p1)), tuple((a+2*b)//3 for a, b in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a+b)//2 for a, b in zip(p0, p1)), (0, 0, 0)]
    return [pal[(idx >> (2*i)) & 3] for i in range(16)]

def decode(data, fmt, w, h):
    base = fmt & ~0x60
    bw, bh = (w + 3)//4, (h + 3)//4
    bs = 8 if base == 0x86 else 16
    img = Image.new('RGBA', (bw*4, bh*4))
    px = img.load()
    for by in range(bh):
        for bx in range(bw):
            o = (by*bw + bx) * bs
            blk = data[o:o+bs]
            if len(blk) < bs: continue
            if base == 0x86:
                cols, al = color_block(blk, False), [255]*16
            elif base == 0x87:
                a = int.from_bytes(blk[:8], 'little')
                al = [((a >> (4*i)) & 15) * 17 for i in range(16)]
                cols = color_block(blk[8:], True)
            else:
                a0, a1 = blk[0], blk[1]
                ai = int.from_bytes(blk[2:8], 'little')
                if a0 > a1: pal = [a0, a1] + [((6-i)*a0 + (i+1)*a1)//7 for i in range(6)]
                else: pal = [a0, a1] + [((4-i)*a0 + (i+1)*a1)//5 for i in range(4)] + [0, 255]
                al = [pal[(ai >> (3*i)) & 7] for i in range(16)]
                cols = color_block(blk[8:], True)
            for i in range(16):
                px[bx*4 + i % 4, by*4 + i//4] = cols[i] + (al[i],)
    return img.crop((0, 0, w, h))

def main(d):
    d = pathlib.Path(d)
    for f in sorted(d.glob('u*_f*.bin')):
        m = re.match(r'u(\d)_([0-9A-F]{8})_f([0-9A-F]{2})_(\d+)x(\d+)_p(\d+)\.bin', f.name)
        if not m: continue
        fmt, w, h = int(m[3], 16), int(m[4]), int(m[5])
        if (fmt & ~0x60) not in (0x86, 0x87, 0x88):
            print(f.name, 'format not decoded'); continue
        img = decode(f.read_bytes(), fmt, w, h)
        img.save(f.with_suffix('.png'))
        rgb = img.convert('RGB'); a = img.getchannel('A')
        sheet = Image.new('RGB', (w*2, h)); sheet.paste(rgb, (0, 0)); sheet.paste(a.convert('RGB'), (w, 0))
        scale = max(1, 256 // max(w, h))
        sheet.resize((w*2*scale, h*scale), Image.NEAREST).save(f.with_name(f.stem + '_rgb_alpha.png'))
        print(f.name, '->', f.with_suffix('.png').name)

if __name__ == '__main__':
    main(sys.argv[1])
