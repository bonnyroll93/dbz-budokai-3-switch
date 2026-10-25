#!/usr/bin/env python3
"""T40 liftfast: build-time copy of a lifted PPU source with less per-call and
per-access overhead (Switch). The checked-in lift is untouched.

1. Drops DRAIN_TRAMPOLINE(ctx) after calls that cannot leave a trampoline set.
   Only lifted code sets g_trampoline_fn, always as
   `{ g_trampoline_fn = F; return; }` in the function's own body, and every
   call site drains right after the call. So a function returns with the
   trampoline set only if its OWN body contains such a set; after a call to
   any other function the drain is a no-op -- but on the Switch (-mtp=soft)
   each drain reads a thread-local, which is a call to __aarch64_read_tp.
   ps3_indirect_call drains internally, so drains after it are dropped too.
   Drains after calls to functions that do set a trampoline are kept.

2. Includes dbz3_liftfast.h right after ppu_recomp.h: guest memory accessors
   with one range test and boot-constant globals the compiler can keep in
   registers (see that header).

usage: ppu_liftfast.py INPUT OUTPUT ORIGINAL_SOURCE...
  ORIGINAL_SOURCE: every lifted .cpp (to find the functions that set a
  trampoline, wherever they live).
"""
import re
import sys

FUNC_RE = re.compile(r'^void func_([0-9A-Fa-f]{8})\(ppu_context\* ctx\) \{')
SET_TOKEN = 'g_trampoline_fn = ('
DIRECT_RE = re.compile(r'func_([0-9A-Fa-f]{8})\(ctx\); DRAIN_TRAMPOLINE\(ctx\);')
INDIRECT_RE = re.compile(r'ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);')
INCLUDE = '#include "ppu_recomp.h"'


def setters(paths):
    found = set()
    for p in paths:
        cur = None
        with open(p, encoding='utf-8', errors='surrogateescape') as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    cur = m.group(1).upper()
                elif cur and SET_TOKEN in line:
                    found.add(cur)
    return found


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, dst, originals = sys.argv[1], sys.argv[2], sys.argv[3:]
    keep = setters(originals)
    text = open(src, encoding='utf-8', errors='surrogateescape').read()

    stats = {'kept': 0, 'dropped': 0, 'indirect': 0}

    def direct(m):
        if m.group(1).upper() in keep:
            stats['kept'] += 1
            return m.group(0)
        stats['dropped'] += 1
        return 'func_%s(ctx);' % m.group(1)

    text = DIRECT_RE.sub(direct, text)
    text, stats['indirect'] = INDIRECT_RE.subn('ps3_indirect_call(ctx);', text)

    if INCLUDE in text:
        text = text.replace(INCLUDE, INCLUDE + '\n#include "dbz3_liftfast.h"', 1)
    else:
        sys.exit('ppu_liftfast.py: %s does not include ppu_recomp.h' % src)

    with open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n') as f:
        f.write(text)
    print('ppu_liftfast: %s drains dropped=%d kept=%d indirect_dropped=%d (setters=%d)'
          % (src.replace('\\', '/').split('/')[-1], stats['dropped'], stats['kept'],
             stats['indirect'], len(keep)))


if __name__ == '__main__':
    main()
