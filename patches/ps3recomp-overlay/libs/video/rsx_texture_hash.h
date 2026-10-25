/*
 * XXH64, seed zero, specialized for the draw engine's texture mutation check.
 * Adapted from xxHash v0.8.3, https://github.com/Cyan4973/xxHash
 * Copyright (C) 2012-2023 Yann Collet
 *
 * BSD 2-Clause License
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PS3RECOMP_RSX_TEXTURE_HASH_H
#define PS3RECOMP_RSX_TEXTURE_HASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define RSX_TH_P1 UINT64_C(11400714785074694791)
#define RSX_TH_P2 UINT64_C(14029467366897019727)
#define RSX_TH_P3 UINT64_C(1609587929392839161)
#define RSX_TH_P4 UINT64_C(9650029242287828579)
#define RSX_TH_P5 UINT64_C(2870177450012600261)

static inline uint64_t rsx_th_rotl(uint64_t v, unsigned n)
{
    return (v << n) | (v >> (64u - n));
}

/* memcpy permits unaligned inputs without aliasing or end-of-span overreads.
 * Guest byte order is immaterial; XXH64 always reads its words little endian. */
static inline uint64_t rsx_th_read64(const unsigned char* p)
{
    uint64_t v;
    memcpy(&v, p, sizeof v);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

static inline uint32_t rsx_th_read32(const unsigned char* p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

static inline uint64_t rsx_th_round(uint64_t acc, uint64_t word)
{
    return rsx_th_rotl(acc + word * RSX_TH_P2, 31) * RSX_TH_P1;
}

/* An ephemeral content fingerprint, never a persisted shader/pipeline key.
 * Four independent accumulators remove the old word-at-a-time FNV dependency
 * chain. Every byte, including the tail, is still checked on every due frame. */
static inline uint64_t rsx_texture_hash(const void* bytes, size_t size)
{
    const unsigned char* p = (const unsigned char*)bytes;
    size_t left = size;
    uint64_t h;
    if (left >= 32) {
        uint64_t a = RSX_TH_P1 + RSX_TH_P2, b = RSX_TH_P2;
        uint64_t c = 0, d = UINT64_C(0) - RSX_TH_P1;
        do {
            a = rsx_th_round(a, rsx_th_read64(p));
            b = rsx_th_round(b, rsx_th_read64(p + 8));
            c = rsx_th_round(c, rsx_th_read64(p + 16));
            d = rsx_th_round(d, rsx_th_read64(p + 24));
            p += 32;
            left -= 32;
        } while (left >= 32);
        h = rsx_th_rotl(a, 1) + rsx_th_rotl(b, 7) +
            rsx_th_rotl(c, 12) + rsx_th_rotl(d, 18);
        h = (h ^ rsx_th_round(0, a)) * RSX_TH_P1 + RSX_TH_P4;
        h = (h ^ rsx_th_round(0, b)) * RSX_TH_P1 + RSX_TH_P4;
        h = (h ^ rsx_th_round(0, c)) * RSX_TH_P1 + RSX_TH_P4;
        h = (h ^ rsx_th_round(0, d)) * RSX_TH_P1 + RSX_TH_P4;
    } else {
        h = RSX_TH_P5;
    }
    h += (uint64_t)size;
    while (left >= 8) {
        h ^= rsx_th_round(0, rsx_th_read64(p));
        h = rsx_th_rotl(h, 27) * RSX_TH_P1 + RSX_TH_P4;
        p += 8;
        left -= 8;
    }
    if (left >= 4) {
        h ^= (uint64_t)rsx_th_read32(p) * RSX_TH_P1;
        h = rsx_th_rotl(h, 23) * RSX_TH_P2 + RSX_TH_P3;
        p += 4;
        left -= 4;
    }
    while (left--) {
        h ^= (uint64_t)*p++ * RSX_TH_P5;
        h = rsx_th_rotl(h, 11) * RSX_TH_P1;
    }
    h ^= h >> 33;
    h *= RSX_TH_P2;
    h ^= h >> 29;
    h *= RSX_TH_P3;
    return h ^ (h >> 32);
}

#undef RSX_TH_P1
#undef RSX_TH_P2
#undef RSX_TH_P3
#undef RSX_TH_P4
#undef RSX_TH_P5
#endif
