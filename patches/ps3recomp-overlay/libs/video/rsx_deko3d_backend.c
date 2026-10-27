/*
 * ps3recomp - deko3d RSX backend for Nintendo Switch (T22)
 *
 * A backend for the register-file draw engine (rsx_draw_engine.h), modelled
 * on the Vulkan backend's engine stages but written for deko3d. Everything
 * runs on the frame clock thread, which is the only thread that drains the
 * FIFO and presents, so no locking is needed here.
 *
 * Stage 0:
 *   - device, queue, memory blocks, one command buffer, a 2-image swapchain;
 *   - colour and depth targets, clears, viewport/scissor, target binding;
 *   - present: blit the flipped surface into the swapchain image, force its
 *     alpha to 1 (guest clears arrive with alpha 0), present;
 *   - readback and color_target_write, so 2D transfers stay coherent.
 * Stage 2:
 *   - shaders: the decompilers' HLSL is compiled ahead of time on the PC
 *     (scripts/gen-deko3d-shaders.py, GLSL through uam) into
 *     deko3d/rsx_deko3d_shaders.h and looked up by the FNV-1a hash of the
 *     HLSL text. A program not in the table is dumped to the SD card and its
 *     draws are dropped, so the next build can include it;
 *   - textures (2D, every engine format, mips), sampler cache, surface views;
 *   - pipelines from the NV4097 render state, and draws through a data ring
 *     that holds vertices, indices and both constant blocks.
 *
 * Submission is synchronous (submit + wait idle) as the engine's contract
 * for submit_and_wait requires. Fences and frames in flight are stage 4.
 *
 * Diagnostics go to sdmc:/switch/dbz-budokai-3-switch/t22-gpu.log.
 */
#ifdef __SWITCH__

#include "rsx_deko3d_backend.h"
#include "rsx_draw_engine.h"
#include "deko3d/rsx_deko3d_shaders.h"
#include "../../runtime/platform/switch_debug.h"

/* <switch.h> is not included: libnx's u128/s128 clash with ps3types.h. The
 * two libnx symbols needed here are declared by hand instead. */
#include <deko3d.h>

#include <malloc.h>

void ps3_switch_resource_report(const char *tag);
/* Optional port hook: screenshots and MJPEG capture are kept out of the
 * renderer and are called only for a requested frame. */
extern int dbz3_capture_wants_frame(uint64_t now_us) __attribute__((weak));
extern void dbz3_capture_frame(const uint8_t *rgba, unsigned width,
                               unsigned height, unsigned pitch,
                               uint64_t now_us) __attribute__((weak));
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct NWindow NWindow;
extern NWindow* nwindowGetDefault(void);

#define DK_LOG_PATH      "sdmc:/switch/dbz-budokai-3-switch/t22-gpu.log"

#define DK_NUM_SWAP      2
#define DK_IMAGE_POOL    (512u * 1024u * 1024u)  /* targets and textures      */
#define DK_STAGING_SIZE  (8u * 1024u * 1024u)    /* upload / readback staging  */
/* T41: one submit per frame. With 512 ops a DBZ3 battle frame (~1300 draws)
 * hit 1-2 mid-frame dk_submit_wait()s of ~16 ms each (t29-spikes.log, L557):
 * the render thread waited for the GPU instead of overlapping with it. The
 * command chunk and the data ring are sized so a whole frame fits; a full
 * ring still falls back to a synchronous submit. */
#define DK_CMD_SIZE      (4u * 1024u * 1024u)    /* command memory, per chunk  */
#define DK_FLUSH_OPS     16384u                  /* auto-submit after N ops    */
#define DK_MAX_SURFACES  64u
#define DK_MAX_DEPTHS    32u
#define DK_MAX_EXTENTS   2048u
#define DK_CODE_SIZE     (256u * 1024u)          /* shader code                */
#define DK_RING_SIZE     (32u * 1024u * 1024u)   /* vertices, indices, UBOs    */

/* One image descriptor per handle. Handles: colour targets 1..64, textures
 * from DK_TEX_BASE, surface views from DK_VIEW_BASE; the last slot is the
 * white 1x1 an unbound unit samples. */
#define DK_MAX_TEXTURES  1024u
#define DK_MAX_VIEWS     128u
#define DK_TEX_BASE      128u
#define DK_VIEW_BASE     (DK_TEX_BASE + DK_MAX_TEXTURES)
#define DK_DESC_SLOTS    2048u
#define DK_DUMMY_SLOT    (DK_DESC_SLOTS - 1u)
#define DK_SAMP_SLOTS    256u
#define DK_SAMP_OFFSET   (DK_DESC_SLOTS * 32u)
#define DK_DESC_SIZE     ((DK_SAMP_OFFSET + DK_SAMP_SLOTS * 32u + 0xFFFu) & ~0xFFFu)
#define DK_MAX_PIPES     4096u

/* ---- log ----------------------------------------------------------------- */

static FILE* s_log;

extern int ps3_diag_enabled;   /* cellGcmSys.c; set at boot from diag.on */

static void dk_log(const char* fmt, ...)
{
    /* Diagnostics off: no SD write (each line used to be flushed twice). */
    if (!ps3_diag_enabled) return;
    /* One formatted line, one write: stderr is shared with other threads. */
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (!s_log) s_log = fopen(DK_LOG_PATH, "a");
    if (s_log) {
        fputs(line, s_log);
        fflush(s_log);
    }
    fprintf(stderr, "[deko3d] %s", line);
}

/* Resource-exhaustion and failure messages: written even with diagnostics off
 * (V3: after a long session the game slows down and renders white, and with
 * dk_log gated nothing said which table had filled). Capped at 400 lines. */
static void dk_err(const char* fmt, ...)
{
    static unsigned lines = 0;
    if (lines >= 400) return;
    lines++;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (!s_log) s_log = fopen(DK_LOG_PATH, "a");
    if (s_log) {
        fputs(line, s_log);
        fflush(s_log);
    }
    fprintf(stderr, "[deko3d] %s", line);
}

/* FNV-1a, the key the precompiled shaders are looked up by. */
static u64 dk_hash_str(const char* s)
{
    u64 h = 0xcbf29ce484222325ull;
    for (; s && *s; s++) { h ^= (u8)*s; h *= 0x100000001b3ull; }
    return h;
}

#define DK_SHADER_DUMP_DIR "sdmc:/switch/dbz-budokai-3-switch/t23-shaders"

/* Stage 2 needs the decompilers' exact HLSL to write the GLSL that uam
 * compiles, so every distinct pipeline is dumped once with its state. */
static void dk_dump_pipeline(const char* vs_hlsl, const char* ps_hlsl,
                             const rsx_be_render_state* rs,
                             const rsx_vertex_layout_plan* layout,
                             u32 vertex_stride, rsx_be_format rt_fmt, u32 rt_count)
{
    /* Always on, even with diagnostics off: it writes each distinct pipeline
     * once, and these dumps are the only way a missing shader ("pipeline
     * refused ... (missing)") gets into the precompiled set. */
    /* Each distinct (vs, ps) pair once; the old 32-call cap was spent on
     * repeats of the same few pipelines and later shaders never reached SD. */
    static u64 seen[512];
    static u32 dumped = 0;
    const u64 hv = dk_hash_str(vs_hlsl), hp = dk_hash_str(ps_hlsl);
    const u64 key = hv ^ (hp * 0x9E3779B97F4A7C15ull);
    for (u32 i = 0; i < dumped; i++) if (seen[i] == key) return;
    if (dumped >= 512) return;
    seen[dumped] = key;
    mkdir(DK_SHADER_DUMP_DIR, 0777);
    char path[256];
    FILE* f;
    snprintf(path, sizeof path, DK_SHADER_DUMP_DIR "/vs_%016llx.hlsl", (unsigned long long)hv);
    if ((f = fopen(path, "w"))) { fputs(vs_hlsl ? vs_hlsl : "", f); fclose(f); }
    snprintf(path, sizeof path, DK_SHADER_DUMP_DIR "/ps_%016llx.hlsl", (unsigned long long)hp);
    if ((f = fopen(path, "w"))) { fputs(ps_hlsl ? ps_hlsl : "", f); fclose(f); }
    snprintf(path, sizeof path, DK_SHADER_DUMP_DIR "/pipelines.txt");
    if ((f = fopen(path, "a"))) {
        fprintf(f, "pipeline vs=%016llx ps=%016llx stride=%u rt_fmt=%d rt_count=%u\n",
                (unsigned long long)hv, (unsigned long long)hp, vertex_stride,
                (int)rt_fmt, rt_count);
        if (layout) {
            fprintf(f, "  layout mask=%04x stride=%u count=%u attrs=", layout->mask,
                    layout->stride, layout->count);
            for (u32 i = 0; i < layout->count; i++) fprintf(f, "%u ", layout->attrs[i]);
            fputc('\n', f);
        }
        if (rs) {
            fprintf(f, "  alpha_test=%u func=%x ref=%x fmt=%u\n", rs->alpha_test_enable,
                    rs->alpha_func, rs->alpha_ref_raw, rs->alpha_ref_format);
            fprintf(f, "  blend=%u sf_rgb=%x df_rgb=%x sf_a=%x df_a=%x eq_rgb=%x eq_a=%x\n",
                    rs->blend_enable, rs->sf_rgb, rs->df_rgb, rs->sf_a, rs->df_a,
                    rs->eq_rgb, rs->eq_a);
            fprintf(f, "  depth test=%u write=%u func=%x cull=%u face=%x front=%x mask=%08x fp16=%u\n",
                    rs->depth_test, rs->depth_write, rs->depth_func, rs->cull_enable,
                    rs->cull_face, rs->front_face, rs->color_mask, rs->rt_fp16);
            fprintf(f, "  stencil=%u two_sided=%u func=%x\n", rs->stencil_enable,
                    rs->stencil_two_sided, rs->s_func);
        }
        fclose(f);
    }
    dumped++;
    dk_log("pipeline dumped: vs=%016llx ps=%016llx stride=%u attrs=%u\n",
           (unsigned long long)hv, (unsigned long long)hp, vertex_stride,
           layout ? layout->count : 0u);
}

/* ---- state --------------------------------------------------------------- */

typedef struct dk_alloc {          /* first-fit allocator over the image pool */
    u32 off[DK_MAX_EXTENTS], size[DK_MAX_EXTENTS];
    u32 count;                     /* free extents, kept sorted by offset     */
} dk_alloc;

typedef struct dk_target {
    u32 used;
    DkImage image;
    DkImageFormat dk_fmt;
    rsx_be_format fmt;
    u32 w, h, bpp;
    u32 logical_w, logical_h;      /* guest dimensions before render scaling */
    u32 block;                     /* BC block bytes, 0 for a linear format  */
    u32 mips;
    u8  swz[4];                    /* DkImageSwizzle per output r,g,b,a      */
    u32 mem_off, mem_size;
} dk_target;

typedef struct dk_view {
    u32 used, surface;
    u8  swz[4];
} dk_view;

typedef struct dk_pipe {
    u32 used;
    u32 vs, ps;                    /* rsx_dk_shader_table indices            */
    u64 vs_hash, ps_hash;
    u32 stride, rt_count;
    rsx_be_format rt_fmt;
    u32 nattr;
    DkVtxAttribState attr[16];
    DkRasterizerState ras;
    DkColorState color;
    DkColorWriteState cwrite;
    DkBlendState blend;
    DkDepthStencilState ds;
    u8  s_write_mask, s_func_mask;
    u32 depth_test;
} dk_pipe;

typedef struct dk_stats {
    u32 presents, clears_color, clears_depth, submits;
    u32 targets_made, depths_made, target_fail;
    u32 pipelines_refused, pipelines_made, readbacks, writes, acquire_fail;
    u32 gpu_draws, draws_skipped, tex_made, tex_fail, tex_uploads, tex_unbound, blits;
    u64 present_us, submit_us;
} dk_stats;

static struct {
    int ready;
    int error;                     /* a deko3d error was reported            */
    u32 width, height;

    DkDevice   device;
    DkQueue    queue;
    DkMemBlock mem_images;         /* GPU-only: targets                      */
    DkMemBlock mem_cmd;            /* command memory                         */
    DkMemBlock mem_cmd_spare;      /* overflow chunk, added on demand        */
    DkMemBlock mem_staging;        /* CPU-cached transfer buffer             */
    u32 staging_used;              /* bytes of staging recorded, not yet idle */
    int texbatch_off;              /* sdmc texbatch-off                      */
    u32 flush_ops;                 /* DK_FLUSH_OPS, or 512 with sdmc flush512 */
    DkCmdBuf   cmd;
    u32 ops;                       /* commands recorded since last submit    */
    int spare_in_use;

    DkImage     swap_images[DK_NUM_SWAP];
    DkMemBlock  mem_swap;
    DkSwapchain swapchain;

    dk_alloc  alloc;
    dk_target surfaces[DK_MAX_SURFACES + 1];   /* handle = index, 0 = none */
    dk_target depths[DK_MAX_DEPTHS + 1];

    /* What the engine last bound, re-recorded after a clear or a present
     * rebinds targets of its own. */
    u32 bound_color[RSX_BE_MAX_COLOR_TARGETS];
    u32 bound_count, bound_depth;
    int bound_dirty;

    /* State recorded in the current command list, not guest-side bindings. */
    struct {
        int pipeline_valid, textures_valid, viewport_valid, scissor_valid;
        u32 pipeline, stencil_ref, vertex_stride;
        DkResHandle textures[16];
        DkViewport viewport;
        DkScissor scissor;
    } recorded;

    /* Stage 2. */
    DkMemBlock mem_code;           /* shader code, appended on first use     */
    DkMemBlock mem_ring;           /* per-draw data, recycled at each submit */
    /* T23: two sets of ring + command memory. A present submits the frame
     * with a fence and switches set instead of waiting for the GPU; the set
     * is reused only after its fence. Any other submit still waits for idle. */
    DkMemBlock set_ring[2], set_cmd[2], set_cmd_spare[2];
    DkFence    set_fence[2];
    int        set_pending[2];
    int        set_cur;
    int        async_present;      /* 0: every present waits (present-sync) */
    DkMemBlock mem_desc;           /* image + sampler descriptors            */
    u32 code_used, ring_used;
    dk_target textures[DK_MAX_TEXTURES];
    dk_view   views[DK_MAX_VIEWS];
    dk_target dummy;
    u64 samp_keys[DK_SAMP_SLOTS];
    u32 samp_count;
    DkShader shaders[RSX_DK_SHADER_COUNT];
    u8  shader_loaded[RSX_DK_SHADER_COUNT];
    dk_pipe pipes[DK_MAX_PIPES];
    u32 warned;
    struct {
        u32 pipe;
        const void* vs_data; u32 vs_bytes;
        const void* ps_data; u32 ps_bytes;
        u32 tex[16];
        rsx_be_sampler_desc samp[16];
        u32 tex_mask;
        float vp[4]; int have_vp;
        u32 sc[4];   int have_sc;
        u32 stencil_ref;
    } draw;

    dk_stats st;
} s_dk;

static u64 dk_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000u + (u64)ts.tv_nsec / 1000u;
}

