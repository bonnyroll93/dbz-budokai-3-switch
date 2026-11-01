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
