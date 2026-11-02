/*
 * DBZ3: lifted functions replaced by native code (dbz3_ppu_native.cpp).
 *
 * Force-included into the lifted translation unit that defines them
 * (CMakeLists.txt). Declaring them weak there lets the native definition win
 * at link time, including for calls from inside that same translation unit,
 * which a linker --wrap cannot redirect. The lifted body stays compiled in,
 * unused.
 */
#pragma once
#include "ppu_recomp.h"

void func_00025130(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00292628(ppu_context* ctx) __attribute__((weak));   /* defined in TU 004 */
/* T6.2b heap trace (dbz3_heap_trace.cpp): title alloc / free */
void func_0001D294(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001D4D8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
/* T6.2c a2d state-stack pops with an empty-stack guard (dbz3_heap_trace.cpp) */
void func_000595CC(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000595F0(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
/* A4: CRI error dispatcher, logged (dbz3_crierr.cpp) */
void func_00216DB4(ppu_context* ctx) __attribute__((weak));   /* defined in TU 003 */
/* Native-A step 1: material apply (dbz3_native_material.cpp). The lifted body
 * stays reachable as func_0002C088_lifted for the fallback and verify paths:
 * an assembler alias of a symbol defined in this object points at this
 * definition, whichever definition of the weak name the linker keeps. */
#ifdef DBZ3_NATIVE_MATERIAL
void func_0002C088(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0002C088_lifted(ppu_context* ctx)
    __attribute__((alias("_Z13func_0002C088P11ppu_context")));
#endif
/* Native-A step 2a-i: shader program binds (dbz3_native_shader.cpp). */
#ifdef DBZ3_NATIVE_SHADER
void func_00053B30(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00053880(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00053B30_lifted(ppu_context* ctx)
    __attribute__((alias("_Z13func_00053B30P11ppu_context")));
void func_00053880_lifted(ppu_context* ctx)
    __attribute__((alias("_Z13func_00053880P11ppu_context")));
#endif
/* Native-A steps 2a-ii and 2b: draw-state functions (dbz3_native_draw.cpp). */
#ifdef DBZ3_NATIVE_DRAW
void func_0002BAE8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00053A7C(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0004E288(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0004EB98(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00050844(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0002BAE8_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0002BAE8P11ppu_context")));
void func_00053A7C_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_00053A7CP11ppu_context")));
void func_0004E288_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0004E288P11ppu_context")));
void func_0004EB98_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0004EB98P11ppu_context")));
void func_00050844_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_00050844P11ppu_context")));
#endif
/* Hot math (dbz3_native_math.cpp): one function per translation unit, since
 * an alias must name a definition in the same object. */
#ifdef DBZ3_NATIVE_MATH_TU000
void func_000252B0(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000252B0_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_000252B0P11ppu_context")));
void func_0001F1E8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001F1E8_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0001F1E8P11ppu_context")));
void func_0001EEF8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001EEF8_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0001EEF8P11ppu_context")));
void func_0001F164(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001F164_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0001F164P11ppu_context")));
void func_0001EF6C(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001EF6C_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0001EF6CP11ppu_context")));
void func_000297B8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000297B8_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_000297B8P11ppu_context")));
void func_000299AC(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000299AC_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_000299ACP11ppu_context")));
void func_000357A0(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000357A0_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_000357A0P11ppu_context")));
void func_00024F58(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_00024F58_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_00024F58P11ppu_context")));
#endif
#ifdef DBZ3_NATIVE_MATH_TU005
void func_002B6E00(ppu_context* ctx) __attribute__((weak));   /* defined in TU 005: cellGcmAddressToOffset stub */
void func_002B6E00_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_002B6E00P11ppu_context")));
#endif
#ifdef DBZ3_NATIVE_MATH_TU004
void func_002A733C(ppu_context* ctx) __attribute__((weak));   /* defined in TU 004 */
void func_002A733C_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_002A733CP11ppu_context")));
void func_002993A0(ppu_context* ctx) __attribute__((weak));   /* defined in TU 004: memset */
void func_002993A0_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_002993A0P11ppu_context")));
#endif
/* Native-A step 2c: mesh draw bodies (dbz3_native_mesh.cpp). */
#ifdef DBZ3_NATIVE_MESH
void func_0001AA00(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_000328A8(ppu_context* ctx) __attribute__((weak));   /* defined in TU 000 */
void func_0001AA00_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_0001AA00P11ppu_context")));
void func_000328A8_lifted(ppu_context* ctx) __attribute__((alias("_Z13func_000328A8P11ppu_context")));
#endif