static void dk_debug_cb(void* user, const char* context, DkResult result,
                        const char* message)
{
    (void)user;
    dk_log("debug: %s: result %d: %s\n", context ? context : "?", (int)result,
           message ? message : "");
    if (result != DkResult_Success) s_dk.error = 1;
}

/* ---- image pool allocator ------------------------------------------------ */

static void dk_alloc_init(dk_alloc* a, u32 size)
{
    a->count = 1;
    a->off[0] = 0;
    a->size[0] = size;
}

static int dk_alloc_take(dk_alloc* a, u32 size, u32 align, u32* out)
{
    for (u32 i = 0; i < a->count; i++) {
        const u32 start = (a->off[i] + align - 1u) & ~(align - 1u);
        const u32 pad = start - a->off[i];
        if (a->size[i] < pad || a->size[i] - pad < size) continue;
        const u32 end = start + size;
        const u32 old_end = a->off[i] + a->size[i];
        if (pad == 0) {
            a->off[i] = end;
            a->size[i] = old_end - end;
            if (!a->size[i]) {
                memmove(&a->off[i], &a->off[i + 1], (a->count - i - 1) * sizeof(u32));
                memmove(&a->size[i], &a->size[i + 1], (a->count - i - 1) * sizeof(u32));
                a->count--;
            }
        } else {
            a->size[i] = pad;
            if (old_end > end) {
                if (a->count >= DK_MAX_EXTENTS) return -1;
                memmove(&a->off[i + 2], &a->off[i + 1], (a->count - i - 1) * sizeof(u32));
                memmove(&a->size[i + 2], &a->size[i + 1], (a->count - i - 1) * sizeof(u32));
                a->off[i + 1] = end;
                a->size[i + 1] = old_end - end;
                a->count++;
            }
        }
        *out = start;
        return 0;
    }
    return -1;
}

static void dk_alloc_give(dk_alloc* a, u32 off, u32 size)
{
    u32 i = 0;
    while (i < a->count && a->off[i] < off) i++;
    if (a->count >= DK_MAX_EXTENTS) return;      /* leak rather than corrupt */
    memmove(&a->off[i + 1], &a->off[i], (a->count - i) * sizeof(u32));
    memmove(&a->size[i + 1], &a->size[i], (a->count - i) * sizeof(u32));
    a->off[i] = off;
    a->size[i] = size;
    a->count++;
    /* merge with the next, then with the previous */
    if (i + 1 < a->count && a->off[i] + a->size[i] == a->off[i + 1]) {
        a->size[i] += a->size[i + 1];
        memmove(&a->off[i + 1], &a->off[i + 2], (a->count - i - 2) * sizeof(u32));
        memmove(&a->size[i + 1], &a->size[i + 2], (a->count - i - 2) * sizeof(u32));
        a->count--;
    }
    if (i > 0 && a->off[i - 1] + a->size[i - 1] == a->off[i]) {
        a->size[i - 1] += a->size[i];
        memmove(&a->off[i], &a->off[i + 1], (a->count - i - 1) * sizeof(u32));
        memmove(&a->size[i], &a->size[i + 1], (a->count - i - 1) * sizeof(u32));
        a->count--;
    }
}

/* ---- command submission -------------------------------------------------- */

static void dk_cmd_reset(void)
{
    dkCmdBufClear(s_dk.cmd);
    memset(&s_dk.recorded, 0, sizeof s_dk.recorded);
    dkCmdBufAddMemory(s_dk.cmd, s_dk.mem_cmd, 0, DK_CMD_SIZE);
    s_dk.spare_in_use = 0;
    s_dk.ops = 0;
    /* Every list starts with the descriptor sets bound and the GPU's caches
     * dropped: descriptors, shader code and the data ring are written by the
     * CPU between submits, at addresses an earlier list already read. */
    if (s_dk.mem_desc) {
        const DkGpuAddr d = dkMemBlockGetGpuAddr(s_dk.mem_desc);
        dkCmdBufBindImageDescriptorSet(s_dk.cmd, d, DK_DESC_SLOTS);
        dkCmdBufBindSamplerDescriptorSet(s_dk.cmd, d + DK_SAMP_OFFSET, DK_SAMP_SLOTS);
        dkCmdBufBarrier(s_dk.cmd, DkBarrier_None,
                        DkInvalidateFlags_Descriptors | DkInvalidateFlags_Shader |
                        DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    }
}

/* Called by deko3d when the command memory runs out mid-recording. */
static void dk_cmd_add_mem(void* user, DkCmdBuf cmdbuf, size_t min_size)
{
    (void)user;
    if (!s_dk.spare_in_use && min_size <= DK_CMD_SIZE) {
        dkCmdBufAddMemory(cmdbuf, s_dk.mem_cmd_spare, 0, DK_CMD_SIZE);
        s_dk.spare_in_use = 1;
        dk_log("command memory overflow: spare chunk added\n");
        return;
    }
    dk_log("command memory exhausted (%u bytes asked)\n", (u32)min_size);
    s_dk.error = 1;
}

/* T41 spike log: per presented frame, what made the render thread wait.
 * A frame slower than SPK_FRAME_US appends one line to t29-spikes.log (no
 * diag.on needed; nothing is written for normal frames). Waits are tagged
 * with the source line that asked for them. */
#define SPK_FRAME_US   45000u
#define SPK_MAX_LINES  4000u
#define SPK_TAGS       8
static struct {
    u64 last_present_us;
    u64 wait_us, fence_us;
    u32 waits, uploads, readbacks, tex_made, pipes;
    struct { u32 line, n; u64 us; } tag[SPK_TAGS];
    FILE* f;
    u32 lines;
} s_spk;

static void spk_wait_note(int line, u64 us)
{
    s_spk.wait_us += us;
    s_spk.waits++;
    int free_i = -1;
    for (int i = 0; i < SPK_TAGS; i++) {
        if (s_spk.tag[i].n && s_spk.tag[i].line == (u32)line) {
            s_spk.tag[i].n++; s_spk.tag[i].us += us; return;
        }
        if (!s_spk.tag[i].n && free_i < 0) free_i = i;
    }
    if (free_i >= 0) {
        s_spk.tag[free_i].line = (u32)line; s_spk.tag[free_i].n = 1; s_spk.tag[free_i].us = us;
    }
}

/* Called at the end of every present. */
static void spk_frame_end(u64 now)
{
    const u64 frame_us = s_spk.last_present_us ? now - s_spk.last_present_us : 0;
    if (frame_us > SPK_FRAME_US && s_spk.lines < SPK_MAX_LINES) {
        if (!s_spk.f) s_spk.f = fopen("sdmc:/switch/dbz-budokai-3-switch/t29-spikes.log", "w");
        if (s_spk.f) {
            fprintf(s_spk.f, "present=%u frame_ms=%.1f wait_ms=%.1f waits=%u fence_ms=%.1f "
                    "uploads=%u readbacks=%u tex_made=%u pipes=%u",
                    (unsigned)s_dk.st.presents, frame_us / 1000.0, s_spk.wait_us / 1000.0,
                    (unsigned)s_spk.waits, s_spk.fence_us / 1000.0, (unsigned)s_spk.uploads,
                    (unsigned)s_spk.readbacks, (unsigned)s_spk.tex_made, (unsigned)s_spk.pipes);
            for (int i = 0; i < SPK_TAGS; i++)
                if (s_spk.tag[i].n)
                    fprintf(s_spk.f, " L%u:%ux%.1fms", (unsigned)s_spk.tag[i].line,
                            (unsigned)s_spk.tag[i].n, s_spk.tag[i].us / 1000.0);
            fputc('\n', s_spk.f);
            if (++s_spk.lines % 8u == 0) fflush(s_spk.f);
        }
    }
    s_spk.last_present_us = now;
    s_spk.wait_us = s_spk.fence_us = 0;
    s_spk.waits = s_spk.uploads = s_spk.readbacks = s_spk.tex_made = s_spk.pipes = 0;
    memset(s_spk.tag, 0, sizeof s_spk.tag);
}

/* The GPU has finished everything: no set is in flight any more. */
static void dk_sets_idle(void)
{
    s_dk.set_pending[0] = s_dk.set_pending[1] = 0;
    s_dk.staging_used = 0;         /* nothing in flight reads it any more */
}

/* Before the CPU frees or rewrites memory an earlier, still running frame may
 * read (image memory, the staging buffer). */
static void dk_wait_inflight_at(int line)
{
    if (s_dk.set_pending[0] || s_dk.set_pending[1]) {
        const u64 t0 = dk_now_us();
        dkQueueWaitIdle(s_dk.queue);
        dk_sets_idle();
        spk_wait_note(line, dk_now_us() - t0);
    }
}
#define dk_wait_inflight() dk_wait_inflight_at(__LINE__)

static void dk_submit_wait_at(int line)
{
    const u64 t0 = dk_now_us();
    DkCmdList list = dkCmdBufFinishList(s_dk.cmd);
    dkQueueSubmitCommands(s_dk.queue, list);
    dkQueueWaitIdle(s_dk.queue);
    dk_sets_idle();
    spk_wait_note(line, dk_now_us() - t0);
    s_dk.ring_used = 0;            /* the GPU is done with every draw's data */
    dk_cmd_reset();
    s_dk.st.submits++;
    s_dk.st.submit_us += dk_now_us() - t0;
    if (dkQueueIsInErrorState(s_dk.queue) && !s_dk.error) {
        dk_log("queue is in error state\n");
        s_dk.error = 1;
    }
}

#define dk_submit_wait() dk_submit_wait_at(__LINE__)

/* End of frame: submit with a fence, move to the other set, and wait only for
 * that set's previous frame. The GPU runs frame N while the FIFO builds N+1. */
static void dk_submit_async(void)
{
    if (!s_dk.async_present || !s_dk.set_ring[1]) { dk_submit_wait(); return; }
    const u64 t0 = dk_now_us();
    DkCmdList list = dkCmdBufFinishList(s_dk.cmd);
    dkQueueSubmitCommands(s_dk.queue, list);
    dkQueueSignalFence(s_dk.queue, &s_dk.set_fence[s_dk.set_cur], false);
    dkQueueFlush(s_dk.queue);
    s_dk.set_pending[s_dk.set_cur] = 1;
    s_dk.set_cur ^= 1;
    if (s_dk.set_pending[s_dk.set_cur]) {
        const u64 tf = dk_now_us();
        dkFenceWait(&s_dk.set_fence[s_dk.set_cur], -1);
        s_spk.fence_us += dk_now_us() - tf;
        s_dk.set_pending[s_dk.set_cur] = 0;
    }
    s_dk.mem_ring = s_dk.set_ring[s_dk.set_cur];
    s_dk.mem_cmd = s_dk.set_cmd[s_dk.set_cur];
    s_dk.mem_cmd_spare = s_dk.set_cmd_spare[s_dk.set_cur];
    s_dk.ring_used = 0;
    dk_cmd_reset();
    s_dk.st.submits++;
    s_dk.st.submit_us += dk_now_us() - t0;
    if (dkQueueIsInErrorState(s_dk.queue) && !s_dk.error) {
        dk_log("queue is in error state\n");
        s_dk.error = 1;
    }
}

static void dk_note_op(void)
{
    if (++s_dk.ops >= (s_dk.flush_ops ? s_dk.flush_ops : DK_FLUSH_OPS)) dk_submit_wait();
}

/* 2D/copy engine work must see every earlier 3D write, and the other way. */
static void dk_full_barrier(void)
{
    dkCmdBufBarrier(s_dk.cmd, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
}

/* ---- targets ------------------------------------------------------------- */

static void dk_write_image_desc(u32 slot, const DkImage* img, const u8* swz);

static int dk_format(rsx_be_format fmt, DkImageFormat* out, u32* bpp)
{
    switch (fmt) {
    case RSX_BE_FMT_R8G8B8A8:      *out = DkImageFormat_RGBA8_Unorm;  *bpp = 4;  return 0;
    case RSX_BE_FMT_R16G16B16A16F: *out = DkImageFormat_RGBA16_Float; *bpp = 8;  return 0;
    case RSX_BE_FMT_R32F:          *out = DkImageFormat_R32_Float;    *bpp = 4;  return 0;
    case RSX_BE_FMT_R32G32B32A32F: *out = DkImageFormat_RGBA32_Float; *bpp = 16; return 0;
    default: return -1;
    }
}

static int dk_image_create(dk_target* t, DkImageFormat dk_fmt, u32 w, u32 h,
                           u32 mips, u32 flags)
{
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_dk.device);
    lm.flags = flags;
    lm.format = dk_fmt;
    lm.dimensions[0] = w;
    lm.dimensions[1] = h;
    lm.mipLevels = mips ? mips : 1u;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    const u64 size64 = dkImageLayoutGetSize(&layout);
    const u32 align = dkImageLayoutGetAlignment(&layout);
    if (size64 > DK_IMAGE_POOL) return -1;
    const u32 size = (u32)size64;
    u32 off = 0;
    if (dk_alloc_take(&s_dk.alloc, size, align ? align : 1u, &off)) return -1;
    dkImageInitialize(&t->image, &layout, s_dk.mem_images, off);
    t->mem_off = off;
    t->mem_size = size;
    t->dk_fmt = dk_fmt;
    t->w = w;
    t->h = h;
    t->used = 1;
    return 0;
}

/* dk_wait_inflight: macro over dk_wait_inflight_at, defined above */
static void dk_image_free(dk_target* t)
{
    if (!t->used) return;
    dk_wait_inflight();
    dk_alloc_give(&s_dk.alloc, t->mem_off, t->mem_size);
    memset(t, 0, sizeof *t);
}

/* Upload `rows` (w*bpp packed, or src_pitch apart) into a rectangle. */
static void dk_upload_rect(dk_target* t, u32 x, u32 y, u32 w, u32 h,
                           const void* rows, u32 src_pitch)
{
    const u32 row = w * t->bpp;
    if (!w || !h || (u64)row * h > DK_STAGING_SIZE) return;
    /* The staging buffer may still be the source of an earlier recorded
     * copy; submitting first keeps the contents of every copy intact. */
    if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
    u8* dst = (u8*)dkMemBlockGetCpuAddr(s_dk.mem_staging);
    for (u32 r = 0; r < h; r++)
        memcpy(dst + (size_t)r * row, (const u8*)rows + (size_t)r * src_pitch, row);
    dkMemBlockFlushCpuCache(s_dk.mem_staging, 0, row * h);

    DkImageView view;
    dkImageViewDefaults(&view, &t->image);
    DkCopyBuf src = { dkMemBlockGetGpuAddr(s_dk.mem_staging), 0, 0 };
    DkImageRect rect = { x, y, 0, w, h, 1 };
    dk_full_barrier();
    dkCmdBufCopyBufferToImage(s_dk.cmd, &src, &view, &rect, 0);
    dk_full_barrier();
    dk_submit_wait();
}

static u32 dk_scaled_dim(u32 logical)
{
    u32 n = (logical * (u32)switch_debug_snapshot().active_scale_tenths + 5u) / 10u;
    return n ? n : 1u;
}

static u32 dk_scale_edge(u32 edge, u32 logical, u32 physical)
{
    if (!logical) return 0;
    if (edge >= logical) return physical;
    return (u32)((u64)edge * physical / logical);
}

static void dk_scaled_rect(const dk_target* t, u32 x, u32 y, u32 w, u32 h,
                           u32* px, u32* py, u32* pw, u32* ph)
{
    u32 x0 = dk_scale_edge(x, t->logical_w, t->w);
    u32 y0 = dk_scale_edge(y, t->logical_h, t->h);
    u32 x1 = dk_scale_edge(x + w, t->logical_w, t->w);
    u32 y1 = dk_scale_edge(y + h, t->logical_h, t->h);
    if (w && x1 <= x0 && x0 < t->w) x1 = x0 + 1;
    if (h && y1 <= y0 && y0 < t->h) y1 = y0 + 1;
    *px = x0; *py = y0; *pw = x1 - x0; *ph = y1 - y0;
}

static void dk_upload_logical(dk_target* t, u32 x, u32 y, u32 w, u32 h,
                              const void* rows, u32 pitch)
{
    if (!rows || !w || !h) return;
    u32 px, py, pw, ph;
    dk_scaled_rect(t, x, y, w, h, &px, &py, &pw, &ph);
    if (!pw || !ph || (u64)pw * ph * t->bpp > DK_STAGING_SIZE) return;
    if (t->w == t->logical_w && t->h == t->logical_h) {
        dk_upload_rect(t, px, py, pw, ph, rows, pitch);
        return;
    }
    u8* reduced = (u8*)malloc((size_t)pw * ph * t->bpp);
    if (!reduced) return;
    for (u32 r = 0; r < ph; r++) {
        u32 sy = (u32)((u64)(py + r) * t->logical_h / t->h);
        if (sy < y) sy = y;
        if (sy >= y + h) sy = y + h - 1;
        for (u32 c = 0; c < pw; c++) {
            u32 sx = (u32)((u64)(px + c) * t->logical_w / t->w);
            if (sx < x) sx = x;
            if (sx >= x + w) sx = x + w - 1;
            memcpy(reduced + ((size_t)r * pw + c) * t->bpp,
                   (const u8*)rows + (size_t)(sy - y) * pitch + (size_t)(sx - x) * t->bpp,
                   t->bpp);
        }
    }
    dk_upload_rect(t, px, py, pw, ph, reduced, pw * t->bpp);
    free(reduced);
}

static u32 be_color_target_create(void* user, rsx_be_format fmt, u32 w, u32 h,
                                  const void* seed, u32 seed_row_bytes)
{
    (void)user;
    DkImageFormat dk_fmt;
    u32 bpp;
    if (!w || !h || w > 4096 || h > 4096 || dk_format(fmt, &dk_fmt, &bpp)) {
        s_dk.st.target_fail++;
        dk_err("colour target refused: fmt %d %ux%u\n", (int)fmt, w, h);
        return 0;
    }
    for (u32 i = 1; i <= DK_MAX_SURFACES; i++) {
        dk_target* t = &s_dk.surfaces[i];
        if (t->used) continue;
        if (dk_image_create(t, dk_fmt, dk_scaled_dim(w), dk_scaled_dim(h), 1,
                            DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)) {
            s_dk.st.target_fail++;
            dk_err("colour target %ux%u fmt %d: out of image memory\n", w, h, (int)fmt);
            return 0;
        }
        t->fmt = fmt;
        t->bpp = bpp;
        t->logical_w = w; t->logical_h = h;
        t->mips = 1;
        dk_write_image_desc(i, &t->image, NULL);    /* sampled as a texture */
        if (seed) {
            dk_upload_logical(t, 0, 0, w, h, seed, seed_row_bytes ? seed_row_bytes : w * bpp);
        } else {
            /* Defined contents: opaque black, like the Vulkan backend. */
            DkImageView view;
            dkImageViewDefaults(&view, &t->image);
            DkImageView const* views[1] = { &view };
            dkCmdBufBindRenderTargets(s_dk.cmd, views, 1, NULL);
            DkScissor sc = { 0, 0, t->w, t->h };
            dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
            dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
            s_dk.bound_dirty = 1;
            dk_note_op();
        }
        s_dk.st.targets_made++;
        if (s_dk.st.targets_made <= 16)
            dk_log("colour target %u: %ux%u fmt %d%s\n", i, w, h, (int)fmt,
                   seed ? " (seeded)" : "");
        return i;
    }
    s_dk.st.target_fail++;
    return 0;
}

static void be_color_target_release(void* user, u32 surface)
{
    (void)user;
    if (!surface || surface > DK_MAX_SURFACES) return;
    if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
    dk_image_free(&s_dk.surfaces[surface]);
    for (u32 i = 0; i < DK_MAX_VIEWS; i++)
        if (s_dk.views[i].used && s_dk.views[i].surface == surface)
            memset(&s_dk.views[i], 0, sizeof s_dk.views[i]);
}

static void be_color_target_write(void* user, u32 surface, u32 x, u32 y, u32 w,
                                  u32 h, const void* rows, u32 row_bytes)
{
    (void)user;
    if (!surface || surface > DK_MAX_SURFACES || !s_dk.surfaces[surface].used) return;
    dk_target* t = &s_dk.surfaces[surface];
    if (x >= t->logical_w || y >= t->logical_h) return;
    if (w > t->logical_w - x) w = t->logical_w - x;
    if (h > t->logical_h - y) h = t->logical_h - y;
    dk_upload_logical(t, x, y, w, h, rows, row_bytes);
    s_dk.st.writes++;
}

static u32 be_depth_target_create(void* user, u32 w, u32 h)
{
    (void)user;
    if (!w || !h || w > 4096 || h > 4096) return 0;
    for (u32 i = 1; i <= DK_MAX_DEPTHS; i++) {
        dk_target* t = &s_dk.depths[i];
        if (t->used) continue;
        if (dk_image_create(t, DkImageFormat_Z24S8, dk_scaled_dim(w), dk_scaled_dim(h), 1, DkImageFlags_UsageRender)) {
            dk_err("depth target %ux%u: out of image memory\n", w, h);
            return 0;
        }
        t->bpp = 4;
        t->logical_w = w; t->logical_h = h;
        DkImageView view;
        dkImageViewDefaults(&view, &t->image);
        dkCmdBufBindRenderTargets(s_dk.cmd, NULL, 0, &view);
        DkScissor sc = { 0, 0, t->w, t->h };
        dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
        dkCmdBufClearDepthStencil(s_dk.cmd, true, 1.0f, 0xFF, 0);  /* far plane */
        s_dk.bound_dirty = 1;
        dk_note_op();
        s_dk.st.depths_made++;
        return i;
    }
    return 0;
}

static void be_depth_target_release(void* user, u32 depth)
{
    (void)user;
    if (!depth || depth > DK_MAX_DEPTHS) return;
    if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
    dk_image_free(&s_dk.depths[depth]);
}

/* ---- binding ------------------------------------------------------------- */

static void dk_rebind_targets(void)
{
    DkImageView views[RSX_BE_MAX_COLOR_TARGETS];
    DkImageView const* ptrs[RSX_BE_MAX_COLOR_TARGETS];
    u32 n = 0;
    for (u32 i = 0; i < s_dk.bound_count; i++) {
        const u32 h = s_dk.bound_color[i];
        if (!h || h > DK_MAX_SURFACES || !s_dk.surfaces[h].used) continue;
        dkImageViewDefaults(&views[n], &s_dk.surfaces[h].image);
        ptrs[n] = &views[n];
        n++;
    }
    DkImageView dview;
    int has_depth = s_dk.bound_depth && s_dk.bound_depth <= DK_MAX_DEPTHS &&
                    s_dk.depths[s_dk.bound_depth].used;
    /* A depth target smaller than the colour one cannot be attached. */
    if (has_depth && n && s_dk.bound_color[0] && s_dk.bound_color[0] <= DK_MAX_SURFACES) {
        const dk_target* c0 = &s_dk.surfaces[s_dk.bound_color[0]];
        const dk_target* z = &s_dk.depths[s_dk.bound_depth];
        if (z->w < c0->w || z->h < c0->h) has_depth = 0;
    }
    if (has_depth) dkImageViewDefaults(&dview, &s_dk.depths[s_dk.bound_depth].image);
    dkCmdBufBindRenderTargets(s_dk.cmd, n ? ptrs : NULL, n, has_depth ? &dview : NULL);
    s_dk.bound_dirty = 0;
}

static void be_bind_targets(void* user, const u32* surfaces, u32 count, u32 depth)
{
    (void)user;
    if (count > RSX_BE_MAX_COLOR_TARGETS) count = RSX_BE_MAX_COLOR_TARGETS;
    for (u32 i = 0; i < count; i++) s_dk.bound_color[i] = surfaces ? surfaces[i] : 0;
    s_dk.bound_count = count;
    s_dk.bound_depth = depth;
    dk_rebind_targets();
}

/* Remembered, and applied by the draw: a clear in between sets a scissor of
 * its own. */
static void be_set_viewport(void* user, float x, float y, float w, float h)
{
    (void)user;
    s_dk.draw.vp[0] = x; s_dk.draw.vp[1] = y; s_dk.draw.vp[2] = w; s_dk.draw.vp[3] = h;
    s_dk.draw.have_vp = 1;
}

static void be_set_scissor(void* user, u32 x, u32 y, u32 w, u32 h)
{
    (void)user;
    s_dk.draw.sc[0] = x; s_dk.draw.sc[1] = y; s_dk.draw.sc[2] = w; s_dk.draw.sc[3] = h;
    s_dk.draw.have_sc = 1;
}

/* ---- clears -------------------------------------------------------------- */

static void be_clear_color(void* user, u32 surface, const float rgba[4])
{
    (void)user;
    if (!surface || surface > DK_MAX_SURFACES || !s_dk.surfaces[surface].used) return;
    dk_target* t = &s_dk.surfaces[surface];
    DkImageView view;
    dkImageViewDefaults(&view, &t->image);
    DkImageView const* views[1] = { &view };
    dkCmdBufBindRenderTargets(s_dk.cmd, views, 1, NULL);
    DkScissor sc = { 0, 0, t->w, t->h };
    dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
    dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, rgba[0], rgba[1], rgba[2], rgba[3]);
    s_dk.bound_dirty = 1;
    s_dk.st.clears_color++;
    dk_note_op();
}

