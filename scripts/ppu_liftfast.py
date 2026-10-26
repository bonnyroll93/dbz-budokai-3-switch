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
SORT_SCAN_START = 'loc_0005ADF4:\n'
SORT_SCAN_END = '        if (((ctx->ctr = (uint32_t)(ctx->ctr - 1)) != 0)) goto loc_0005ADF4;\n'
DU_START = 'void func_001DBC58(ppu_context* ctx) {\n'
DU_TAIL = 'loc_001DE4F4:\n'
DU_TAIL_MID = '        ctx->lr = 0x001DE598; func_00019818(ctx);'
DU_END = 'loc_001DE61C:\n'


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

    # The draw-list selection sort in func_0005AD60 compares only one key per
    # record. Keep its outer loop and swaps; replace the vector-heavy scan.
    if 'void func_0005AD60(ppu_context* ctx) {' in text:
        start = text.find(SORT_SCAN_START, text.find('void func_0005AD60(ppu_context* ctx) {'))
        end = text.find(SORT_SCAN_END, start)
        if start < 0 or end < 0 or end - start > 10000:
            sys.exit('ppu_liftfast.py: func_0005AD60 sort scan changed')
        end += len(SORT_SCAN_END)
        text = text[:start] + '        dbz3_sort_scan(ctx);\n' + text[end:]
        # Enter after the original prologue has saved nonvolatile registers.
        # The draw continuation initializes its arguments and loop registers;
        # none of the sort's volatile temporaries are live at that boundary.
        begin = text.index('void func_0005AD60(ppu_context* ctx) {')
        anchor = '        ctx->gpr[3] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[3], 3, 0, 28);'
        entry = text.find(anchor, begin)
        if entry < 0 or entry > start:
            sys.exit('ppu_liftfast.py: draw-list sort entry changed')
        text = (text[:entry] +
                '        if (dbz3_sort_draw_list()) goto loc_0005AE70;\n' +
                text[entry:])
        stats['sort_scan'] = 1
    else:
        stats['sort_scan'] = 0

    # DU task phase samples: only a build-time copy is changed. Anchor every
    # probe to its guest address and fail if the lift changes, so this cannot
    # silently instrument a different function or call site.
    begin = text.find(DU_START)
    if begin >= 0:
        finish = text.find('\nvoid func_', begin + len(DU_START))
        if finish < 0:
            sys.exit('ppu_liftfast.py: func_001DBC58 end missing')
        du = text[begin:finish]
        for anchor in (DU_START, DU_TAIL, DU_TAIL_MID, DU_END):
            if du.count(anchor) != 1:
                sys.exit('ppu_liftfast.py: DU phase anchor changed: ' + anchor.strip())
        du = du.replace(DU_START, DU_START + '        dbz3_du_phase_probe(0);\n', 1)
        du = du.replace(DU_TAIL, DU_TAIL +
                        '        dbz3_du_phase_probe(1);\n'
                        '        if (dbz3_du_skip_render()) goto loc_001DE61C;\n', 1)
        mid = DU_TAIL_MID + ' DRAIN_TRAMPOLINE(ctx);'
        if du.count(mid) != 1:
            mid = DU_TAIL_MID
        du = du.replace(mid, mid + '\n        dbz3_du_phase_probe(2);', 1)
        du = du.replace(DU_END, DU_END + '        dbz3_du_phase_probe(3);\n', 1)
        text = text[:begin] + du + text[finish:]

    if INCLUDE in text:
        text = text.replace(INCLUDE, INCLUDE + '\n#include "dbz3_liftfast.h"\nextern "C" void dbz3_du_phase_probe(uint32_t);\nextern "C" int dbz3_du_skip_render(void);', 1)
    else:
        sys.exit('ppu_liftfast.py: %s does not include ppu_recomp.h' % src)

    with open(dst, 'w', encoding='utf-8', errors='surrogateescape', newline='\n') as f:
        f.write(text)
    print('ppu_liftfast: %s drains dropped=%d kept=%d indirect_dropped=%d sort_scan=%d (setters=%d)'
          % (src.replace('\\', '/').split('/')[-1], stats['dropped'], stats['kept'],
             stats['indirect'], stats['sort_scan'], len(keep)))


if __name__ == '__main__':
    main()
