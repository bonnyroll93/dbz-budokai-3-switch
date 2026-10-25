/*
 * DBZ3 -- CRI middleware error messages (A4, music silent).
 *
 * func_00216DB4 is CRI's error dispatcher: r3 = message string ("E2005081906 :
 * Failed in cellSurMixerSurBusAddData()" and the like, ~170 call sites); it
 * forwards to a user callback only when one is registered, so in this port the
 * messages were never seen. Made weak in its lifted TU (dbz3_weak_overrides.h)
 * and replaced here by the lifted body, copied verbatim
 * (dbz3_crierr_lifted.inc, from ppu_recomp_003.cpp), plus a log of each
 * distinct message with its caller and count.
 *
 * T27 log: sdmc:/switch/dbz-budokai-3-switch/t27-crierr.log.
 */

#define PPU_FAST_VM 1
#include "dbz3_crierr_lifted.inc"

#include <cstdio>
#include <cstring>

namespace {

constexpr int kMaxMsgs = 64;
struct Msg { uint32_t ea, lr; uint64_t count; };
Msg g_msgs[kMaxMsgs];
int g_nmsgs = 0;
FILE* g_log = nullptr;

void guest_str(uint32_t ea, char* out, int n)
{
    int i = 0;
    if (ea >= 0x10000u && ea < 0x80000000u) {
        for (; i < n - 1; ++i) {
            const char c = (char)vm_read8(ea + (uint32_t)i);
            if (!c) break;
            out[i] = (c >= 32 && c < 127) ? c : '?';
        }
    }
    out[i] = 0;
}

void log_msg(uint32_t ea, uint32_t lr)
{
    for (int i = 0; i < g_nmsgs; ++i) {
        if (g_msgs[i].ea == ea && g_msgs[i].lr == lr) {
            const uint64_t c = ++g_msgs[i].count;
            if (g_log && (c == 10 || c == 100 || c == 1000 || (c % 10000) == 0)) {
                char s[160];
                guest_str(ea, s, sizeof s);
                fprintf(g_log, "CRIERR x%llu lr=0x%08X \"%s\"\n", (unsigned long long)c, lr, s);
                fflush(g_log);
            }
            return;
        }
    }
    if (!g_log) g_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t27-crierr.log", "w");
    char s[160];
    guest_str(ea, s, sizeof s);
    if (g_log) {
        fprintf(g_log, "CRIERR first lr=0x%08X msg@0x%08X \"%s\"\n", lr, ea, s);
        fflush(g_log);
    }
    fprintf(stderr, "[cri] error: \"%s\" (lr=0x%08X)\n", s, lr);
    if (g_nmsgs < kMaxMsgs) g_msgs[g_nmsgs++] = Msg{ea, lr, 1};
}

} // namespace

void func_00216DB4(ppu_context* ctx)
{
    log_msg((uint32_t)ctx->gpr[3], (uint32_t)ctx->lr);
    lifted_00216DB4(ctx);
}