static void be_clear_depth_stencil(void* user, u32 depth, u32 flags,
                                   float depth_value, u8 stencil)
{
    (void)user;
    if (!depth || depth > DK_MAX_DEPTHS || !s_dk.depths[depth].used) return;
    dk_target* t = &s_dk.depths[depth];
    DkImageView view;
    dkImageViewDefaults(&view, &t->image);
    dkCmdBufBindRenderTargets(s_dk.cmd, NULL, 0, &view);
    DkScissor sc = { 0, 0, t->w, t->h };
    dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
    dkCmdBufClearDepthStencil(s_dk.cmd, (flags & RSX_BE_CLEAR_DEPTH) != 0, depth_value,
                              (flags & RSX_BE_CLEAR_STENCIL) ? 0xFF : 0x00, stencil);
    s_dk.bound_dirty = 1;
    s_dk.st.clears_depth++;
    dk_note_op();
}

/* ---- readback ------------------------------------------------------------ */

static void be_readback(void* user, u32 surface, u32 x, u32 y, u32 w, u32 h,
                        void* out, u32 out_pitch)
{
    (void)user;
    if (!out || !surface || surface > DK_MAX_SURFACES || !s_dk.surfaces[surface].used) return;
    dk_target* t = &s_dk.surfaces[surface];
    if (x >= t->logical_w || y >= t->logical_h || !w || !h) return;
    if (w > t->logical_w - x) w = t->logical_w - x;
    if (h > t->logical_h - y) h = t->logical_h - y;
    u32 px, py, pw, ph;
    dk_scaled_rect(t, x, y, w, h, &px, &py, &pw, &ph);
    const u32 row = pw * t->bpp;
    if (!pw || !ph || (u64)row * ph > DK_STAGING_SIZE) return;

    DkImageView view;
    dkImageViewDefaults(&view, &t->image);
    DkImageRect rect = { px, py, 0, pw, ph, 1 };
    DkCopyBuf dst = { dkMemBlockGetGpuAddr(s_dk.mem_staging), 0, 0 };
    dk_full_barrier();
    dkCmdBufCopyImageToBuffer(s_dk.cmd, &view, &rect, &dst, 0);
    dk_full_barrier();
    dk_submit_wait();
    /* Clean + invalidate, so the CPU reads what the GPU wrote. */
    dkMemBlockFlushCpuCache(s_dk.mem_staging, 0, row * ph);
    const u8* src = (const u8*)dkMemBlockGetCpuAddr(s_dk.mem_staging);
    if (t->w == t->logical_w && t->h == t->logical_h) {
        for (u32 r = 0; r < h; r++)
            memcpy((u8*)out + (size_t)r * out_pitch, src + (size_t)r * row, row);
        s_dk.st.readbacks++;
        s_spk.readbacks++;
        return;
    }
    for (u32 r = 0; r < h; r++) {
        u32 sy = dk_scale_edge(y + r, t->logical_h, t->h);
        if (sy < py) sy = py;
        if (sy >= py + ph) sy = py + ph - 1;
        for (u32 c = 0; c < w; c++) {
            u32 sx = dk_scale_edge(x + c, t->logical_w, t->w);
            if (sx < px) sx = px;
            if (sx >= px + pw) sx = px + pw - 1;
            memcpy((u8*)out + (size_t)r * out_pitch + (size_t)c * t->bpp,
                   src + ((size_t)(sy - py) * pw + sx - px) * t->bpp, t->bpp);
        }
    }
    s_dk.st.readbacks++;
    s_spk.readbacks++;
}

/* ---- present ------------------------------------------------------------- */

static void dk_stats_window(void)
{
    if (s_dk.st.presents % 30u) return;
    if (s_dk.st.presents % 300u == 0) ps3_switch_resource_report("periodic");
    dk_log("presents=%u avg_present_us=%llu submits=%u avg_submit_us=%llu "
           "clears c=%u d=%u targets=%u depths=%u target_fail=%u "
           "pipelines=%u refused=%u readbacks=%u writes=%u acquire_fail=%u "
           "gpu_draws=%u skipped=%u tex=%u tex_fail=%u uploads=%u unbound=%u "
           "blits=%u guest_draws_last_frame=%u\n",
           s_dk.st.presents,
           (unsigned long long)(s_dk.st.present_us / 30u),
           s_dk.st.submits,
           (unsigned long long)(s_dk.st.submits ? s_dk.st.submit_us / s_dk.st.submits : 0),
           s_dk.st.clears_color, s_dk.st.clears_depth, s_dk.st.targets_made,
           s_dk.st.depths_made, s_dk.st.target_fail, s_dk.st.pipelines_made,
           s_dk.st.pipelines_refused,
           s_dk.st.readbacks, s_dk.st.writes, s_dk.st.acquire_fail,
           s_dk.st.gpu_draws, s_dk.st.draws_skipped, s_dk.st.tex_made, s_dk.st.tex_fail,
           s_dk.st.tex_uploads, s_dk.st.tex_unbound, s_dk.st.blits,
           rsx_draw_engine_guest_draws());
    s_dk.st.present_us = 0;
    s_dk.st.submit_us = 0;
    s_dk.st.submits = 0;
}

/* FPS counter, top right of the swapchain image: a 3x5 block font drawn with
 * scissored clears (no shader, no texture). Shows presents per second over
 * the last full second, one decimal. */
