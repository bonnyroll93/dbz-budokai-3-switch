/* Texture mutation detector: unaligned spans, every byte/bit, and optional
 * equivalence against the upstream xxHash v0.8.3 header (-DRSX_HASH_REFERENCE).
 * --bench compares throughput with the previous full-span word FNV detector. */
#include "rsx_texture_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef RSX_HASH_REFERENCE
#define XXH_INLINE_ALL
#include "xxhash.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

static NOINLINE uint64_t old_hash(const void* data, size_t size)
{
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = UINT64_C(1469598103934665603);
    while (size >= 8) {
        uint64_t word;
        memcpy(&word, p, 8);
        h = (h ^ word) * UINT64_C(1099511628211);
        p += 8;
        size -= 8;
    }
    while (size--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}

static NOINLINE uint64_t new_hash(const void* data, size_t size)
{
    return rsx_texture_hash(data, size);
}

static double bench(uint64_t (*hash)(const void*, size_t), unsigned char* buf,
                    size_t size, unsigned iterations)
{
    volatile uint64_t result = 0;
    const clock_t start = clock();
    for (unsigned i = 0; i < iterations; i++) {
        buf[i % size] ^= (unsigned char)i;
        result ^= hash(buf, size);
    }
    (void)result;
    return (double)(clock() - start) / CLOCKS_PER_SEC;
}

int main(int argc, char** argv)
{
    unsigned char buf[520];
    unsigned checks = 0;
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)(i * 73 + i / 7);
    if (rsx_texture_hash(NULL, 0) != UINT64_C(0xef46db3751d8e999)) return 1;
    for (unsigned alignment = 0; alignment < 8; alignment++) {
        unsigned char* p = buf + alignment;
        for (size_t n = 0; n <= 512; n++) {
            const uint64_t h = rsx_texture_hash(p, n);
#ifdef RSX_HASH_REFERENCE
            if (h != XXH64(p, n, 0)) {
                fprintf(stderr, "reference mismatch: offset=%u length=%zu\n", alignment, n);
                return 1;
            }
#endif
            for (size_t b = 0; b < n; b++) {
                /* Include the final byte and each accumulator's full input. */
                const unsigned char bit = (unsigned char)(1u << ((b + n) & 7u));
                p[b] ^= bit;
                const uint64_t changed = rsx_texture_hash(p, n);
                p[b] ^= bit;
                if (changed == h) {
                    fprintf(stderr, "missed mutation: offset=%u length=%zu byte=%zu\n", alignment, n, b);
                    return 1;
                }
                checks++;
            }
        }
    }
#ifdef _WIN32
    /* Put each span directly before an inaccessible page: a vector/word load
     * past the requested length would fault, even if the digest looked right. */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    unsigned char* pages = (unsigned char*)VirtualAlloc(NULL, si.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD previous;
    if (!pages || !VirtualProtect(pages + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &previous)) return 1;
    for (size_t n = 0; n <= 512; n++) {
        unsigned char* p = pages + si.dwPageSize - n;
        memcpy(p, buf, n);
        if (rsx_texture_hash(p, n) != rsx_texture_hash(buf, n)) return 1;
    }
    VirtualFree(pages, 0, MEM_RELEASE);
#endif
    printf("PASS: %u texture mutations, all tails/alignments, bounded reads", checks);
#ifdef RSX_HASH_REFERENCE
    printf(", upstream XXH64 equivalence");
#endif
    printf("\n");
    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        const size_t sizes[] = {4096, 1024 * 1024};
        unsigned char* data = (unsigned char*)malloc(sizes[1]);
        if (!data) return 1;
        for (size_t i = 0; i < sizes[1]; i++) data[i] = (unsigned char)(i * 73 + i / 7);
        for (unsigned j = 0; j < 2; j++) {
            const unsigned iterations = (unsigned)((256u * 1024u * 1024u) / sizes[j]);
            const double old_s = bench(old_hash, data, sizes[j], iterations);
            const double new_s = bench(new_hash, data, sizes[j], iterations);
            printf("%zu bytes: old=%.3fs new=%.3fs speedup=%.2fx (host only)\n",
                   sizes[j], old_s, new_s, new_s > 0 ? old_s / new_s : 0);
        }
        free(data);
    }
    return 0;
}
