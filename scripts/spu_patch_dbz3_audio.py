#!/usr/bin/env python3
"""DBZ3 bnusCore audio SPU (spu_0001_at_002E2580): optional reverb bypass.

The bnusCore SPU spends most of its time in a partitioned convolution reverb
(94 impulse-response partitions of 8 KB, frequency-domain accumulators of
6 KB, an FFT per block): the function group 0x8148..0x9970, entered only from
0x811C and left only to 0x8C48. 0x811C already skips it when the partition
count it reads is <= 0 -- the engine's own "no reverb" path. This rewrite
makes that branch also taken while the runtime flag g_bnus_reverb_skip is set
(ports/dbz3/stubs.cpp), so the SPU produces the dry mix in time instead of
the full mix at a quarter of the rate.

usage: spu_patch_dbz3_audio.py INPUT OUTPUT HEADER_DIR
"""
import re
import sys

FUNC = 'void spu_0001_at_002E2580_spu_func_0000811C(spu_context* ctx) {'
OLD = 'if (ctx->gpr[41]._u32[0] == 0) { ctx->pc = 0x8C48;'
NEW = 'if (g_bnus_reverb_skip || ctx->gpr[41]._u32[0] == 0) { ctx->pc = 0x8C48;'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    header_dir = sys.argv[3].replace(chr(92), '/')
    text = open(src, encoding='utf-8', errors='surrogateescape').read()
    start = text.find(FUNC)
    if start < 0:
        raise SystemExit('spu_patch_dbz3_audio: function 0x811C not found')
    end = text.find('\n}', start)
    body = text[start:end]
    if body.count(OLD) != 1:
        raise SystemExit('spu_patch_dbz3_audio: reverb branch not found exactly once')
    text = text[:start] + body.replace(OLD, NEW) + text[end:]
    # The build copy lives elsewhere: name the image's header by full path.
    text = re.sub(r'^#include ".*spu_recomp\.h"', f'#include "{header_dir}/spu_recomp.h"', text, count=1, flags=re.M)
    # Declaration right before the patched function (C file, file scope).
    text = text.replace(FUNC, 'extern int g_bnus_reverb_skip;   /* ports/dbz3/stubs.cpp */\n' + FUNC, 1)
    open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write(text)
    print(f'spu_patch_dbz3_audio: reverb bypass branch added -> {dst}')


if __name__ == '__main__':
    main()