static void dk_draw_fps_overlay(void)
{
    static const unsigned short glyph[11] = {   /* 3x5, row-major, MSB first */
        0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,
        0x0002 /* '.' */ };
    static u64 win_t0 = 0;
    static u32 win_n = 0, shown_x10 = 0;
    const u64 now = dk_now_us();
    if (!win_t0) win_t0 = now;
    win_n++;
    if (now - win_t0 >= 1000000u) {
        shown_x10 = (u32)((u64)win_n * 10000000u / (now - win_t0));
        win_t0 = now;
        win_n = 0;
    }
    int ch[6], n = 0;
    u32 ip = shown_x10 / 10u;
    if (ip > 999u) ip = 999u;
    if (ip >= 100u) ch[n++] = (int)(ip / 100u);
    if (ip >= 10u) ch[n++] = (int)(ip / 10u % 10u);
    ch[n++] = (int)(ip % 10u);
    ch[n++] = 10;
    ch[n++] = (int)(shown_x10 % 10u);
    const u32 px = s_dk.width >= 1920 ? 6u : 4u;   /* one font cell */
    const u32 adv = px * 4u, pad = px;
    const u32 box_w = adv * (u32)n + pad, box_h = px * 5u + pad * 2u;
    const u32 x0 = s_dk.width - box_w - px * 2u, y0 = px * 2u;
    DkScissor sc = { x0, y0, box_w, box_h };
    dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
    dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    for (int c = 0; c < n; c++) {
        const unsigned g = glyph[ch[c]];
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < 3; k++) {
                if (!(g & (1u << (14 - (r * 3 + k))))) continue;
                DkScissor b = { x0 + pad + (u32)c * adv + (u32)k * px, y0 + pad + (u32)r * px, px, px };
                dkCmdBufSetScissors(s_dk.cmd, 0, &b, 1);
                dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.2f, 1.0f, 0.2f, 1.0f);
            }
    }
}

/* Compact 3x5 ASCII font for the Switch debug controls. */
static unsigned dk_debug_glyph(char c)
{
    static const unsigned short digits[10] = {
        0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9,
        0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF
    };
    static const char* letters[26] = {
        "111101111101101", "110101110101110", "111100100100111",
        "110101101101110", "111100110100111", "111100110100100",
        "111100101101111", "101101111101101", "111010010010111",
        "001001001101111", "101101110101101", "100100100100111",
        "101111111101101", "101111111111101", "111101101101111",
        "111101111100100", "111101101111011", "111101111110101",
        "111100111001111", "111010010010010", "101101101101111",
        "101101101101010", "101101111111101", "101101010101101",
        "101101010010010", "111001010100111"
    };
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') {
        unsigned bits = 0;
        for (int i = 0; i < 15; i++) bits = (bits << 1) | (letters[c - 'A'][i] == '1');
        return bits;
    }
    if (c == '.') return 0x0002;
    if (c == ':') return 0x0410;
    if (c == '>') return 0x2212;
    if (c == '-') return 0x01C0;
    if (c == '+') return 0x05D0;
    if (c == '/') return 0x1248;
    return 0;
}

static void dk_debug_text(u32 x, u32 y, const char* str, u32 px,
                          float red, float green, float blue)
{
    for (u32 ch = 0; str[ch]; ch++) {
        unsigned glyph = dk_debug_glyph(str[ch]);
        for (u32 r = 0; r < 5; r++) for (u32 col = 0; col < 3; col++) {
            if (!(glyph & (1u << (14u - r * 3u - col)))) continue;
            DkScissor b = { x + ch * 4u * px + col * px, y + r * px, px, px };
            dkCmdBufSetScissors(s_dk.cmd, 0, &b, 1);
            dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA,
                                    red, green, blue, 1.0f);
        }
    }
}

static void dk_draw_debug_menu(const SwitchDebugSettings* settings, u32 fps_x10,
                               u32 frame_ms_x10)
{
    const u32 px = s_dk.width >= 1920 ? 4u : 3u;
    const u32 x = 24u, y = 58u, line = 9u * px;
    const u32 box_w = 220u * px, box_h = 135u * px;
    DkScissor bg = { x, y, box_w, box_h };
    dkCmdBufSetScissors(s_dk.cmd, 0, &bg, 1);
    dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.04f, 0.06f, 0.12f, 1.0f);
    char label[80];
    dk_debug_text(x + 3u * px, y + 3u * px, "DEBUG MENU", px, 0.2f, 1.0f, 0.4f);
    dk_debug_text(x + 3u * px, y + 3u * px + line, "UP DOWN SELECT  LEFT RIGHT CHANGE  A ENTER  B CLOSE", px,
                  0.7f, 0.8f, 1.0f);
    const char* labels[7];
    char scale[80], fps[80], ms[80];
    snprintf(scale, sizeof scale, "RENDER SCALE: %d.%d  ACTIVE: %d.%d%s",
             settings->scale_tenths / 10, settings->scale_tenths % 10,
             settings->active_scale_tenths / 10, settings->active_scale_tenths % 10,
             settings->scale_tenths != settings->active_scale_tenths ? "  REBOOT NEEDED" : "");
    snprintf(fps, sizeof fps, "SHOW FPS: %s", settings->show_fps ? "ON" : "OFF");
    snprintf(ms, sizeof ms, "SHOW FRAME MS: %s", settings->show_frame_ms ? "ON" : "OFF");
    labels[0] = scale; labels[1] = fps; labels[2] = ms;
    labels[3] = "SAVE SCREENSHOT";
    labels[4] = settings->recording ? "STOP SCREEN RECORDING" : "START SCREEN RECORDING";
    labels[5] = "REBOOT APPLICATION"; labels[6] = "CLOSE MENU";
    for (int i = 0; i < 7; i++) {
        u32 yy = y + 3u * px + (u32)(i + 3) * line;
        dk_debug_text(x + 3u * px, yy, i == settings->selected ? ">" : " ", px,
                      0.2f, 1.0f, 0.4f);
        dk_debug_text(x + 8u * px, yy, labels[i], px, 1.0f, 1.0f, 1.0f);
    }
    snprintf(label, sizeof label, "PRESENT FPS: %u.%u  FRAME: %u.%u MS",
             fps_x10 / 10u, fps_x10 % 10u, frame_ms_x10 / 10u, frame_ms_x10 % 10u);
    dk_debug_text(x + 3u * px, y + 3u * px + 11u * line, label, px, 0.2f, 1.0f, 0.4f);
    if (settings->reboot_error) {
        snprintf(label, sizeof label, "REBOOT FAILED: %08X", (unsigned)settings->reboot_error);
        dk_debug_text(x + 3u * px, y + 3u * px + 12u * line, label, px, 1.0f, 0.3f, 0.3f);
    }
}

static void dk_tex_pend_collect(u32 done);   /* deferred texture release, below */
static u32 s_present_serial;
static u8 *s_capture_pixels;
static size_t s_capture_capacity;

