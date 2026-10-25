/* Host regression: sampling scopes must restore their caller on every return. */
#include "../spu_profile.h"
#include <assert.h>
#include <stdio.h>
_Thread_local uint64_t *ps3_spu_profile_current;
static _Thread_local uint64_t test_stamp;
uint64_t *ps3_spu_profile_slot(void)
{
    return ps3_spu_profile_current = &test_stamp;
}
static uint64_t current(void) { return __atomic_load_n(&test_stamp, __ATOMIC_RELAXED); }
static void wait_scope(void)
{
    SPU_PROFILE_SCOPE(0, 0);
    assert(current() == 0);
}
static int child(int early)
{
    SPU_PROFILE_SCOPE(3, 0x5678);
    assert(current() == ((3ull << 32) | 0x5678));
    wait_scope();
    assert(current() == ((3ull << 32) | 0x5678));
    if (early) return 7;
    return 9;
}
static void parent(void)
{
    SPU_PROFILE_SCOPE(1, 0x1234);
    assert(child(1) == 7);
    assert(current() == ((1ull << 32) | 0x1234));
    assert(child(0) == 9);
    assert(current() == ((1ull << 32) | 0x1234));
}
int main(void)
{
    parent();
    assert(current() == 0);
    puts("SPU profiler scope restore: PASS");
    return 0;
}
