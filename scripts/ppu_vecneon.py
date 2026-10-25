#!/usr/bin/env python3
"""T39: build-time rewrite of the lifted PPU AltiVec patterns into NEON.

The lifter emits each AltiVec instruction as a block that loads every operand
through ppu_vldf4/ppu_vldu4 (a byte swap per 32-bit lane: ctx->vr holds raw
big-endian bytes), loops over four lanes, and stores through ppu_vstf4/vstu4.
GCC turned one vmaddfp into ~18 instructions plus alias checks between the
vr[] operands. The same work is a vrev32 per operand, one or two NEON ops and
a vrev32 on the result.

Rewritten (exact text match only; anything else is left as lifted):
  vmaddfp  d = a*c + b      vnmsubfp d = b - a*c      (multiply, then add/sub:
  vaddfp   d = a + b        vsubfp   d = a - b         the lift is compiled with
  vslw  vsrw  vsraw  (shift by b & 31)                 -ffp-contract=off, so is
  vperm    d = concat{a,b}[c & 0x1F] on raw bytes      this, and lane results
                                                       are bit-identical)
All operands are loaded before the destination is stored, as in the lift.

usage: ppu_vecneon.py INPUT OUTPUT
"""
import re
import sys

R = r'(\d+)'
F3 = (r'\{ float a\[4\],b\[4\],c\[4\],d\[4\]; ppu_vldf4\(&ctx->vr\[' + R + r'\],a\); '
      r'ppu_vldf4\(&ctx->vr\[' + R + r'\],b\); ppu_vldf4\(&ctx->vr\[' + R + r'\],c\); '
      r'for\(int i=0;i<4;i\+\+\) d\[i\]=EXPR; ppu_vstf4\(&ctx->vr\[' + R + r'\],d\); \}')
F2 = (r'\{ float a\[4\],b\[4\],d\[4\]; ppu_vldf4\(&ctx->vr\[' + R + r'\],a\); '
      r'ppu_vldf4\(&ctx->vr\[' + R + r'\],b\); '
      r'for\(int i=0;i<4;i\+\+\) d\[i\]=EXPR; ppu_vstf4\(&ctx->vr\[' + R + r'\],d\); \}')
U2 = (r'\{ uint32_t a\[4\],b\[4\],d\[4\]; ppu_vldu4\(&ctx->vr\[' + R + r'\],a\); '
      r'ppu_vldu4\(&ctx->vr\[' + R + r'\],b\); '
      r'for\(int i=0;i<4;i\+\+\) d\[i\]=EXPR; ppu_vstu4\(&ctx->vr\[' + R + r'\],d\); \}')
VPERM = (r'\{ uint8_t\* a = \(uint8_t\*\)&ctx->vr\[' + R + r'\]; '
         r'uint8_t\* b = \(uint8_t\*\)&ctx->vr\[' + R + r'\]; '
         r'uint8_t\* c = \(uint8_t\*\)&ctx->vr\[' + R + r'\]; '
         r'uint8_t\* d = \(uint8_t\*\)&ctx->vr\[' + R + r'\]; uint8_t tmp\[16\]; '
         r'for \(int i = 0; i < 16; i\+\+\) \{ uint8_t sel = c\[i\] & 0x1F; '
         r'tmp\[i\] = \(sel < 16\) \? a\[sel\] : b\[sel - 16\]; \} memcpy\(d, tmp, 16\); \}')


def pat(template, expr):
    return re.compile(template.replace('EXPR', re.escape(expr)))


RULES = [
    (pat(F3, 'a[i]*c[i]+b[i]'),
     lambda m: f'{{ PVN_STF({m[4]}, vaddq_f32(vmulq_f32(PVN_LF({m[1]}), PVN_LF({m[3]})), PVN_LF({m[2]}))); }}'),
    (pat(F3, 'b[i]-a[i]*c[i]'),
     lambda m: f'{{ PVN_STF({m[4]}, vsubq_f32(PVN_LF({m[2]}), vmulq_f32(PVN_LF({m[1]}), PVN_LF({m[3]})))); }}'),
    (pat(F2, 'a[i]+b[i]'),
     lambda m: f'{{ PVN_STF({m[3]}, vaddq_f32(PVN_LF({m[1]}), PVN_LF({m[2]}))); }}'),
    (pat(F2, 'a[i]-b[i]'),
     lambda m: f'{{ PVN_STF({m[3]}, vsubq_f32(PVN_LF({m[1]}), PVN_LF({m[2]}))); }}'),
    (pat(U2, 'a[i]<<(b[i]&31u)'),
     lambda m: f'{{ PVN_STU({m[3]}, vshlq_u32(PVN_LU({m[1]}), PVN_SH({m[2]}))); }}'),
    (pat(U2, 'a[i]>>(b[i]&31u)'),
     lambda m: f'{{ PVN_STU({m[3]}, vshlq_u32(PVN_LU({m[1]}), vnegq_s32(PVN_SH({m[2]})))); }}'),
    (pat(U2, '(uint32_t)((int32_t)a[i]>>(b[i]&31u))'),
     lambda m: (f'{{ PVN_STU({m[3]}, vreinterpretq_u32_s32(vshlq_s32('
                f'vreinterpretq_s32_u32(PVN_LU({m[1]})), vnegq_s32(PVN_SH({m[2]}))))); }}')),
    (re.compile(VPERM),
     lambda m: (f'{{ uint8x16x2_t _t = {{{{ PVN_LB({m[1]}), PVN_LB({m[2]}) }}}}; '
                f'vst1q_u8((uint8_t*)&ctx->vr[{m[4]}], '
                f'vqtbl2q_u8(_t, vandq_u8(PVN_LB({m[3]}), vdupq_n_u8(0x1F)))); }}')),
]

PRELUDE = r'''
/* ppu_vecneon.py: AltiVec patterns in NEON (T39). ctx->vr holds raw big-endian
 * bytes; a float/word view swaps each 32-bit lane, as ppu_vldf4/vldu4 do. */
#include <arm_neon.h>
#define PVN_LB(n) vld1q_u8((const uint8_t*)&ctx->vr[n])
#define PVN_LU(n) vreinterpretq_u32_u8(vrev32q_u8(PVN_LB(n)))
#define PVN_LF(n) vreinterpretq_f32_u32(PVN_LU(n))
#define PVN_STU(n, v) vst1q_u8((uint8_t*)&ctx->vr[n], vrev32q_u8(vreinterpretq_u8_u32(v)))
#define PVN_STF(n, v) PVN_STU(n, vreinterpretq_u32_f32(v))
#define PVN_SH(n) vreinterpretq_s32_u32(vandq_u32(PVN_LU(n), vdupq_n_u32(31u)))
'''

FIRST_FUNC_RE = re.compile(r'^void func_[0-9A-Fa-f]{8}\(ppu_context\* ctx\) \{$', re.M)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding='utf-8', errors='surrogateescape').read()
    counts = []
    for rx, fn in RULES:
        text, n = rx.subn(fn, text)
        counts.append(n)
    m = FIRST_FUNC_RE.search(text)
    if m and sum(counts):
        text = text[:m.start()] + PRELUDE + text[m.start():]
    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write(text)
    print(f'ppu_vecneon: {sum(counts)} AltiVec blocks rewritten {counts} -> {dst}')


if __name__ == '__main__':
    main()