static void be_present(void* user, u32 surface)
{
    (void)user;
    if (!s_dk.ready) return;
    /* V3: table occupancy every 900 presents (~30 s), always logged, to see
     * which resource grows until the game turns slow and white. */
    { static u32 presents = 0;
      if ((++presents % 900u) == 0) {
          u32 tex = 0, pipes = 0, views = 0;
          for (u32 i = 0; i < DK_MAX_TEXTURES; i++) tex += s_dk.textures[i].used != 0;
          for (u32 i = 0; i < DK_MAX_PIPES; i++) pipes += s_dk.pipes[i].used != 0;
          for (u32 i = 0; i < DK_MAX_VIEWS; i++) views += s_dk.views[i].used != 0;
          u64 free_bytes = 0; u32 largest = 0;
          for (u32 i = 0; i < s_dk.alloc.count; i++) {
              free_bytes += s_dk.alloc.size[i];
              if (s_dk.alloc.size[i] > largest) largest = s_dk.alloc.size[i];
          }
          dk_err("[res] presents=%u textures=%u/%u pipes=%u/%u views=%u/%u samplers=%u/%u "
                 "image_free=%llu MiB largest=%u MiB extents=%u/%u tex_made=%u tex_fail=%u\n",
                 presents, tex, DK_MAX_TEXTURES, pipes, DK_MAX_PIPES, views, DK_MAX_VIEWS,
                 s_dk.samp_count, DK_SAMP_SLOTS, (unsigned long long)(free_bytes >> 20),
                 largest >> 20, s_dk.alloc.count, DK_MAX_EXTENTS,
                 (unsigned)s_dk.st.tex_made, (unsigned)s_dk.st.tex_fail);
      } }
    const u64 t0 = dk_now_us();
    const int have_src = surface && surface <= DK_MAX_SURFACES &&
                         s_dk.surfaces[surface].used &&
                         s_dk.surfaces[surface].fmt == RSX_BE_FMT_R8G8B8A8;
    if (have_src && dbz3_capture_wants_frame && dbz3_capture_frame &&
        dbz3_capture_wants_frame(t0)) {
        dk_target *t = &s_dk.surfaces[surface];
        const size_t bytes = (size_t)t->logical_w * t->logical_h * 4u;
        if (t->logical_w && t->logical_h && t->w && t->h && bytes <= (16u << 20)) {
            if (bytes > s_capture_capacity) {
                u8 *pixels = (u8 *)realloc(s_capture_pixels, bytes);
                if (pixels) { s_capture_pixels = pixels; s_capture_capacity = bytes; }
            }
            if (s_capture_capacity >= bytes) {
                const u32 pitch = t->logical_w * 4u;
                if ((u64)t->w * t->h * 4u <= DK_STAGING_SIZE) {
                    be_readback(user, surface, 0, 0, t->logical_w, t->logical_h,
                                s_capture_pixels, pitch);
                } else {
                    /* Docked 1080p can exceed the shared 8 MiB staging block. */
                    for (u32 y = 0; y < t->logical_h; y += 128u) {
                        const u32 h = t->logical_h - y < 128u ? t->logical_h - y : 128u;
                        be_readback(user, surface, 0, y, t->logical_w, h,
                                    s_capture_pixels + (size_t)y * pitch, pitch);
                    }
                }
                dbz3_capture_frame(s_capture_pixels, t->logical_w, t->logical_h,
                                   pitch, t0);
            }
        }
    }
    const int slot = dkQueueAcquireImage(s_dk.queue, s_dk.swapchain);
    if (slot < 0 || slot >= DK_NUM_SWAP) { s_dk.st.acquire_fail++; return; }

    DkImageView dst;
    dkImageViewDefaults(&dst, &s_dk.swap_images[slot]);
    DkImageView const* dsts[1] = { &dst };
    if (have_src) {
        dk_target* t = &s_dk.surfaces[surface];
        DkImageView src;
        dkImageViewDefaults(&src, &t->image);
        DkImageRect sr = { 0, 0, 0, t->w, t->h, 1 };
        DkImageRect dr = { 0, 0, 0, s_dk.width, s_dk.height, 1 };
        dk_full_barrier();
        dkCmdBufBlitImage(s_dk.cmd, &src, &sr, &dst, &dr,
                          DkBlitFlag_FilterLinear | DkBlitFlag_ModeBlit, 0);
        dk_full_barrier();
        /* The guest clears with alpha 0; the display layer must be opaque. */
        dkCmdBufBindRenderTargets(s_dk.cmd, dsts, 1, NULL);
        DkScissor sc = { 0, 0, s_dk.width, s_dk.height };
        dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
        dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_A, 0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        dkCmdBufBindRenderTargets(s_dk.cmd, dsts, 1, NULL);
        DkScissor sc = { 0, 0, s_dk.width, s_dk.height };
        dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
        dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    { static u64 last_present, window_start;
      static u32 window_count, fps_x10, frame_ms_x10;
      const u64 now = dk_now_us();
      if (last_present) frame_ms_x10 = (u32)((now - last_present) / 100u);
      last_present = now;
      if (!window_start) window_start = now;
      window_count++;
      if (now - window_start >= 1000000u) {
          fps_x10 = (u32)((u64)window_count * 10000000u / (now - window_start));
          window_count = 0; window_start = now;
      }
      SwitchDebugSettings settings = switch_debug_snapshot();
      if (settings.show_fps) dk_draw_fps_overlay();
      if (settings.show_frame_ms) {
          char ms[32];
          const u32 px = s_dk.width >= 1920 ? 4u : 3u;
          snprintf(ms, sizeof ms, "FRAME %u.%u MS", frame_ms_x10 / 10u, frame_ms_x10 % 10u);
          DkScissor bg = { s_dk.width - 96u * px, 12u * px, 80u * px, 8u * px };
          dkCmdBufSetScissors(s_dk.cmd, 0, &bg, 1);
          dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
          dk_debug_text(bg.x + px, bg.y + px, ms, px, 0.2f, 1.0f, 0.2f);
      }
      if (settings.menu_open) dk_draw_debug_menu(&settings, fps_x10, frame_ms_x10);
    }
    dk_full_barrier();
    s_dk.bound_dirty = 1;
    dk_submit_async();
    /* Frame s_present_serial was just submitted; frames up to serial - 2 are done. */
    if (s_present_serial >= 2) dk_tex_pend_collect(s_present_serial - 2u);
    s_present_serial++;
    dkQueuePresentImage(s_dk.queue, s_dk.swapchain, slot);
    if (s_dk.bound_count || s_dk.bound_depth) dk_rebind_targets();

    s_dk.st.presents++;
    s_dk.st.present_us += dk_now_us() - t0;
    spk_frame_end(dk_now_us());
    if (s_dk.st.presents == 1)
        dk_log("first present: surface %u (%s)\n", surface,
               have_src ? "blit" : "no RGBA8 source, black");
    dk_stats_window();
}

/* ---- descriptors --------------------------------------------------------- */

/* One image descriptor per handle, written when the object is made: a draw
 * then names a texture by its handle and only the sampler is looked up. The
 * descriptor cache is invalidated at the start of every command list, and an
 * object is only ever released after a submit, so a slot is never rewritten
 * under a list that still uses it. */
static void dk_write_image_desc(u32 slot, const DkImage* img, const u8* swz)
{
    if (slot >= DK_DESC_SLOTS) return;
    DkImageView v;
    dkImageViewDefaults(&v, img);
    if (swz)
        for (u32 i = 0; i < 4; i++) v.swizzle[i] = (DkImageSwizzle)swz[i];
    DkImageDescriptor d;
    dkImageDescriptorInitialize(&d, &v, false, false);
    memcpy((u8*)dkMemBlockGetCpuAddr(s_dk.mem_desc) + (size_t)slot * sizeof d, &d, sizeof d);
}

/* TEXTURE_CONTROL1's crossbar as a view swizzle (the Vulkan backend's
 * vk_remap: output r,g,b,a take crossbar fields 1,2,3,0). */
static void dk_swizzle(u32 remap, u32 rsx_fmt, u8 out[4])
{
    u8 sel[4];
    rsx_texture_component_remap(remap, rsx_fmt & 0x9Fu, sel);
    const u8 order[4] = { sel[1], sel[2], sel[3], sel[0] };
    for (u32 i = 0; i < 4; i++)
        out[i] = order[i] <= 3u ? (u8)(DkImageSwizzle_Red + order[i])
               : order[i] == RSX_REMAP_ONE ? (u8)DkImageSwizzle_One
               : (u8)DkImageSwizzle_Zero;
}

static int dk_swizzle_is_identity(const u8 s[4])
{
    return s[0] == DkImageSwizzle_Red && s[1] == DkImageSwizzle_Green &&
           s[2] == DkImageSwizzle_Blue && s[3] == DkImageSwizzle_Alpha;
}

static DkWrapMode dk_wrap(u32 w)
{
    switch (w & 0xFu) {
    case 1:  return DkWrapMode_Repeat;
    case 2:  return DkWrapMode_MirroredRepeat;
    case 4:  return DkWrapMode_ClampToBorder;
    default: return DkWrapMode_ClampToEdge;      /* 3, 5..8, unset */
    }
}

/* A sampler descriptor slot for this unit's state; slot 0 is the default. */
static u32 dk_sampler_slot(const rsx_be_sampler_desc* d)
{
    if (!d) return 0;
    u32 lo, hi;
    memcpy(&lo, &d->min_lod, 4);
    memcpy(&hi, &d->max_lod, 4);
    const u64 key = 1ull | (u64)d->min_linear << 1 | (u64)d->mag_linear << 2 |
                    (u64)d->mip_linear << 3 | (u64)d->mip_present << 4 |
                    (u64)(d->wrap_s & 0xF) << 5 | (u64)(d->wrap_t & 0xF) << 9 |
                    (u64)(d->wrap_r & 0xF) << 13 | (u64)(lo >> 16) << 17 |
                    (u64)(hi >> 16) << 33;
    for (u32 i = 1; i < s_dk.samp_count; i++)
        if (s_dk.samp_keys[i] == key) return i;
    if (s_dk.samp_count >= DK_SAMP_SLOTS) return 0;
    DkSampler s;
    dkSamplerDefaults(&s);
    s.minFilter = d->min_linear ? DkFilter_Linear : DkFilter_Nearest;
    s.magFilter = d->mag_linear ? DkFilter_Linear : DkFilter_Nearest;
    s.mipFilter = !d->mip_present ? DkMipFilter_None
                : d->mip_linear ? DkMipFilter_Linear : DkMipFilter_Nearest;
    s.wrapMode[0] = dk_wrap(d->wrap_s);
    s.wrapMode[1] = dk_wrap(d->wrap_t);
    s.wrapMode[2] = dk_wrap(d->wrap_r);
    s.lodClampMin = d->min_lod;
    s.lodClampMax = d->mip_present ? d->max_lod : d->min_lod;
    if (s.lodClampMax < s.lodClampMin) s.lodClampMax = s.lodClampMin;
    const u32 slot = s_dk.samp_count++;
    DkSamplerDescriptor sd;
    dkSamplerDescriptorInitialize(&sd, &s);
    memcpy((u8*)dkMemBlockGetCpuAddr(s_dk.mem_desc) + DK_SAMP_OFFSET + (size_t)slot * sizeof sd,
           &sd, sizeof sd);
    s_dk.samp_keys[slot] = key;
    return slot;
}

/* The image descriptor slot a bound engine handle samples through, or the
 * dummy's. */
static u32 dk_image_slot(u32 handle, int* is_surface)
{
    *is_surface = 0;
    if (handle >= 1 && handle <= DK_MAX_SURFACES && s_dk.surfaces[handle].used) {
        *is_surface = 1;
        return handle;
    }
    if (handle >= DK_TEX_BASE && handle < DK_TEX_BASE + DK_MAX_TEXTURES &&
        s_dk.textures[handle - DK_TEX_BASE].used)
        return handle;
    if (handle >= DK_VIEW_BASE && handle < DK_VIEW_BASE + DK_MAX_VIEWS &&
        s_dk.views[handle - DK_VIEW_BASE].used) {
        *is_surface = 1;
        return handle;
    }
    return DK_DUMMY_SLOT;
}

/* ---- textures ------------------------------------------------------------ */

static int dk_tex_format(rsx_be_format fmt, DkImageFormat* out, u32* bpp, u32* block)
{
    *block = 0;
    switch (fmt) {
    case RSX_BE_FMT_R8:            *out = DkImageFormat_R8_Unorm;     *bpp = 1;  return 0;
    case RSX_BE_FMT_R8G8:          *out = DkImageFormat_RG8_Unorm;    *bpp = 2;  return 0;
    case RSX_BE_FMT_R8G8B8A8:      *out = DkImageFormat_RGBA8_Unorm;  *bpp = 4;  return 0;
    case RSX_BE_FMT_BC1:           *out = DkImageFormat_RGBA_BC1; *bpp = 0; *block = 8;  return 0;
    case RSX_BE_FMT_BC2:           *out = DkImageFormat_RGBA_BC2; *bpp = 0; *block = 16; return 0;
    case RSX_BE_FMT_BC3:           *out = DkImageFormat_RGBA_BC3; *bpp = 0; *block = 16; return 0;
    case RSX_BE_FMT_R16:           *out = DkImageFormat_R16_Unorm;    *bpp = 2;  return 0;
    case RSX_BE_FMT_R16G16:        *out = DkImageFormat_RG16_Unorm;   *bpp = 4;  return 0;
    case RSX_BE_FMT_R16G16F:       *out = DkImageFormat_RG16_Float;   *bpp = 4;  return 0;
    case RSX_BE_FMT_R16G16B16A16F: *out = DkImageFormat_RGBA16_Float; *bpp = 8;  return 0;
    case RSX_BE_FMT_R32F:          *out = DkImageFormat_R32_Float;    *bpp = 4;  return 0;
    case RSX_BE_FMT_R32G32B32A32F: *out = DkImageFormat_RGBA32_Float; *bpp = 16; return 0;
    default: return -1;
    }
}

static dk_target* dk_tex(u32 handle)
{
    if (handle < DK_TEX_BASE || handle >= DK_TEX_BASE + DK_MAX_TEXTURES) return NULL;
    dk_target* t = &s_dk.textures[handle - DK_TEX_BASE];
    return t->used ? t : NULL;
}

static void dk_tex_pend_flush(void);   /* below, with be_texture_release */
static u32 s_tex_pend_n;

static u32 be_texture_create_once(void* user, rsx_be_format fmt, u32 w, u32 h,
                                  u32 mips, u32 faces, u32 remap, u32 rsx_fmt)
{
    (void)user;
    DkImageFormat dk_fmt;
    u32 bpp, block;
    if (!w || !h || w > 4096 || h > 4096 || dk_tex_format(fmt, &dk_fmt, &bpp, &block)) {
        if (s_dk.st.tex_fail++ < 8)
            dk_err("texture refused: fmt %d %ux%u\n", (int)fmt, w, h);
        return 0;
    }
    if (faces != 1) {                        /* cube maps: not yet */
        if (s_dk.st.tex_fail++ < 8) dk_err("cube texture refused (%ux%u)\n", w, h);
        return 0;
    }
    if (!mips) mips = 1;
    for (u32 i = 0; i < DK_MAX_TEXTURES; i++) {
        dk_target* t = &s_dk.textures[i];
        if (t->used) continue;
        if (dk_image_create(t, dk_fmt, w, h, mips, 0)) {
            if (s_dk.st.tex_fail++ < 8)
                dk_err("texture %ux%u fmt %d: out of image memory\n", w, h, (int)fmt);
            return 0;
        }
        t->fmt = fmt;
        t->bpp = bpp;
        t->block = block;
        t->mips = mips;
        dk_swizzle(remap, rsx_fmt, t->swz);
        dk_write_image_desc(DK_TEX_BASE + i, &t->image, t->swz);
        { static u32 n = 0;   /* T23: what the model textures are */
          if (n++ < 400)
              dk_log("tex %u: %ux%u mips %u be_fmt %d rsx_fmt 0x%02X remap 0x%04X swz %u%u%u%u\n",
                     DK_TEX_BASE + i, w, h, mips, (int)fmt, rsx_fmt, remap,
                     t->swz[0], t->swz[1], t->swz[2], t->swz[3]); }
        s_dk.st.tex_made++;
        s_spk.tex_made++;
        return DK_TEX_BASE + i;
    }
    if (s_dk.st.tex_fail++ < 8) dk_err("texture table full\n");
    return 0;
}

/* No room (table or image memory): release the deferred textures at once
 * (one GPU wait) and try again. */
static u32 be_texture_create(void* user, rsx_be_format fmt, u32 w, u32 h,
                             u32 mips, u32 faces, u32 remap, u32 rsx_fmt)
{
    u32 handle = be_texture_create_once(user, fmt, w, h, mips, faces, remap, rsx_fmt);
    if (!handle && s_tex_pend_n) {
        dk_tex_pend_flush();
        handle = be_texture_create_once(user, fmt, w, h, mips, faces, remap, rsx_fmt);
    }
    return handle;
}

static void be_texture_upload(void* user, u32 texture, u32 face, u32 mip, u32 w,
                              u32 h, const void* src, u32 row_bytes, u32 rows)
{
    (void)user;
    dk_target* t = dk_tex(texture);
    if (!t || !src || !row_bytes || !rows || face || mip >= t->mips || !w || !h) return;
#ifdef DBZ3_DEKO_DIAG
    if (mip == 0 && t->block == 16 && w == 64 && h == 64) {   /* T23: ramp rows */
        static u32 nr = 0;
        if (nr++ < 24) {
            for (u32 by = 0; by < 16 && by < rows; by += 3) {
                char line[400];
                int at = snprintf(line, sizeof line, "ramp tex %u blockrow %u c0:", texture, by);
                for (u32 bx = 0; bx < 16; bx += 2) {
                    const u8* blk = (const u8*)src + (size_t)by * row_bytes + bx * 16u;
                    const u32 c0 = blk[8] | (blk[9] << 8), c1 = blk[10] | (blk[11] << 8);
                    at += snprintf(line + at, sizeof line - at, " %02x%02x%02x/%02x%02x%02x a%02x",
                                   (c0 >> 11) * 255 / 31, ((c0 >> 5) & 63) * 255 / 63, (c0 & 31) * 255 / 31,
                                   (c1 >> 11) * 255 / 31, ((c1 >> 5) & 63) * 255 / 63, (c1 & 31) * 255 / 31,
                                   blk[0]);
                }
                dk_log("%s\n", line);
            }
        }
    }
    if (mip == 0 && t->block == 16) {   /* T23: BC2/BC3 colour endpoints of level 0 */
        static u32 n = 0;
        if (n++ < 200) {
            u64 r = 0, g = 0, b = 0, cnt = 0;
            const u32 bw = (w + 3u) / 4u;
            for (u32 y = 0; y < rows; y++)
                for (u32 x = 0; x < bw; x += 2, cnt++) {
                    const u8* blk = (const u8*)src + (size_t)y * row_bytes + x * 16u + 8u;
                    const u32 c0 = blk[0] | (blk[1] << 8);
                    r += (c0 >> 11) * 255u / 31u; g += ((c0 >> 5) & 63u) * 255u / 63u; b += (c0 & 31u) * 255u / 31u;
                }
            if (cnt) dk_log("tex %u bc level0 avg c0 rgb %u %u %u (alpha bytes %02x%02x)\n", texture,
                            (u32)(r / cnt), (u32)(g / cnt), (u32)(b / cnt),
                            ((const u8*)src)[0], ((const u8*)src)[1]);
        }
    }
    if (mip == 0 && t->bpp == 4 && !t->block) {   /* T23: average texel of level 0 */
        static u32 n = 0;
        if (n++ < 400) {
            u64 sum[4] = {0, 0, 0, 0}; u32 cnt = 0;
            for (u32 y = 0; y < rows; y += 4)
                for (u32 x = 0; x < w; x += 4, cnt++)
                    for (u32 c = 0; c < 4; c++) sum[c] += ((const u8*)src)[(size_t)y * row_bytes + x * 4u + c];
            if (cnt) dk_log("tex %u level0 avg bytes %u %u %u %u\n", texture,
                            (u32)(sum[0] / cnt), (u32)(sum[1] / cnt), (u32)(sum[2] / cnt), (u32)(sum[3] / cnt));
        }
    }
#endif
    /* A BC row is a row of 4x4 blocks; `rows` already counts them. */
    const u32 tight = t->block ? ((w + 3u) / 4u) * t->block : w * t->bpp;
    const u32 px_per_row = t->block ? 4u : 1u;
    u32 per = DK_STAGING_SIZE / tight;
    if (!per) return;
    for (u32 r0 = 0; r0 < rows; r0 += per) {
        const u32 n = rows - r0 < per ? rows - r0 : per;
        const u32 y = r0 * px_per_row;
        if (y >= h) break;
        u32 ph = n * px_per_row;
        if (ph > h - y) ph = h - y;
        /* T43: chunks go to the next free part of the staging buffer and the
         * copy is just recorded; the GPU is waited for only when the buffer is
         * full (a full idle resets it, dk_sets_idle). Before, every chunk of
         * every mip level waited for the GPU twice: 2 x levels full drains per
         * texture, the bulk of the 100-400 ms cutscene hitches. */
        const u32 bytes = tight * n;
        const u32 off = (s_dk.staging_used + 255u) & ~255u;
        if (off > DK_STAGING_SIZE || DK_STAGING_SIZE - off < bytes) {
            if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
            s_dk.staging_used = 0;
        }
        const u32 at = (s_dk.staging_used + 255u) & ~255u;
        u8* dst = (u8*)dkMemBlockGetCpuAddr(s_dk.mem_staging) + at;
        const u32 copy = tight < row_bytes ? tight : row_bytes;
        for (u32 r = 0; r < n; r++)
            memcpy(dst + (size_t)r * tight, (const u8*)src + (size_t)(r0 + r) * row_bytes, copy);
        dkMemBlockFlushCpuCache(s_dk.mem_staging, at, bytes);
        DkImageView view;
        dkImageViewDefaults(&view, &t->image);
        view.mipLevelOffset = mip;
        view.mipLevelCount = 1;
        DkCopyBuf cb = { dkMemBlockGetGpuAddr(s_dk.mem_staging) + at, 0, 0 };
        DkImageRect rect = { 0, y, 0, w, ph, 1 };
        dk_full_barrier();
        dkCmdBufCopyBufferToImage(s_dk.cmd, &cb, &view, &rect, 0);
        dk_full_barrier();
        s_dk.staging_used = at + bytes;
        s_dk.ops++;          /* recorded work: a later "if (ops) submit" sends it */
        if (s_dk.texbatch_off) dk_submit_wait();
    }
    s_dk.st.tex_uploads++;
    s_spk.uploads++;
}

/* Deferred texture release (DBZ3: after the Genkidama the animation jumped).
 * Releasing used to submit and wait for the GPU to go idle, once per texture,
 * in the middle of a frame; with the texture cache now evicting under memory
 * pressure (rsx_draw_engine.c) that could happen several times a frame, the
 * frame stretched and the 60 Hz logic then ran 3-4 steps in one image. Now a
 * released texture keeps its slot and memory until the GPU is done with it:
 * dk_submit_async waits for frame N-2 when it submits frame N, so a texture
 * released while frame F was recorded is freed at the present of F + 2,
 * without waiting. Only when a create finds no room is the queue flushed at
 * once (one wait). */
#define DK_TEX_PEND_MAX 512u
static struct { u32 index, frame; } s_tex_pend[DK_TEX_PEND_MAX];
static u32 s_tex_pend_n;
static u32 s_present_serial;      /* frames presented so far */

static void dk_tex_free_now(u32 index)
{
    dk_target* t = &s_dk.textures[index];
    if (!t->used) return;
    dk_alloc_give(&s_dk.alloc, t->mem_off, t->mem_size);
    memset(t, 0, sizeof *t);
}

/* All queued releases, now: one wait for the GPU. */
static void dk_tex_pend_flush(void)
{
    if (!s_tex_pend_n) return;
    if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
    for (u32 i = 0; i < s_tex_pend_n; i++) dk_tex_free_now(s_tex_pend[i].index);
    s_tex_pend_n = 0;
}

/* After frame `done` is known complete: free what was released up to it. */
static void dk_tex_pend_collect(u32 done)
{
    u32 kept = 0;
    for (u32 i = 0; i < s_tex_pend_n; i++) {
        if (s_tex_pend[i].frame <= done) dk_tex_free_now(s_tex_pend[i].index);
        else s_tex_pend[kept++] = s_tex_pend[i];
    }
    s_tex_pend_n = kept;
}

static void be_texture_release(void* user, u32 texture)
{
    (void)user;
    dk_target* t = dk_tex(texture);
    if (!t) return;
    if (s_tex_pend_n < DK_TEX_PEND_MAX) {
        s_tex_pend[s_tex_pend_n].index = texture - DK_TEX_BASE;
        s_tex_pend[s_tex_pend_n].frame = s_present_serial;
        s_tex_pend_n++;
        return;
    }
    if (s_dk.ops) dk_submit_wait(); else dk_wait_inflight();
    dk_image_free(t);
}

/* A colour target sampled with a crossbar of its own. Identity needs no view:
 * 0 tells the engine to bind the target handle, which samples it as it is. */
static u32 be_surface_view(void* user, u32 surface, u32 remap, u32 rsx_format)
{
    (void)user;
    if (!surface || surface > DK_MAX_SURFACES || !s_dk.surfaces[surface].used) return 0;
    u8 swz[4];
    dk_swizzle(remap, rsx_format, swz);
    if (dk_swizzle_is_identity(swz)) return 0;
    u32 free_slot = DK_MAX_VIEWS;
    for (u32 i = 0; i < DK_MAX_VIEWS; i++) {
        if (!s_dk.views[i].used) { if (free_slot == DK_MAX_VIEWS) free_slot = i; continue; }
        if (s_dk.views[i].surface == surface && !memcmp(s_dk.views[i].swz, swz, 4))
            return DK_VIEW_BASE + i;
    }
    if (free_slot == DK_MAX_VIEWS) return 0;
    s_dk.views[free_slot].used = 1;
    s_dk.views[free_slot].surface = surface;
    memcpy(s_dk.views[free_slot].swz, swz, 4);
    dk_write_image_desc(DK_VIEW_BASE + free_slot, &s_dk.surfaces[surface].image, swz);
    return DK_VIEW_BASE + free_slot;
}

static u32 be_depth_snapshot(void* user, u32 depth, u32 w, u32 h)
{ (void)user; (void)depth; (void)w; (void)h; return 0; }

/* A 2D transfer between two colour targets, on the 2D engine: what the guest
 * does with NV3089 every frame, without the readback/upload round trip. */
static int be_surface_blit(void* user, u32 src, u32 sx, u32 sy, u32 sw, u32 sh,
                           u32 dst, u32 dx, u32 dy, u32 dw, u32 dh)
{
    (void)user;
    if (!s_dk.ready || !src || src > DK_MAX_SURFACES || !dst || dst > DK_MAX_SURFACES ||
        src == dst || !s_dk.surfaces[src].used || !s_dk.surfaces[dst].used)
        return -1;
    const dk_target* S = &s_dk.surfaces[src];
    const dk_target* D = &s_dk.surfaces[dst];
    if (S->fmt != D->fmt || sx + sw > S->logical_w || sy + sh > S->logical_h ||
        dx + dw > D->logical_w || dy + dh > D->logical_h)
        return -1;
    u32 psx, psy, psw, psh, pdx, pdy, pdw, pdh;
    dk_scaled_rect(S, sx, sy, sw, sh, &psx, &psy, &psw, &psh);
    dk_scaled_rect(D, dx, dy, dw, dh, &pdx, &pdy, &pdw, &pdh);
    DkImageView sv, dv;
    dkImageViewDefaults(&sv, &S->image);
    dkImageViewDefaults(&dv, &D->image);
    DkImageRect sr = { psx, psy, 0, psw, psh, 1 };
    DkImageRect dr = { pdx, pdy, 0, pdw, pdh, 1 };
    dk_full_barrier();
    dkCmdBufBlitImage(s_dk.cmd, &sv, &sr, &dv, &dr, DkBlitFlag_ModeBlit, 0);
    dk_full_barrier();
    s_dk.st.blits++;
    dk_note_op();
    return 0;
}

/* ---- shaders and pipelines ----------------------------------------------- */

/* The precompiled module for this HLSL, loaded into code memory on first
 * use. Returns its table index, or -1 when scripts/gen-deko3d-shaders.py has
 * not been run on it yet. */
static int dk_shader_get(u64 hash, int stage)
{
    for (u32 i = 0; i < RSX_DK_SHADER_COUNT; i++) {
        const rsx_dk_shader_blob* b = &rsx_dk_shader_table[i];
        if (b->hash != hash || b->stage != stage) continue;
        if (s_dk.shader_loaded[i]) return (int)i;
        u32 hdr[6];
        memcpy(hdr, b->dksh, sizeof hdr);
        const u32 control_sz = hdr[2], code_sz = hdr[3];
        const u32 off = (s_dk.code_used + DK_SHADER_CODE_ALIGNMENT - 1u) &
                        ~(DK_SHADER_CODE_ALIGNMENT - 1u);
        if (control_sz + code_sz > b->size ||
            off + code_sz > DK_CODE_SIZE - DK_SHADER_CODE_UNUSABLE_SIZE) {
            dk_log("shader %016llx: does not fit (%u + %u)\n",
                   (unsigned long long)hash, off, code_sz);
            return -1;
        }
        memcpy((u8*)dkMemBlockGetCpuAddr(s_dk.mem_code) + off, b->dksh + control_sz, code_sz);
        DkShaderMaker m;
        dkShaderMakerDefaults(&m, s_dk.mem_code, off);
        m.control = b->dksh;
        m.programId = 0;
        dkShaderInitialize(&s_dk.shaders[i], &m);
        s_dk.code_used = off + code_sz;
        s_dk.shader_loaded[i] = 1;
        return (int)i;
    }
    return -1;
}

static DkBlendFactor dk_blend_factor(u32 f)
{
    switch (f) {
    case 0x0000: return DkBlendFactor_Zero;
    case 0x0001: return DkBlendFactor_One;
    case 0x0300: return DkBlendFactor_SrcColor;
    case 0x0301: return DkBlendFactor_InvSrcColor;
    case 0x0302: return DkBlendFactor_SrcAlpha;
    case 0x0303: return DkBlendFactor_InvSrcAlpha;
    case 0x0304: return DkBlendFactor_DstAlpha;
    case 0x0305: return DkBlendFactor_InvDstAlpha;
    case 0x0306: return DkBlendFactor_DstColor;
    case 0x0307: return DkBlendFactor_InvDstColor;
    case 0x0308: return DkBlendFactor_SrcAlphaSaturate;
    default:     return DkBlendFactor_One;
    }
}

static DkBlendOp dk_blend_op(u32 e)
{
    switch (e) {
    case 0x8007: return DkBlendOp_Min;
    case 0x8008: return DkBlendOp_Max;
    case 0x800A: return DkBlendOp_Sub;
    case 0x800B: return DkBlendOp_RevSub;
    default:     return DkBlendOp_Add;                    /* FUNC_ADD 0x8006 */
    }
}

/* GL NEVER..ALWAYS (0x0200..0x0207) -> DkCompareOp_Never..Always (1..8). */
static DkCompareOp dk_compare(u32 f)
{
    return (f >= 0x0200u && f <= 0x0207u) ? (DkCompareOp)(f - 0x0200u + 1u) : DkCompareOp_Always;
}

static DkStencilOp dk_stencil_op(u32 op)
{
    switch (op) {
    case 0x0000: return DkStencilOp_Zero;
    case 0x1E01: return DkStencilOp_Replace;
    case 0x1E02: return DkStencilOp_Incr;
    case 0x1E03: return DkStencilOp_Decr;
    case 0x150A: return DkStencilOp_Invert;
    case 0x8507: return DkStencilOp_IncrWrap;
    case 0x8508: return DkStencilOp_DecrWrap;
    default:     return DkStencilOp_Keep;                 /* GL_KEEP 0x1E00 */
    }
}

static u32 be_pipeline_create(void* user, const char* vs_hlsl, const char* ps_hlsl,
                              const rsx_be_render_state* rs,
                              const rsx_vertex_layout_plan* layout,
                              u32 vertex_stride, rsx_be_format rt_fmt, u32 rt_count)
{
    (void)user;
    if (!vs_hlsl || !ps_hlsl || !rs || !layout || !vertex_stride) return 0;
    const u64 hv = dk_hash_str(vs_hlsl), hp = dk_hash_str(ps_hlsl);
    const int vs = dk_shader_get(hv, 0);
    const int ps = dk_shader_get(hp, 1);
    DkImageFormat cf;
    u32 cbpp;
    if (vs < 0 || ps < 0 || dk_format(rt_fmt, &cf, &cbpp)) {
        s_dk.st.pipelines_refused++;
        dk_err("pipeline refused: vs %016llx%s ps %016llx%s rt_fmt %d -- "
               "copy t23-shaders/*.hlsl into libs/video/deko3d/hlsl and run "
               "scripts/gen-deko3d-shaders.py\n",
               (unsigned long long)hv, vs < 0 ? " (missing)" : "",
               (unsigned long long)hp, ps < 0 ? " (missing)" : "", (int)rt_fmt);
        dk_dump_pipeline(vs_hlsl, ps_hlsl, rs, layout, vertex_stride, rt_fmt, rt_count);
        return 0;
    }
    u32 slot = DK_MAX_PIPES;
    for (u32 i = 0; i < DK_MAX_PIPES; i++) if (!s_dk.pipes[i].used) { slot = i; break; }
    if (slot == DK_MAX_PIPES) { dk_err("pipeline table full\n"); return 0; }
    dk_pipe* P = &s_dk.pipes[slot];
    memset(P, 0, sizeof *P);
    if (!rt_count) rt_count = 1;
    if (rt_count > RSX_BE_MAX_COLOR_TARGETS) rt_count = RSX_BE_MAX_COLOR_TARGETS;
    P->vs = (u32)vs;
    P->ps = (u32)ps;
    P->stride = vertex_stride;
    P->rt_count = rt_count;
    P->rt_fmt = rt_fmt;

    /* Vertex input: slot s of the packed float4 vertex is the shader's ATTRn
     * input, location n; any location below the highest that the layout does
     * not feed reads a constant. */
    u32 nattr = 0;
    for (u32 s = 0; s < layout->count; s++)
        if (layout->attrs[s] + 1u > nattr) nattr = layout->attrs[s] + 1u;
    if (nattr > 16) nattr = 16;
    for (u32 loc = 0; loc < nattr; loc++) {
        DkVtxAttribState* a = &P->attr[loc];
        memset(a, 0, sizeof *a);
        a->isFixed = 1;
        a->size = DkVtxAttribSize_4x32;
        a->type = DkVtxAttribType_Float;
    }
    for (u32 s = 0; s < layout->count; s++) {
        const u32 loc = layout->attrs[s];
        if (loc >= 16) continue;
        P->attr[loc].isFixed = 0;
        P->attr[loc].bufferId = 0;
        P->attr[loc].offset = s * 16u;
    }
    P->nattr = nattr;

    /* The NV4097 state, decoded the way the Vulkan backend's vk_eng_variant
     * does. CULL_FACE FRONT 0x0404 / BACK 0x0405 / FRONT_AND_BACK 0x0408;
     * FRONT_FACE CCW 0x0901. deko3d's default clip space is D3D's (y up,
     * depth 0..1, upper-left window origin), so the winding needs no flip. */
    dkRasterizerStateDefaults(&P->ras);
    P->ras.cullMode = !rs->cull_enable ? DkFace_None
                    : rs->cull_face == 0x0404u ? DkFace_Front
                    : rs->cull_face == 0x0408u ? DkFace_FrontAndBack : DkFace_Back;
    P->ras.frontFace = rs->front_face == 0x0901u ? DkFrontFace_CCW : DkFrontFace_CW;

    dkColorStateDefaults(&P->color);
    P->color.blendEnableMask = rs->blend_enable ? (1u << rt_count) - 1u : 0u;

    /* nv40 COLOR_MASK: B [0:7], G [8:15], R [16:23], A [24:31]. */
    u32 wm = 0;
    if ((rs->color_mask >> 16) & 0xFFu) wm |= DkColorMask_R;
    if ((rs->color_mask >>  8) & 0xFFu) wm |= DkColorMask_G;
    if ((rs->color_mask >>  0) & 0xFFu) wm |= DkColorMask_B;
    if ((rs->color_mask >> 24) & 0xFFu) wm |= DkColorMask_A;
    dkColorWriteStateDefaults(&P->cwrite);
    for (u32 r = 0; r < rt_count; r++) dkColorWriteStateSetMask(&P->cwrite, r, wm);

    dkBlendStateDefaults(&P->blend);
    P->blend.colorBlendOp = dk_blend_op(rs->eq_rgb);
    P->blend.alphaBlendOp = dk_blend_op(rs->eq_a);
    P->blend.srcColorBlendFactor = dk_blend_factor(rs->sf_rgb);
    P->blend.dstColorBlendFactor = dk_blend_factor(rs->df_rgb);
    P->blend.srcAlphaBlendFactor = dk_blend_factor(rs->sf_a);
    P->blend.dstAlphaBlendFactor = dk_blend_factor(rs->df_a);

    dkDepthStencilStateDefaults(&P->ds);
    P->ds.depthTestEnable = rs->depth_test ? 1 : 0;
    P->ds.depthWriteEnable = (rs->depth_test && rs->depth_write) ? 1 : 0;
    P->ds.depthCompareOp = dk_compare(rs->depth_func);
    P->ds.stencilTestEnable = rs->stencil_enable ? 1 : 0;
    P->ds.stencilFrontFailOp = dk_stencil_op(rs->s_fail);
    P->ds.stencilFrontDepthFailOp = dk_stencil_op(rs->s_zfail);
    P->ds.stencilFrontPassOp = dk_stencil_op(rs->s_zpass);
    P->ds.stencilFrontCompareOp = dk_compare(rs->s_func);
    if (rs->stencil_two_sided) {
        P->ds.stencilBackFailOp = dk_stencil_op(rs->bs_fail);
        P->ds.stencilBackDepthFailOp = dk_stencil_op(rs->bs_zfail);
        P->ds.stencilBackPassOp = dk_stencil_op(rs->bs_zpass);
        P->ds.stencilBackCompareOp = dk_compare(rs->bs_func);
    } else {
        P->ds.stencilBackFailOp = P->ds.stencilFrontFailOp;
        P->ds.stencilBackDepthFailOp = P->ds.stencilFrontDepthFailOp;
        P->ds.stencilBackPassOp = P->ds.stencilFrontPassOp;
        P->ds.stencilBackCompareOp = P->ds.stencilFrontCompareOp;
    }
    P->s_write_mask = (u8)(rs->s_write_mask & 0xFFu);
    P->s_func_mask = (u8)(rs->s_func_mask & 0xFFu);
    P->depth_test = rs->depth_test;
    P->vs_hash = hv;
    P->ps_hash = hp;
    P->used = 1;
    s_dk.st.pipelines_made++;
    s_spk.pipes++;
    /* Pair pass-dump pipeline IDs with the precompiled shader sources. */
    {
        static FILE* map;
        static int opened;
        if (!opened) {
            opened = 1;
            const char* dir = getenv("RSX_ENGINE_PASS_DUMP");
            if (dir && *dir) {
                char path[512];
                snprintf(path, sizeof path, "%s/pipelines-deko.txt", dir);
                map = fopen(path, "w");
            }
        }
        if (map) {
            fprintf(map, "%u vs=%016llx ps=%016llx stride=%u rt=%d x%u\n",
                    slot + 1u, (unsigned long long)hv, (unsigned long long)hp,
                    vertex_stride, (int)rt_fmt, rt_count);
            fflush(map);
        }
    }
    if (s_dk.st.pipelines_made <= 32)
        dk_log("pipeline %u: vs %016llx ps %016llx stride %u attrs %u rt %d x%u "
               "blend %u depth %u/%u cull %u\n", slot + 1u,
               (unsigned long long)hv, (unsigned long long)hp, vertex_stride,
               layout->count, (int)rt_fmt, rt_count, rs->blend_enable,
               rs->depth_test, rs->depth_write, rs->cull_enable);
    return slot + 1u;
}

static void be_pipeline_release(void* user, u32 pipeline)
{
    (void)user;
    if (!pipeline || pipeline > DK_MAX_PIPES) return;
    s_dk.pipes[pipeline - 1].used = 0;
    if (s_dk.recorded.pipeline_valid && s_dk.recorded.pipeline == pipeline)
        s_dk.recorded.pipeline_valid = 0;
    if (s_dk.draw.pipe == pipeline) s_dk.draw.pipe = 0;
}

/* ---- per-draw binding ---------------------------------------------------- */

/* The engine's constant buffers live until its draw call returns, so binding
 * only remembers them; the draw copies them into the ring together with the
 * vertices, after making room for everything at once. */
static void be_bind_pipeline(void* user, u32 pipeline) { (void)user; s_dk.draw.pipe = pipeline; }
static void be_bind_vs_constants(void* user, const void* data, u32 bytes)
{ (void)user; s_dk.draw.vs_data = data; s_dk.draw.vs_bytes = data ? bytes : 0; }
static void be_bind_ps_constants(void* user, const void* data, u32 bytes)
{ (void)user; s_dk.draw.ps_data = data; s_dk.draw.ps_bytes = data ? bytes : 0; }
static void be_bind_textures(void* user, const u32* textures,
                             const rsx_be_sampler_desc* samplers, u32 mask)
{
    (void)user;
    for (u32 u = 0; u < RSX_BE_MAX_TEXTURES && u < 16; u++) {
        const int on = (mask >> u) & 1u;
        s_dk.draw.tex[u] = (on && textures) ? textures[u] : 0;
        if (on && samplers) s_dk.draw.samp[u] = samplers[u];
    }
    s_dk.draw.tex_mask = mask;
}
static void be_bind_vertex_textures(void* user, const u32* textures,
                                    const rsx_be_sampler_desc* samplers, u32 mask)
{
    (void)user; (void)textures; (void)samplers;
    if (mask && !(s_dk.warned & 1u)) {
        dk_log("vertex textures are not supported yet\n");
        s_dk.warned |= 1u;
    }
}
static void be_set_stencil_ref(void* user, u32 ref) { (void)user; s_dk.draw.stencil_ref = ref; }

static int dk_ring_alloc(u32 size, u32 align, u32* off)
{
    const u32 o = (s_dk.ring_used + align - 1u) & ~(align - 1u);
    if (o > DK_RING_SIZE || DK_RING_SIZE - o < size) return -1;
    s_dk.ring_used = o + size;
    *off = o;
    return 0;
}

static DkPrimitive dk_prim(rsx_topology t)
{
    switch (t) {
    case RSX_TOPOLOGY_POINTS:         return DkPrimitive_Points;
    case RSX_TOPOLOGY_LINES:          return DkPrimitive_Lines;
    case RSX_TOPOLOGY_LINE_STRIP:     return DkPrimitive_LineStrip;
    case RSX_TOPOLOGY_TRIANGLE_STRIP: return DkPrimitive_TriangleStrip;
    default:                          return DkPrimitive_Triangles;
    }
}

static void dk_skip(u32 bit, const char* why)
{
    s_dk.st.draws_skipped++;
    if (!(s_dk.warned & bit)) {
        dk_log("draw skipped: %s\n", why);
        s_dk.warned |= bit;
    }
}

static void be_draw(void* user, rsx_topology topology, const void* vertices,
                    u32 vertex_count, u32 stride, const u32* indices, u32 index_count)
{
    (void)user;
    if (!s_dk.ready || !vertices || !vertex_count || !stride) return;
    if (topology < RSX_TOPOLOGY_POINTS || topology > RSX_TOPOLOGY_TRIANGLE_STRIP) return;
    const u32 pipe = s_dk.draw.pipe;
    if (!pipe || pipe > DK_MAX_PIPES || !s_dk.pipes[pipe - 1].used) {
        dk_skip(2u, "no pipeline bound");
        return;
    }
    const dk_pipe* P = &s_dk.pipes[pipe - 1];
    if (stride != P->stride) { dk_skip(4u, "vertex stride differs from the pipeline's"); return; }

    /* Targets: every member of a set is target A's size and format. */
    if (s_dk.bound_count != P->rt_count) {
        dk_skip(8u, "colour target count differs from the pipeline's");
        return;
    }
    const u32 rt0 = s_dk.bound_color[0];
    if (!rt0 || rt0 > DK_MAX_SURFACES || !s_dk.surfaces[rt0].used ||
        s_dk.surfaces[rt0].fmt != P->rt_fmt) {
        dk_skip(16u, "colour target missing or of another format");
        return;
    }
    const dk_target* T = &s_dk.surfaces[rt0];

    /* Room for vertices, indices and both constant blocks, in one go: a
     * submit in between would recycle the ring under the earlier copies. */
    const u32 vbytes = vertex_count * stride;
    const u32 ibytes = (indices && index_count) ? index_count * 4u : 0u;
    const u32 vsb = s_dk.draw.vs_bytes ? s_dk.draw.vs_bytes : 16u;
    const u32 psb = s_dk.draw.ps_bytes ? s_dk.draw.ps_bytes : 16u;
    const u32 need = vbytes + ibytes + vsb + psb + 4u * DK_UNIFORM_BUF_ALIGNMENT;
    if (need > DK_RING_SIZE) { dk_skip(32u, "draw larger than the data ring"); return; }
    if (DK_RING_SIZE - s_dk.ring_used < need) dk_submit_wait();
    u32 voff, ioff = 0, vsoff, psoff;
    if (dk_ring_alloc(vbytes, 16u, &voff) ||
        (ibytes && dk_ring_alloc(ibytes, 16u, &ioff)) ||
        dk_ring_alloc(vsb, DK_UNIFORM_BUF_ALIGNMENT, &vsoff) ||
        dk_ring_alloc(psb, DK_UNIFORM_BUF_ALIGNMENT, &psoff)) {
        dk_skip(32u, "data ring exhausted");
        return;
    }
    u8* cpu = (u8*)dkMemBlockGetCpuAddr(s_dk.mem_ring);
    const DkGpuAddr gpu = dkMemBlockGetGpuAddr(s_dk.mem_ring);
    memcpy(cpu + voff, vertices, vbytes);
    if (ibytes) memcpy(cpu + ioff, indices, ibytes);
    if (s_dk.draw.vs_bytes) memcpy(cpu + vsoff, s_dk.draw.vs_data, s_dk.draw.vs_bytes);
    else memset(cpu + vsoff, 0, 16);
    if (s_dk.draw.ps_bytes) memcpy(cpu + psoff, s_dk.draw.ps_data, s_dk.draw.ps_bytes);
    else memset(cpu + psoff, 0, 16);

    /* Texture handles; a unit bound to a render target needs the earlier
     * draws into it finished and the texture cache dropped. */
    DkResHandle th[16];
    int samples_target = 0;
    for (u32 u = 0; u < 16; u++) {
        int is_surface = 0;
        const int on = (s_dk.draw.tex_mask >> u) & 1u;
        const u32 img = on ? dk_image_slot(s_dk.draw.tex[u], &is_surface) : DK_DUMMY_SLOT;
        const u32 smp = on ? dk_sampler_slot(&s_dk.draw.samp[u]) : 0u;
        if (on && img == DK_DUMMY_SLOT) s_dk.st.tex_unbound++;
        samples_target |= is_surface;
        th[u] = dkMakeTextureHandle(img, smp);
    }
    if (samples_target)
        dkCmdBufBarrier(s_dk.cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
#ifdef DBZ3_DEKO_DIAG
    { static u32 n = 0;   /* T23: which textures the model draws sample */
      if (P->vs_hash == 0x3c48ee42ce65b498ull || P->vs_hash == 0x955f3911a36b6d89ull ||
          P->vs_hash == 0x05bf97857ab9760aull) {
          if (n++ < 60)
              dk_log("model draw vs %016llx ps %016llx texmask %04x t0 %u t1 %u t2 %u\n",
                     (unsigned long long)P->vs_hash, (unsigned long long)P->ps_hash,
                     s_dk.draw.tex_mask, s_dk.draw.tex[0], s_dk.draw.tex[1], s_dk.draw.tex[2]);
      }
      /* T23 (V1, white skin): the body shader's fragment constants per draw.
       * c3.x picks the ramp row and c5.y offsets the ramp column; the game
       * patches them into the program for each material. */
      static u32 m = 0, mv = 0;
      /* T28: the rim term's vertex constants (TEXCOORD2.w = flag * c304.x),
       * sampled over the whole session, always written (dk_err). */
      if (P->ps_hash == 0x511e3e7252f5f9dcull && s_dk.draw.vs_data &&
          s_dk.draw.vs_bytes >= 468u * 16u && (mv++ % 300u) == 0u && mv < 300u * 150u) {
          const float* v = (const float*)s_dk.draw.vs_data;
          static const u32 k[6] = { 292, 293, 304, 466, 467, 465 };
          char line[400];
          int at = snprintf(line, sizeof line, "t28 body vs %016llx t0 %u t1 %u",
                            (unsigned long long)P->vs_hash, s_dk.draw.tex[0], s_dk.draw.tex[1]);
          for (u32 i = 0; i < 6; i++)
              at += snprintf(line + at, sizeof line - at, " c%u(%g %g %g %g)", k[i],
                             v[k[i] * 4], v[k[i] * 4 + 1], v[k[i] * 4 + 2], v[k[i] * 4 + 3]);
          dk_err("%s\n", line);
      }
      if (P->ps_hash == 0x511e3e7252f5f9dcull && s_dk.draw.ps_data && m++ < 80) {
          const float* c = (const float*)s_dk.draw.ps_data;
          const u32 nvec = s_dk.draw.ps_bytes / 16u;
          char line[640];
          int at = snprintf(line, sizeof line, "body fp t0 %u t1 %u consts", s_dk.draw.tex[0],
                            s_dk.draw.tex[1]);
          for (u32 v = 0; v < nvec && v < 11u && at < (int)sizeof line - 48; v++)
              at += snprintf(line + at, sizeof line - at, " c%u(%.3f %.3f %.3f %.3f)", v,
                             c[v * 4], c[v * 4 + 1], c[v * 4 + 2], c[v * 4 + 3]);
          dk_log("%s\n", line);
          if (m <= 12) {
              const rsx_be_sampler_desc* a = &s_dk.draw.samp[0];
              const rsx_be_sampler_desc* b = &s_dk.draw.samp[1];
              dk_log("body samp0 min%u mag%u mip%u/%u wrap %u %u %u | samp1 min%u mag%u mip%u/%u wrap %u %u %u lod %.2f..%.2f\n",
                     a->min_linear, a->mag_linear, a->mip_linear, a->mip_present,
                     a->wrap_s, a->wrap_t, a->wrap_r,
                     b->min_linear, b->mag_linear, b->mip_linear, b->mip_present,
                     b->wrap_s, b->wrap_t, b->wrap_r, b->min_lod, b->max_lod);
          }
      } }
#endif

    if (s_dk.bound_dirty) {
        dk_rebind_targets();
        s_dk.recorded.scissor_valid = 0;
    }

    DkCmdBuf cb = s_dk.cmd;
    const int pipeline_changed = !s_dk.recorded.pipeline_valid || s_dk.recorded.pipeline != pipe;
    if (pipeline_changed) {
        DkShader const* sh[2] = { &s_dk.shaders[P->vs], &s_dk.shaders[P->ps] };
        dkCmdBufBindShaders(cb, DkStageFlag_GraphicsMask, sh, 2);
        dkCmdBufBindRasterizerState(cb, &P->ras);
        dkCmdBufBindColorState(cb, &P->color);
        dkCmdBufBindColorWriteState(cb, &P->cwrite);
        for (u32 r = 0; r < P->rt_count; r++) dkCmdBufBindBlendState(cb, r, &P->blend);
        dkCmdBufBindDepthStencilState(cb, &P->ds);
        dkCmdBufBindVtxAttribState(cb, P->attr, P->nattr);
        s_dk.recorded.pipeline = pipe;
        s_dk.recorded.pipeline_valid = 1;
    }
    if (pipeline_changed || s_dk.recorded.vertex_stride != stride) {
        DkVtxBufferState vbs = { stride, 0 };
        dkCmdBufBindVtxBufferState(cb, &vbs, 1);
        s_dk.recorded.vertex_stride = stride;
    }
    if (P->ds.stencilTestEnable &&
        (pipeline_changed || s_dk.recorded.stencil_ref != s_dk.draw.stencil_ref))
        dkCmdBufSetStencil(cb, DkFace_FrontAndBack, P->s_write_mask,
                           (u8)(s_dk.draw.stencil_ref & 0xFFu), P->s_func_mask);
    s_dk.recorded.stencil_ref = s_dk.draw.stencil_ref;
    dkCmdBufBindUniformBuffer(cb, DkStage_Vertex, 0, gpu + vsoff,
                              (vsb + 15u) & ~15u);
    dkCmdBufBindUniformBuffer(cb, DkStage_Fragment, 1, gpu + psoff,
                              (psb + 15u) & ~15u);
    if (!s_dk.recorded.textures_valid || memcmp(s_dk.recorded.textures, th, sizeof th)) {
        dkCmdBufBindTextures(cb, DkStage_Fragment, 0, th, 16);
        memcpy(s_dk.recorded.textures, th, sizeof th);
        s_dk.recorded.textures_valid = 1;
    }
    dkCmdBufBindVtxBuffer(cb, 0, gpu + voff, vbytes);

    DkViewport vp = { 0.0f, 0.0f, (float)T->w, (float)T->h, 0.0f, 1.0f };
    if (s_dk.draw.have_vp && s_dk.draw.vp[2] > 0.0f && s_dk.draw.vp[3] > 0.0f) {
        const float sx = (float)T->w / (float)T->logical_w;
        const float sy = (float)T->h / (float)T->logical_h;
        vp.x = s_dk.draw.vp[0] * sx; vp.y = s_dk.draw.vp[1] * sy;
        vp.width = s_dk.draw.vp[2] * sx; vp.height = s_dk.draw.vp[3] * sy;
    }
    if (!s_dk.recorded.viewport_valid || memcmp(&s_dk.recorded.viewport, &vp, sizeof vp)) {
        dkCmdBufSetViewports(cb, 0, &vp, 1);
        s_dk.recorded.viewport = vp;
        s_dk.recorded.viewport_valid = 1;
    }
    DkScissor sc = { 0, 0, T->w, T->h };
    if (s_dk.draw.have_sc) {
        u32 x0, y0, rw, rh;
        dk_scaled_rect(T, s_dk.draw.sc[0], s_dk.draw.sc[1],
                       s_dk.draw.sc[2], s_dk.draw.sc[3], &x0, &y0, &rw, &rh);
        u32 x1 = x0 + rw, y1 = y0 + rh;
        sc.x = x0; sc.y = y0;
        sc.width = x1 > x0 ? x1 - x0 : 0;
        sc.height = y1 > y0 ? y1 - y0 : 0;
    }
    if (!s_dk.recorded.scissor_valid || memcmp(&s_dk.recorded.scissor, &sc, sizeof sc)) {
        dkCmdBufSetScissors(cb, 0, &sc, 1);
        s_dk.recorded.scissor = sc;
        s_dk.recorded.scissor_valid = 1;
    }

    const DkPrimitive prim = dk_prim(topology);
    if (ibytes) {
        dkCmdBufBindIdxBuffer(cb, DkIdxFormat_Uint32, gpu + ioff);
        dkCmdBufDrawIndexed(cb, prim, index_count, 1, 0, 0, 0);
    } else {
        dkCmdBufDraw(cb, prim, vertex_count, 1, 0, 0);
    }
    s_dk.st.gpu_draws++;
    dk_note_op();
}

/* ---- lifecycle ----------------------------------------------------------- */

static void be_submit_and_wait(void* user, u32 reason)
{
    (void)user; (void)reason;
    if (s_dk.ready && s_dk.ops) dk_submit_wait();
}

static int be_init(void* user, u32 width, u32 height)
{
    (void)user; (void)width; (void)height;
    return 0;                         /* the device is up before the engine */
}

static void be_shutdown(void* user) { (void)user; }

static const rsx_draw_backend s_dk_engine_backend = {
    .user = NULL,
    .init = be_init,
    .shutdown = be_shutdown,
    .submit_and_wait = be_submit_and_wait,
    .texture_create = be_texture_create,
    .texture_upload = be_texture_upload,
    .texture_release = be_texture_release,
    .color_target_create = be_color_target_create,
    .color_target_release = be_color_target_release,
    .color_target_write = be_color_target_write,
    .surface_view = be_surface_view,
    .depth_target_create = be_depth_target_create,
    .depth_target_release = be_depth_target_release,
    .depth_snapshot = be_depth_snapshot,
    .pipeline_create = be_pipeline_create,
    .pipeline_release = be_pipeline_release,
    .bind_targets = be_bind_targets,
    .bind_pipeline = be_bind_pipeline,
    .bind_vs_constants = be_bind_vs_constants,
    .bind_ps_constants = be_bind_ps_constants,
    .bind_textures = be_bind_textures,
    .bind_vertex_textures = be_bind_vertex_textures,
    .set_viewport = be_set_viewport,
    .set_scissor = be_set_scissor,
    .set_stencil_ref = be_set_stencil_ref,
    .draw = be_draw,
    .clear_color = be_clear_color,
    .clear_depth_stencil = be_clear_depth_stencil,
    .present = be_present,
    .readback = be_readback,
    .depth_readback = NULL,
    .surface_blit = be_surface_blit,
};

static DkMemBlock dk_block(u32 size, u32 flags)
{
    DkMemBlockMaker mm;
    dkMemBlockMakerDefaults(&mm, s_dk.device, size);
    mm.flags = flags;
    return dkMemBlockCreate(&mm);
}

static void dk_destroy_all(void)
{
    if (s_dk.queue) dkQueueWaitIdle(s_dk.queue);
    if (s_dk.swapchain)     dkSwapchainDestroy(s_dk.swapchain);
    if (s_dk.cmd)           dkCmdBufDestroy(s_dk.cmd);
    if (s_dk.queue)         dkQueueDestroy(s_dk.queue);
    if (s_dk.mem_swap)      dkMemBlockDestroy(s_dk.mem_swap);
    if (s_dk.mem_desc)      dkMemBlockDestroy(s_dk.mem_desc);
    if (s_dk.mem_ring)      dkMemBlockDestroy(s_dk.mem_ring);
    if (s_dk.mem_code)      dkMemBlockDestroy(s_dk.mem_code);
    if (s_dk.mem_staging)   dkMemBlockDestroy(s_dk.mem_staging);
    if (s_dk.mem_cmd_spare) dkMemBlockDestroy(s_dk.mem_cmd_spare);
    if (s_dk.mem_cmd)       dkMemBlockDestroy(s_dk.mem_cmd);
    if (s_dk.mem_images)    dkMemBlockDestroy(s_dk.mem_images);
    if (s_dk.device)        dkDeviceDestroy(s_dk.device);
    memset(&s_dk, 0, sizeof s_dk);
}

static int dk_init_device(u32 width, u32 height)
{
    switch_debug_init();
    s_dk.width = width;
    s_dk.height = height;

    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = dk_debug_cb;
    s_dk.device = dkDeviceCreate(&dm);
    if (!s_dk.device) { dk_log("dkDeviceCreate failed\n"); return -1; }

    s_dk.mem_images  = dk_block(DK_IMAGE_POOL, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    s_dk.mem_cmd     = dk_block(DK_CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_dk.mem_cmd_spare = dk_block(DK_CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_dk.mem_staging = dk_block(DK_STAGING_SIZE, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached);
    if (!s_dk.mem_images || !s_dk.mem_cmd || !s_dk.mem_cmd_spare || !s_dk.mem_staging) {
        dk_log("memory blocks: images=%p cmd=%p spare=%p staging=%p (out of heap?)\n",
               (void*)s_dk.mem_images, (void*)s_dk.mem_cmd,
               (void*)s_dk.mem_cmd_spare, (void*)s_dk.mem_staging);
        return -1;
    }
    dk_alloc_init(&s_dk.alloc, DK_IMAGE_POOL);
    s_dk.mem_code = dk_block(DK_CODE_SIZE, DkMemBlockFlags_CpuUncached |
                             DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    s_dk.mem_ring = dk_block(DK_RING_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_dk.set_ring[0] = s_dk.mem_ring;
    s_dk.set_cmd[0] = s_dk.mem_cmd;
    s_dk.set_cmd_spare[0] = s_dk.mem_cmd_spare;
    s_dk.set_ring[1] = dk_block(DK_RING_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_dk.set_cmd[1] = dk_block(DK_CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    s_dk.set_cmd_spare[1] = dk_block(DK_CMD_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!s_dk.set_ring[1] || !s_dk.set_cmd[1] || !s_dk.set_cmd_spare[1]) {
        dk_log("second frame set unavailable: presents stay synchronous\n");
        s_dk.set_ring[1] = NULL;
    }
    { FILE* ps = fopen("sdmc:/switch/dbz-budokai-3-switch/present-sync", "r");
      s_dk.async_present = !ps;
      if (ps) fclose(ps); }
    /* A/B switches for the T41/T43 changes: texbatch-off = one GPU wait per
     * texture chunk again, flush512 = mid-frame submit every 512 ops again. */
    { FILE* f = fopen("sdmc:/switch/dbz-budokai-3-switch/texbatch-off", "r");
      s_dk.texbatch_off = f != NULL; if (f) fclose(f); }
    { FILE* f = fopen("sdmc:/switch/dbz-budokai-3-switch/flush512", "r");
      s_dk.flush_ops = f ? 512u : DK_FLUSH_OPS; if (f) fclose(f); }
    dk_log("texbatch %s, flush every %u ops\n", s_dk.texbatch_off ? "off" : "on", s_dk.flush_ops);
    dk_log("present: %s\n", s_dk.async_present && s_dk.set_ring[1]
                              ? "asynchronous (2 frame sets)" : "synchronous");
    s_dk.mem_desc = dk_block(DK_DESC_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    if (!s_dk.mem_code || !s_dk.mem_ring || !s_dk.mem_desc) {
        dk_log("memory blocks: code=%p ring=%p desc=%p (out of heap?)\n",
               (void*)s_dk.mem_code, (void*)s_dk.mem_ring, (void*)s_dk.mem_desc);
        return -1;
    }
    memset(dkMemBlockGetCpuAddr(s_dk.mem_desc), 0, DK_DESC_SIZE);

    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, s_dk.device);
    qm.flags = DkQueueFlags_Graphics | DkQueueFlags_MediumPrio;
    s_dk.queue = dkQueueCreate(&qm);
    if (!s_dk.queue) { dk_log("dkQueueCreate failed\n"); return -1; }

    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, s_dk.device);
    cm.cbAddMem = dk_cmd_add_mem;
    s_dk.cmd = dkCmdBufCreate(&cm);
    if (!s_dk.cmd) { dk_log("dkCmdBufCreate failed\n"); return -1; }
    dk_cmd_reset();

    /* Swapchain images live in their own block so the target pool's
     * allocator never has to know about them. */
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, s_dk.device);
    lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
               DkImageFlags_Usage2DEngine;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.dimensions[0] = width;
    lm.dimensions[1] = height;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    const u32 align = dkImageLayoutGetAlignment(&layout);
    const u32 one = (u32)((dkImageLayoutGetSize(&layout) + align - 1u) & ~(u64)(align - 1u));
    const u32 swap_size = (one * DK_NUM_SWAP + DK_MEMBLOCK_ALIGNMENT - 1u) &
                          ~(DK_MEMBLOCK_ALIGNMENT - 1u);
    s_dk.mem_swap = dk_block(swap_size, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!s_dk.mem_swap) { dk_log("swapchain memory (%u bytes) failed\n", swap_size); return -1; }
    DkImage const* imgs[DK_NUM_SWAP];
    for (u32 i = 0; i < DK_NUM_SWAP; i++) {
        dkImageInitialize(&s_dk.swap_images[i], &layout, s_dk.mem_swap, i * one);
        imgs[i] = &s_dk.swap_images[i];
    }
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, s_dk.device, nwindowGetDefault(), imgs, DK_NUM_SWAP);
    s_dk.swapchain = dkSwapchainCreate(&sm);
    if (!s_dk.swapchain) { dk_log("dkSwapchainCreate failed\n"); return -1; }

    /* Start on a defined black frame. */
    for (u32 i = 0; i < DK_NUM_SWAP; i++) {
        DkImageView v;
        dkImageViewDefaults(&v, &s_dk.swap_images[i]);
        DkImageView const* vs[1] = { &v };
        dkCmdBufBindRenderTargets(s_dk.cmd, vs, 1, NULL);
        DkScissor sc = { 0, 0, width, height };
        dkCmdBufSetScissors(s_dk.cmd, 0, &sc, 1);
        dkCmdBufClearColorFloat(s_dk.cmd, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    dk_submit_wait();

    /* Sampler slot 0, the default; the white 1x1 an unbound unit samples. */
    {
        /* Slot 0 is written by hand: dk_sampler_slot never hands it out. */
        s_dk.samp_keys[0] = 0;
        s_dk.samp_count = 1;
        DkSampler s;
        dkSamplerDefaults(&s);
        s.minFilter = DkFilter_Linear;
        s.magFilter = DkFilter_Linear;
        s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor sd;
        dkSamplerDescriptorInitialize(&sd, &s);
        memcpy((u8*)dkMemBlockGetCpuAddr(s_dk.mem_desc) + DK_SAMP_OFFSET, &sd, sizeof sd);
    }
    if (dk_image_create(&s_dk.dummy, DkImageFormat_RGBA8_Unorm, 1, 1, 1, 0)) {
        dk_err("dummy texture: out of image memory\n");
        return -1;
    }
    s_dk.dummy.bpp = 4;
    {
        static const u8 white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        dk_upload_rect(&s_dk.dummy, 0, 0, 1, 1, white, 4);
    }
    dk_write_image_desc(DK_DUMMY_SLOT, &s_dk.dummy.image, NULL);
    if (s_dk.error) { dk_log("deko3d reported an error during init\n"); return -1; }
    return 0;
}

int rsx_deko3d_backend_requested(void)
{
    const char* e = getenv("PS3RECOMP_RSX_GPU");
    if (e && *e) return strcmp(e, "0") != 0;
    return 1;
}

int rsx_deko3d_backend_init(uint32_t width, uint32_t height, const char* title)
{
    memset(&s_dk, 0, sizeof s_dk);
    width  = width  ? width  : 1280;
    height = height ? height : 720;
    dk_log("---- T22 deko3d backend, stage 2 (%ux%u, %s, %u precompiled shaders) ----\n",
           width, height, title ? title : "", (u32)RSX_DK_SHADER_COUNT);
    if (dk_init_device(width, height)) {
        dk_log("init FAILED: falling back to the software backend\n");
        dk_destroy_all();
        return -1;
    }
    s_dk.ready = 1;

    /* One path at a time: the engine only. The rsx_state vtable keeps
     * tracking state with no backend registered, as on the Vulkan path. */
    rsx_draw_engine_set_backend(&s_dk_engine_backend);
    rsx_draw_engine_set_default(1);
    if (!rsx_draw_engine_enabled() || rsx_draw_engine_init(width, height) != 0) {
        dk_log("init FAILED: the draw engine did not come up "
               "(PS3RECOMP_RSX_ENGINE=vtable?)\n");
        rsx_draw_engine_set_backend(NULL);
        dk_destroy_all();
        return -1;
    }
    dk_log("init OK: device, queue, swapchain %ux%u x%u, image pool %u MiB, "
           "draw engine up\n", width, height, DK_NUM_SWAP, DK_IMAGE_POOL >> 20);
    {
        struct mallinfo mi = mallinfo();
        dk_log("heap after init: arena %zu MiB, in use %zu MiB, free %zu MiB\n",
               (size_t)mi.arena >> 20, (size_t)mi.uordblks >> 20,
               (size_t)mi.fordblks >> 20);
    }
    ps3_switch_resource_report("deko3d init done");
    return 0;
}

void rsx_deko3d_backend_shutdown(void)
{
    if (!s_dk.ready) return;
    rsx_draw_engine_shutdown();
    rsx_draw_engine_set_backend(NULL);
    dk_destroy_all();
    dk_log("shutdown\n");
    if (s_log) { fclose(s_log); s_log = NULL; }
}

void rsx_deko3d_backend_present(void)
{
    if (s_dk.ready) rsx_draw_engine_present();
}

int rsx_deko3d_backend_pump_messages(void) { return 0; }

#endif /* __SWITCH__ */
