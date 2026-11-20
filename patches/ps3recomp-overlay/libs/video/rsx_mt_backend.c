/*
 * Deferred draw backend: a pass-through rsx_draw_backend that records the
 * engine's calls into command buffers and replays them on the real backend
 * from a second thread (ROADto60fps.md, render split).
 *
 * The engine (FIFO walk, state, vertex fetch, texture decode) keeps running
 * on its own thread; the recorded calls (binds, constants, draws, clears,
 * uploads, releases, present) run on the "draw" thread, which alone talks to
 * the real backend. A buffer is handed over at every present (at most two
 * waiting, then the engine waits) or when it grows past 16 MB.
 *
 * Every recorded call copies what it points at (vertices, indices,
 * constants, rows): the engine reuses those buffers as soon as a call
 * returns, and the title reuses guest memory once the engine is past a frame.
 *
 * Calls that return something:
 *  - surface_view: answered at once with a virtual handle (bit 30 set); the
 *    draw thread asks the real backend when it replays the bind, and binds
 *    the surface itself when there is no view (what the engine does on 0).
 *  - surface_blit: answered at once after the real backend's own checks
 *    (known, distinct colour targets of one format, rectangles inside), then
 *    recorded. A refused one is refused at once, as before.
 *  - creates, depth_snapshot, readback, submit_and_wait: drain (hand over
 *    and wait until the draw thread is idle), then call the real backend on
 *    the engine's thread. Rare (run130).
 *
 * SD switches: render-mt-off bypasses all of it; render-mt-sync replays on
 * the engine's thread (step 1 behaviour). Non-light builds count every call
 * kind and the time the engine waited for the draw thread in t40-bemt.log.
 */
#include "rsx_draw_engine.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
/* The ARM generic timer (19.2 MHz), as rsx_draw_engine.c reads it: switch.h's
 * own types clash with the runtime's u128. */
static inline u64 mt_tick(void) { u64 t; __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t)); return t; }
static inline u64 mt_ticks_to_ns(u64 t) { return t * 625u / 12u; }
#endif

enum {
    OP_TEX_UPLOAD, OP_TEX_RELEASE, OP_CT_RELEASE, OP_CT_WRITE, OP_DT_RELEASE,
    OP_PIPE_RELEASE, OP_BIND_TARGETS, OP_BIND_PIPELINE, OP_VS_CONST, OP_PS_CONST,
    OP_BIND_TEX, OP_BIND_VTEX, OP_VIEWPORT, OP_SCISSOR, OP_STENCIL_REF, OP_DRAW,
    OP_CLEAR_COLOR, OP_CLEAR_DS, OP_PRESENT, OP_BLIT,
    OP_DEFERRED_COUNT,
    /* counted only */
    OP_SUBMIT_WAIT = OP_DEFERRED_COUNT, OP_TEX_CREATE, OP_CT_CREATE, OP_SURF_VIEW,
    OP_DT_CREATE, OP_DEPTH_SNAP, OP_PIPE_CREATE, OP_READBACK, OP_DEPTH_READBACK,
    OP_BLIT_REFUSED, OP_SURF_VIEW_SYNC,
    OP_COUNT
};
static const char* const k_op_name[OP_COUNT] = {
    "tex_upload", "tex_release", "ct_release", "ct_write", "dt_release",
    "pipe_release", "bind_targets", "bind_pipeline", "vs_const", "ps_const",
    "bind_tex", "bind_vtex", "viewport", "scissor", "stencil_ref", "draw",
    "clear_color", "clear_ds", "present", "blit",
    "SYNC_submit_wait", "SYNC_tex_create", "SYNC_ct_create", "surface_view(virtual)",
    "SYNC_dt_create", "SYNC_depth_snapshot", "SYNC_pipe_create", "SYNC_readback",
    "SYNC_depth_readback", "blit_refused", "SYNC_surface_view(table full)",
};

typedef struct { u32 op, size; } cmd_hdr;   /* size = header + payload, 8-aligned */
typedef struct { u8* data; size_t len, cap; } cmd_buf;

static const rsx_draw_backend* s_real;
static rsx_draw_backend s_proxy;
static int s_threaded;

/* Recording side: one buffer being filled, under s_rec_lock (the engine can
 * be entered from the FIFO walker's poll path as well as the render thread). */
static char s_rec_lock;
static cmd_buf* s_cur;
static inline void rec_lock(void)
{
    while (__atomic_test_and_set(&s_rec_lock, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&s_rec_lock, __ATOMIC_RELAXED)) sched_yield();
    }
}
static inline void rec_unlock(void) { __atomic_clear(&s_rec_lock, __ATOMIC_RELEASE); }

/* Set by the engine before each draw (rsx_draw_engine.c): the vertices come
 * from its vertex cache, whose arrays outlive a replay (freed 8 presents
 * after eviction), so the draw records the pointer instead of a copy. */
int g_rsx_mt_verts_stable;



/* Hand-over: buffers waiting for the draw thread, and free ones. */
#define MT_BUFS 4
#define MT_MAX_PENDING 2
static cmd_buf s_pool[MT_BUFS];
static cmd_buf* s_pending[MT_BUFS];
static unsigned s_pending_n;
static cmd_buf* s_free[MT_BUFS];
static unsigned s_free_n;
static int s_draw_busy;
static pthread_mutex_t s_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_q_work = PTHREAD_COND_INITIALIZER;   /* draw thread waits */
static pthread_cond_t s_q_done = PTHREAD_COND_INITIALIZER;   /* engine waits */
/* Only one thread talks to the real backend at a time. */
static pthread_mutex_t s_be_lock = PTHREAD_MUTEX_INITIALIZER;

/* Virtual surface views (bit 30). */
#define MT_VIEW_TAG 0x40000000u
#define MT_VIEWS 1024u
typedef struct { u32 surface, remap, fmt; } mt_view;
static mt_view s_views[MT_VIEWS];
static u32 s_views_n;

/* Colour targets the engine created, for surface_blit's checks. */
#define MT_TARGETS 4096u
typedef struct { u32 used, fmt, w, h; } mt_target;
static mt_target s_targets[MT_TARGETS];

/* ---- redundant state ------------------------------------------------------ */
/* The engine binds the whole state before every draw (run226: ~360 draws per
 * image, every bind recorded and replayed each time). The real backend keeps
 * what was bound until the next bind (rsx_deko3d_backend.c be_bind_*, applied
 * by be_draw), so a bind equal to the last one recorded is dropped. What was
 * last recorded is forgotten (s_last_valid = 0) at every buffer hand-over and
 * at every recorded call that is not a bind or a draw (clears, blits,
 * uploads, releases, present) and every direct call: after those the full
 * state is recorded again, as before. The fragment constants are compared
 * against their last copy in the current buffer, the one the backend points
 * at. SD switch render-dedup-off records every bind. */
enum {
    LV_TARGETS = 1u << 0, LV_PIPE = 1u << 1, LV_TEX = 1u << 2, LV_VTEX = 1u << 3,
    LV_VIEWPORT = 1u << 4, LV_SCISSOR = 1u << 5, LV_STENCIL = 1u << 6, LV_PS = 1u << 7,
};
static int s_dedup = 1;
static u32 s_last_valid;
static size_t s_last_ps_off;      /* payload offset in s_cur of the last OP_PS_CONST */

/* Vertex constants across buffers. The backend keeps a pointer to the last
 * block it was given, inside the buffer it came from; the engine rebinds the
 * block only when a constant really changed (rsx_dispatch const_gen), so a
 * buffer's first draws may have no block of their own while the previous
 * buffer is already free and being refilled. Each buffer therefore gets its
 * own copy before its first draw: the last block is kept aside at hand-over
 * (one copy per buffer) and recorded again when needed. */
static int s_vs_in_buf;           /* an OP_VS_CONST was recorded in s_cur */
static size_t s_vs_last_off;      /* its payload offset in s_cur */
static u8* s_vs_shadow;
static u32 s_vs_shadow_bytes, s_vs_shadow_cap;
static int s_vs_shadow_valid, s_vs_shadow_has_data;

/* ---- statistics (non-light builds) --------------------------------------- */
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
static unsigned long long s_skipped[OP_COUNT];
static unsigned long long s_count[OP_COUNT], s_bytes, s_replays, s_replay_ticks, s_max_len;
static unsigned long long s_wait_full_ticks, s_wait_drain_ticks, s_drains;
static u64 s_window_t0;
static FILE* s_log;
static void stats_window(void)
{
    const u64 now = mt_tick();
    if (!s_window_t0) { s_window_t0 = now; return; }
    if (mt_ticks_to_ns(now - s_window_t0) < 5000000000ull) return;
    if (!s_log) s_log = fopen("sdmc:/switch/dbz-budokai-3-switch/t40-bemt.log", "w");
    if (s_log) {
        fprintf(s_log, "window presents=%llu threaded=%d recorded_kb=%llu replays=%llu replay_ms=%llu "
                "max_buffer_kb=%llu engine_wait_queue_full_ms=%llu engine_wait_drain_ms=%llu drains=%llu\n",
                s_count[OP_PRESENT], s_threaded, s_bytes >> 10, s_replays,
                (unsigned long long)(mt_ticks_to_ns(s_replay_ticks) / 1000000ull), s_max_len >> 10,
                (unsigned long long)(mt_ticks_to_ns(s_wait_full_ticks) / 1000000ull),
                (unsigned long long)(mt_ticks_to_ns(s_wait_drain_ticks) / 1000000ull), s_drains);
        for (int i = 0; i < OP_COUNT; i++)
            if (s_count[i]) fprintf(s_log, "  %s=%llu\n", k_op_name[i], s_count[i]);
        for (int i = 0; i < OP_COUNT; i++)
            if (s_skipped[i]) fprintf(s_log, "  skipped_%s=%llu\n", k_op_name[i], s_skipped[i]);
        fflush(s_log);
    }
    memset(s_count, 0, sizeof s_count);
    memset(s_skipped, 0, sizeof s_skipped);
    s_bytes = s_replays = s_replay_ticks = s_max_len = 0;
    s_wait_full_ticks = s_wait_drain_ticks = s_drains = 0;
    s_window_t0 = now;
}
#define COUNT(op) (s_count[op]++)
#define SKIPPED(op) (s_skipped[op]++)
#define TICK() mt_tick()
#else
#define COUNT(op) ((void)0)
#define SKIPPED(op) ((void)0)
#define TICK() 0ull
#endif

/* ---- replay ---------------------------------------------------------------- */
typedef struct { u32 tex, face, mip, w, h, row_bytes, rows; } p_tex_upload;
typedef struct { u32 surface, x, y, w, h, row_bytes; } p_ct_write;
typedef struct { u32 count, depth, surfaces[RSX_BE_MAX_COLOR_TARGETS]; } p_bind_targets;
typedef struct { u32 bytes, has_data; } p_const;
typedef struct { u32 mask; u32 tex[RSX_BE_MAX_TEXTURES]; rsx_be_sampler_desc smp[RSX_BE_MAX_TEXTURES]; } p_bind_tex;
typedef struct { u32 mask; u32 tex[RSX_BE_MAX_VERTEX_TEXTURES]; rsx_be_sampler_desc smp[RSX_BE_MAX_VERTEX_TEXTURES]; } p_bind_vtex;
typedef struct { float x, y, w, h; } p_viewport;
typedef struct { u32 x, y, w, h; } p_scissor;
typedef struct { u32 topology, vertex_count, stride, index_count, has_indices, in_place; const void* verts; } p_draw;
typedef struct { u32 surface; float rgba[4]; } p_clear_color;
typedef struct { u32 depth, flags; float value; u32 stencil; } p_clear_ds;
typedef struct { u32 src, sx, sy, sw, sh, dst, dx, dy, dw, dh; } p_blit;

/* A virtual view to the handle the real backend binds. */
static u32 resolve_tex(u32 h)
{
    if (!(h & MT_VIEW_TAG)) return h;
    const u32 i = h & ~MT_VIEW_TAG;
    if (i >= MT_VIEWS) return 0;
    const mt_view* v = &s_views[i];
    const u32 real = s_real->surface_view
        ? s_real->surface_view(s_real->user, v->surface, v->remap, v->fmt) : 0;
    return real ? real : v->surface;
}

static void replay_buf(cmd_buf* cb)
{
    const rsx_draw_backend* b = s_real;
    void* u = b->user;
    size_t at = 0;
    while (at < cb->len) {
        const cmd_hdr* h = (const cmd_hdr*)(cb->data + at);
        const u8* p = (const u8*)(h + 1);
        switch (h->op) {
        case OP_TEX_UPLOAD: {
            const p_tex_upload* c = (const p_tex_upload*)p;
            b->texture_upload(u, c->tex, c->face, c->mip, c->w, c->h, c + 1, c->row_bytes, c->rows);
            break; }
        case OP_TEX_RELEASE:   b->texture_release(u, *(const u32*)p); break;
        case OP_CT_RELEASE:    b->color_target_release(u, *(const u32*)p); break;
        case OP_CT_WRITE: {
            const p_ct_write* c = (const p_ct_write*)p;
            b->color_target_write(u, c->surface, c->x, c->y, c->w, c->h, c + 1, c->row_bytes);
            break; }
        case OP_DT_RELEASE:    b->depth_target_release(u, *(const u32*)p); break;
        case OP_PIPE_RELEASE:  b->pipeline_release(u, *(const u32*)p); break;
        case OP_BIND_TARGETS: {
            const p_bind_targets* c = (const p_bind_targets*)p;
            b->bind_targets(u, c->surfaces, c->count, c->depth);
            break; }
        case OP_BIND_PIPELINE: b->bind_pipeline(u, *(const u32*)p); break;
        case OP_VS_CONST: {
            const p_const* c = (const p_const*)p;
            b->bind_vs_constants(u, c->has_data ? (const void*)(c + 1) : NULL, c->bytes);
            break; }
        case OP_PS_CONST: {
            const p_const* c = (const p_const*)p;
            b->bind_ps_constants(u, c->has_data ? (const void*)(c + 1) : NULL, c->bytes);
            break; }
        case OP_BIND_TEX: {
            p_bind_tex c = *(const p_bind_tex*)p;
            for (u32 i = 0; i < RSX_BE_MAX_TEXTURES; i++)
                if (c.mask & (1u << i)) c.tex[i] = resolve_tex(c.tex[i]);
            b->bind_textures(u, c.tex, c.smp, c.mask);
            break; }
        case OP_BIND_VTEX: {
            p_bind_vtex c = *(const p_bind_vtex*)p;
            for (u32 i = 0; i < RSX_BE_MAX_VERTEX_TEXTURES; i++)
                if (c.mask & (1u << i)) c.tex[i] = resolve_tex(c.tex[i]);
            b->bind_vertex_textures(u, c.tex, c.smp, c.mask);
            break; }
        case OP_VIEWPORT: {
            const p_viewport* c = (const p_viewport*)p;
            b->set_viewport(u, c->x, c->y, c->w, c->h);
            break; }
        case OP_SCISSOR: {
            const p_scissor* c = (const p_scissor*)p;
            b->set_scissor(u, c->x, c->y, c->w, c->h);
            break; }
        case OP_STENCIL_REF:   b->set_stencil_ref(u, *(const u32*)p); break;
        case OP_DRAW: {
            const p_draw* c = (const p_draw*)p;
            const u8* data = (const u8*)(c + 1);
            const size_t vbytes = c->in_place ? 0 : (((size_t)c->vertex_count * c->stride + 7u) & ~(size_t)7u);
            const u8* verts = c->in_place ? (const u8*)c->verts : data;
            const u32* idx = c->has_indices ? (const u32*)(data + vbytes) : NULL;
            b->draw(u, (rsx_topology)c->topology, c->vertex_count ? verts : NULL,
                    c->vertex_count, c->stride, idx, c->index_count);
            break; }
        case OP_CLEAR_COLOR: {
            const p_clear_color* c = (const p_clear_color*)p;
            b->clear_color(u, c->surface, c->rgba);
            break; }
        case OP_CLEAR_DS: {
            const p_clear_ds* c = (const p_clear_ds*)p;
            b->clear_depth_stencil(u, c->depth, c->flags, c->value, (u8)c->stencil);
            break; }
        case OP_PRESENT:       b->present(u, *(const u32*)p); break;
        case OP_BLIT: {
            const p_blit* c = (const p_blit*)p;
            b->surface_blit(u, c->src, c->sx, c->sy, c->sw, c->sh, c->dst, c->dx, c->dy, c->dw, c->dh);
            break; }
        default: break;
        }
        at += h->size;
    }
    cb->len = 0;
}

static void replay_timed(cmd_buf* cb)
{
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
    const u64 t0 = mt_tick();
#endif
    pthread_mutex_lock(&s_be_lock);
    replay_buf(cb);
    pthread_mutex_unlock(&s_be_lock);
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
    s_replay_ticks += mt_tick() - t0;
    s_replays++;
#endif
}

/* ---- the draw thread --------------------------------------------------------- */
static void* draw_thread(void* arg)
{
    (void)arg;
#ifdef __SWITCH__
    { extern void ps3_thread_stats_register(const char*, const char*);
      ps3_thread_stats_register("frame", "render-draw"); }
#endif
    for (;;) {
        pthread_mutex_lock(&s_q_lock);
        while (!s_pending_n) pthread_cond_wait(&s_q_work, &s_q_lock);
        cmd_buf* cb = s_pending[0];
        for (unsigned i = 1; i < s_pending_n; i++) s_pending[i - 1] = s_pending[i];
        s_pending_n--;
        s_draw_busy = 1;
        pthread_mutex_unlock(&s_q_lock);

        replay_timed(cb);

        pthread_mutex_lock(&s_q_lock);
        s_free[s_free_n++] = cb;
        s_draw_busy = 0;
        pthread_cond_broadcast(&s_q_done);
        pthread_mutex_unlock(&s_q_lock);
    }
    return NULL;
}

/* Under s_rec_lock: give the current buffer to the draw thread (or replay it
 * here in sync mode) and take an empty one. Waits while two are pending. */
static void vs_keep_aside(void)
{
    if (!s_vs_in_buf || !s_cur) return;
    const p_const* c = (const p_const*)(s_cur->data + s_vs_last_off);
    const u32 n = c->has_data ? c->bytes : 0;
    if (n > s_vs_shadow_cap) {
        u8* nb = (u8*)realloc(s_vs_shadow, n);
        if (!nb) { s_vs_shadow_valid = 0; s_vs_in_buf = 0; return; }
        s_vs_shadow = nb;
        s_vs_shadow_cap = n;
    }
    if (n) memcpy(s_vs_shadow, c + 1, n);
    s_vs_shadow_bytes = c->bytes;
    s_vs_shadow_has_data = c->has_data;
    s_vs_shadow_valid = 1;
    s_vs_in_buf = 0;
}

static void hand_over(void)
{
    s_last_valid = 0;              /* the next buffer starts with the full state */
    vs_keep_aside();
    s_vs_in_buf = 0;
    if (!s_cur || !s_cur->len) return;
    if (!s_threaded) { replay_timed(s_cur); return; }
    const u64 t0 = TICK();
    pthread_mutex_lock(&s_q_lock);
    while (s_pending_n >= MT_MAX_PENDING || !s_free_n) pthread_cond_wait(&s_q_done, &s_q_lock);
    s_pending[s_pending_n++] = s_cur;
    s_cur = s_free[--s_free_n];
    pthread_cond_signal(&s_q_work);
    pthread_mutex_unlock(&s_q_lock);
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
    s_wait_full_ticks += mt_tick() - t0;
#else
    (void)t0;
#endif
}

/* Under s_rec_lock: everything recorded so far has run on the real backend
 * when this returns; the caller then holds s_be_lock (release with drain_end). */
static void drain_begin(void)
{
    hand_over();
    if (s_threaded) {
        const u64 t0 = TICK();
        pthread_mutex_lock(&s_q_lock);
        while (s_pending_n || s_draw_busy) pthread_cond_wait(&s_q_done, &s_q_lock);
        pthread_mutex_unlock(&s_q_lock);
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
        s_wait_drain_ticks += mt_tick() - t0;
        s_drains++;
#else
        (void)t0;
#endif
    }
    pthread_mutex_lock(&s_be_lock);
}
static void drain_end(void) { pthread_mutex_unlock(&s_be_lock); }

/* ---- recording ----------------------------------------------------------- */
/* Under s_rec_lock. NULL only when memory runs out (the caller then drains
 * and calls the real backend directly). */
static u8* rec(u32 op, size_t payload)
{
    const size_t size = (sizeof(cmd_hdr) + payload + 7u) & ~(size_t)7u;
    if (s_cur->len && s_cur->len + size > ((size_t)16 << 20)) hand_over();
    cmd_buf* cb = s_cur;
    if (cb->len + size > cb->cap) {
        size_t cap = cb->cap ? cb->cap * 2 : (size_t)8 << 20;
        while (cap < cb->len + size) cap *= 2;
        u8* nb = (u8*)realloc(cb->data, cap);
        if (!nb) return NULL;
        cb->data = nb; cb->cap = cap;
    }
    cmd_hdr* h = (cmd_hdr*)(cb->data + cb->len);
    h->op = op; h->size = (u32)size;
    cb->len += size;
    switch (op) {
    case OP_BIND_TARGETS: case OP_BIND_PIPELINE: case OP_VS_CONST: case OP_PS_CONST:
    case OP_BIND_TEX: case OP_BIND_VTEX: case OP_VIEWPORT: case OP_SCISSOR:
    case OP_STENCIL_REF: case OP_DRAW:
        break;
    default:
        s_last_valid = 0;          /* clears, blits, uploads, releases, present */
        break;
    }
    COUNT(op);
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
    s_bytes += size;
    if (cb->len > s_max_len) s_max_len = cb->len;
#endif
    return (u8*)(h + 1);
}

#define LOCK()   rec_lock()
#define UNLOCK() rec_unlock()
/* Out of memory for the record: run the call directly, in order. */
#define DIRECT(call) do { drain_begin(); call; drain_end(); UNLOCK(); return; } while (0)

static void px_texture_upload(void* user, u32 texture, u32 face, u32 mip, u32 w, u32 h,
                              const void* src, u32 row_bytes, u32 rows)
{
    (void)user;
    LOCK();
    const size_t n = (size_t)row_bytes * rows;
    p_tex_upload* c = (p_tex_upload*)rec(OP_TEX_UPLOAD, sizeof *c + n);
    if (!c) DIRECT(s_real->texture_upload(s_real->user, texture, face, mip, w, h, src, row_bytes, rows));
    c->tex = texture; c->face = face; c->mip = mip; c->w = w; c->h = h;
    c->row_bytes = row_bytes; c->rows = rows;
    if (n && src) memcpy(c + 1, src, n);
    UNLOCK();
}
#define REC_U32(name, op, field, extra)                                       \
    static void name(void* user, u32 v)                                       \
    {                                                                         \
        (void)user;                                                           \
        LOCK();                                                               \
        extra;                                                                \
        u32* c = (u32*)rec(op, sizeof(u32));                                  \
        if (!c) DIRECT(s_real->field(s_real->user, v));                       \
        *c = v;                                                               \
        UNLOCK();                                                             \
    }
REC_U32(px_texture_release, OP_TEX_RELEASE, texture_release, (void)0)
REC_U32(px_color_target_release, OP_CT_RELEASE, color_target_release,
        if (v < MT_TARGETS) s_targets[v].used = 0)
REC_U32(px_depth_target_release, OP_DT_RELEASE, depth_target_release, (void)0)
REC_U32(px_pipeline_release, OP_PIPE_RELEASE, pipeline_release, (void)0)
/* A bind of one u32 that is dropped when it equals the last one recorded. */
#define REC_U32_STATE(name, op, field, bit)                                   \
    static u32 name##_last;                                                   \
    static void name(void* user, u32 v)                                      \
    {                                                                         \
        (void)user;                                                           \
        LOCK();                                                               \
        if (s_dedup && (s_last_valid & (bit)) && name##_last == v) {          \
            SKIPPED(op);                                                      \
            UNLOCK();                                                         \
            return;                                                           \
        }                                                                     \
        u32* c = (u32*)rec(op, sizeof(u32));                                  \
        if (!c) DIRECT(s_real->field(s_real->user, v));                       \
        *c = v;                                                               \
        name##_last = v;                                                      \
        s_last_valid |= (bit);                                                \
        UNLOCK();                                                             \
    }
REC_U32_STATE(px_bind_pipeline, OP_BIND_PIPELINE, bind_pipeline, LV_PIPE)
REC_U32_STATE(px_set_stencil_ref, OP_STENCIL_REF, set_stencil_ref, LV_STENCIL)

static void px_color_target_write(void* user, u32 surface, u32 x, u32 y, u32 w, u32 h,
                                  const void* rows, u32 row_bytes)
{
    (void)user;
    LOCK();
    const size_t n = (size_t)row_bytes * h;
    p_ct_write* c = (p_ct_write*)rec(OP_CT_WRITE, sizeof *c + n);
    if (!c) DIRECT(s_real->color_target_write(s_real->user, surface, x, y, w, h, rows, row_bytes));
    c->surface = surface; c->x = x; c->y = y; c->w = w; c->h = h; c->row_bytes = row_bytes;
    if (n && rows) memcpy(c + 1, rows, n);
    UNLOCK();
}
static p_bind_targets s_last_targets;
static void px_bind_targets(void* user, const u32* surfaces, u32 count, u32 depth)
{
    (void)user;
    LOCK();
    p_bind_targets t;
    int fits = count <= RSX_BE_MAX_COLOR_TARGETS;
    if (fits) {
        t.count = count; t.depth = depth;
        memset(t.surfaces, 0, sizeof t.surfaces);
        if (count && surfaces) memcpy(t.surfaces, surfaces, count * sizeof(u32));
        if (s_dedup && (s_last_valid & LV_TARGETS) && !memcmp(&t, &s_last_targets, sizeof t)) {
            SKIPPED(OP_BIND_TARGETS);
            UNLOCK();
            return;
        }
    }
    p_bind_targets* c = fits ? (p_bind_targets*)rec(OP_BIND_TARGETS, sizeof *c) : NULL;
    if (!c) DIRECT(s_real->bind_targets(s_real->user, surfaces, count, depth));
    *c = t;
    s_last_targets = t;
    s_last_valid |= LV_TARGETS;
    UNLOCK();
}
static void px_const(u32 op, const void* data, u32 bytes)
{
    LOCK();
    /* The fragment block: dropped when it equals its last copy in this
     * buffer, which the backend still points at when the draw replays. */
    if (op == OP_PS_CONST && s_dedup && (s_last_valid & LV_PS)) {
        const p_const* last = (const p_const*)(s_cur->data + s_last_ps_off);
        if (last->bytes == bytes && last->has_data == (data != NULL) &&
            (!data || !bytes || !memcmp(last + 1, data, bytes))) {
            SKIPPED(op);
            UNLOCK();
            return;
        }
    }
    p_const* c = (p_const*)rec(op, sizeof *c + (data ? bytes : 0));
    if (!c) {
        if (op == OP_VS_CONST) DIRECT(s_real->bind_vs_constants(s_real->user, data, bytes));
        DIRECT(s_real->bind_ps_constants(s_real->user, data, bytes));
    }
    c->bytes = bytes;
    c->has_data = data != NULL;
    if (data && bytes) memcpy(c + 1, data, bytes);
    if (op == OP_VS_CONST) {
        s_vs_last_off = (size_t)((const u8*)c - s_cur->data);
        s_vs_in_buf = 1;
    }
    if (op == OP_PS_CONST) {
        s_last_ps_off = (size_t)((const u8*)c - s_cur->data);
        s_last_valid |= LV_PS;
    }
    UNLOCK();
}
/* Vertex constants are copied whole: run132 tried row deltas, but more than
 * half of the 7.5 KB block changes on every bind (bone palettes), so the
 * compare only added work. */
static void px_bind_vs_constants(void* user, const void* data, u32 bytes) { (void)user; px_const(OP_VS_CONST, data, bytes); }
static void px_bind_ps_constants(void* user, const void* data, u32 bytes) { (void)user; px_const(OP_PS_CONST, data, bytes); }
/* Texture binds are built in a zeroed local first (padding included), so an
 * equal bind compares equal byte for byte; a false "different" only records
 * the bind, as before. */
static p_bind_tex s_last_tex;
static void px_bind_textures(void* user, const u32* textures, const rsx_be_sampler_desc* samplers, u32 mask)
{
    (void)user;
    LOCK();
    p_bind_tex t;
    memset(&t, 0, sizeof t);
    t.mask = mask;
    for (u32 i = 0; i < RSX_BE_MAX_TEXTURES; i++) {
        if (!(mask & (1u << i))) continue;
        if (textures) t.tex[i] = textures[i];
        if (samplers) memcpy(&t.smp[i], &samplers[i], sizeof t.smp[i]);
    }
    if (s_dedup && (s_last_valid & LV_TEX) && !memcmp(&t, &s_last_tex, sizeof t)) {
        SKIPPED(OP_BIND_TEX);
        UNLOCK();
        return;
    }
    p_bind_tex* c = (p_bind_tex*)rec(OP_BIND_TEX, sizeof *c);
    if (!c) {
        u32 r[RSX_BE_MAX_TEXTURES] = {0};
        for (u32 i = 0; i < RSX_BE_MAX_TEXTURES; i++)
            if ((mask & (1u << i)) && textures) r[i] = resolve_tex(textures[i]);
        DIRECT(s_real->bind_textures(s_real->user, r, samplers, mask));
    }
    memcpy(c, &t, sizeof t);
    memcpy(&s_last_tex, &t, sizeof t);
    s_last_valid |= LV_TEX;
    UNLOCK();
}
static p_bind_vtex s_last_vtex;
static void px_bind_vertex_textures(void* user, const u32* textures, const rsx_be_sampler_desc* samplers, u32 mask)
{
    (void)user;
    LOCK();
    p_bind_vtex t;
    memset(&t, 0, sizeof t);
    t.mask = mask;
    for (u32 i = 0; i < RSX_BE_MAX_VERTEX_TEXTURES; i++) {
        if (!(mask & (1u << i))) continue;
        if (textures) t.tex[i] = textures[i];
        if (samplers) memcpy(&t.smp[i], &samplers[i], sizeof t.smp[i]);
    }
    if (s_dedup && (s_last_valid & LV_VTEX) && !memcmp(&t, &s_last_vtex, sizeof t)) {
        SKIPPED(OP_BIND_VTEX);
        UNLOCK();
        return;
    }
    p_bind_vtex* c = (p_bind_vtex*)rec(OP_BIND_VTEX, sizeof *c);
    if (!c) {
        u32 r[RSX_BE_MAX_VERTEX_TEXTURES] = {0};
        for (u32 i = 0; i < RSX_BE_MAX_VERTEX_TEXTURES; i++)
            if ((mask & (1u << i)) && textures) r[i] = resolve_tex(textures[i]);
        DIRECT(s_real->bind_vertex_textures(s_real->user, r, samplers, mask));
    }
    memcpy(c, &t, sizeof t);
    memcpy(&s_last_vtex, &t, sizeof t);
    s_last_valid |= LV_VTEX;
    UNLOCK();
}
static p_viewport s_last_vp;
static void px_set_viewport(void* user, float x, float y, float w, float h)
{
    (void)user;
    LOCK();
    const p_viewport v = { x, y, w, h };
    if (s_dedup && (s_last_valid & LV_VIEWPORT) && !memcmp(&v, &s_last_vp, sizeof v)) {
        SKIPPED(OP_VIEWPORT);
        UNLOCK();
        return;
    }
    p_viewport* c = (p_viewport*)rec(OP_VIEWPORT, sizeof *c);
    if (!c) DIRECT(s_real->set_viewport(s_real->user, x, y, w, h));
    *c = v;
    s_last_vp = v;
    s_last_valid |= LV_VIEWPORT;
    UNLOCK();
}
static p_scissor s_last_sc;
static void px_set_scissor(void* user, u32 x, u32 y, u32 w, u32 h)
{
    (void)user;
    LOCK();
    const p_scissor s = { x, y, w, h };
    if (s_dedup && (s_last_valid & LV_SCISSOR) && !memcmp(&s, &s_last_sc, sizeof s)) {
        SKIPPED(OP_SCISSOR);
        UNLOCK();
        return;
    }
    p_scissor* c = (p_scissor*)rec(OP_SCISSOR, sizeof *c);
    if (!c) DIRECT(s_real->set_scissor(s_real->user, x, y, w, h));
    *c = s;
    s_last_sc = s;
    s_last_valid |= LV_SCISSOR;
    UNLOCK();
}
static void px_draw(void* user, rsx_topology topology, const void* vertices, u32 vertex_count,
                    u32 stride, const u32* indices, u32 index_count)
{
    (void)user;
    LOCK();
    const int in_place = vertices && g_rsx_mt_verts_stable;
    const size_t vbytes = (vertices && !in_place) ? (((size_t)vertex_count * stride + 7u) & ~(size_t)7u) : 0;
    const size_t ibytes = indices ? (size_t)index_count * sizeof(u32) : 0;
    /* This buffer's own copy of the vertex constants, before its first draw.
     * The block and the draw must land in the same buffer: hand over first if
     * the two would not fit (rec hands over only on a buffer with data). */
    if (!s_vs_in_buf && s_vs_shadow_valid) {
        const size_t vs_rec = (sizeof(cmd_hdr) + sizeof(p_const) +
                               (s_vs_shadow_has_data ? s_vs_shadow_bytes : 0) + 7u) & ~(size_t)7u;
        const size_t dr_rec = (sizeof(cmd_hdr) + sizeof(p_draw) + vbytes + ibytes + 7u) & ~(size_t)7u;
        if (s_cur->len && s_cur->len + vs_rec + dr_rec > ((size_t)16 << 20)) hand_over();
        p_const* v = (p_const*)rec(OP_VS_CONST, vs_rec - sizeof(cmd_hdr));
        if (v) {
            v->bytes = s_vs_shadow_bytes;
            v->has_data = s_vs_shadow_has_data;
            if (s_vs_shadow_has_data && s_vs_shadow_bytes)
                memcpy(v + 1, s_vs_shadow, s_vs_shadow_bytes);
            s_vs_last_off = (size_t)((const u8*)v - s_cur->data);
            s_vs_in_buf = 1;
        }
    }
    p_draw* c = (p_draw*)rec(OP_DRAW, sizeof *c + vbytes + ibytes);
    if (!c) DIRECT(s_real->draw(s_real->user, topology, vertices, vertex_count, stride, indices, index_count));
    c->topology = (u32)topology;
    c->vertex_count = vertices ? vertex_count : 0;
    c->stride = stride;
    c->index_count = index_count;
    c->has_indices = indices != NULL;
    c->in_place = in_place;
    c->verts = in_place ? vertices : NULL;
    u8* d = (u8*)(c + 1);
    if (vbytes) memcpy(d, vertices, (size_t)vertex_count * stride);
    if (ibytes) memcpy(d + vbytes, indices, ibytes);
    UNLOCK();
}
static void px_clear_color(void* user, u32 surface, const float rgba[4])
{
    (void)user;
    LOCK();
    p_clear_color* c = (p_clear_color*)rec(OP_CLEAR_COLOR, sizeof *c);
    if (!c) DIRECT(s_real->clear_color(s_real->user, surface, rgba));
    c->surface = surface;
    memcpy(c->rgba, rgba, sizeof c->rgba);
    UNLOCK();
}
static void px_clear_depth_stencil(void* user, u32 depth, u32 flags, float value, u8 stencil)
{
    (void)user;
    LOCK();
    p_clear_ds* c = (p_clear_ds*)rec(OP_CLEAR_DS, sizeof *c);
    if (!c) DIRECT(s_real->clear_depth_stencil(s_real->user, depth, flags, value, stencil));
    c->depth = depth; c->flags = flags; c->value = value; c->stencil = stencil;
    UNLOCK();
}
static void px_present(void* user, u32 surface)
{
    (void)user;
    LOCK();
    u32* c = (u32*)rec(OP_PRESENT, sizeof(u32));
    if (!c) DIRECT(s_real->present(s_real->user, surface));
    *c = surface;
    hand_over();                   /* one frame per buffer */
#if defined(__SWITCH__) && !defined(PORT_DBZ3_LIGHT_BUILD)
    stats_window();
#endif
    UNLOCK();
}

/* A view for a colour target sampled through a crossbar: a virtual handle at
 * once, resolved by the draw thread when the bind runs. */
static u32 px_surface_view(void* user, u32 surface, u32 remap, u32 rsx_format)
{
    (void)user;
    LOCK();
    for (u32 i = 0; i < s_views_n; i++) {
        const mt_view* v = &s_views[i];
        if (v->surface == surface && v->remap == remap && v->fmt == rsx_format) {
            COUNT(OP_SURF_VIEW);
            UNLOCK();
            return MT_VIEW_TAG | i;
        }
    }
    if (s_views_n < MT_VIEWS) {
        const u32 i = s_views_n++;
        s_views[i].surface = surface; s_views[i].remap = remap; s_views[i].fmt = rsx_format;
        COUNT(OP_SURF_VIEW);
        UNLOCK();
        return MT_VIEW_TAG | i;
    }
    COUNT(OP_SURF_VIEW_SYNC);
    drain_begin();
    const u32 r = s_real->surface_view(s_real->user, surface, remap, rsx_format);
    drain_end();
    UNLOCK();
    return r;
}

/* The real backend's own checks (rsx_deko3d_backend.c be_surface_blit), so a
 * blit it would refuse is refused here, at once, as before. */
static int px_surface_blit(void* user, u32 src, u32 sx, u32 sy, u32 sw, u32 sh,
                           u32 dst, u32 dx, u32 dy, u32 dw, u32 dh)
{
    (void)user;
    LOCK();
    const mt_target* S = src < MT_TARGETS ? &s_targets[src] : NULL;
    const mt_target* D = dst < MT_TARGETS ? &s_targets[dst] : NULL;
    if (!src || !dst || src == dst || !S || !D || !S->used || !D->used || S->fmt != D->fmt ||
        (u64)sx + sw > S->w || (u64)sy + sh > S->h || (u64)dx + dw > D->w || (u64)dy + dh > D->h) {
        COUNT(OP_BLIT_REFUSED);
        UNLOCK();
        return -1;
    }
    p_blit* c = (p_blit*)rec(OP_BLIT, sizeof *c);
    if (!c) {
        drain_begin();
        const int r = s_real->surface_blit(s_real->user, src, sx, sy, sw, sh, dst, dx, dy, dw, dh);
        drain_end();
        UNLOCK();
        return r;
    }
    c->src = src; c->sx = sx; c->sy = sy; c->sw = sw; c->sh = sh;
    c->dst = dst; c->dx = dx; c->dy = dy; c->dw = dw; c->dh = dh;
    UNLOCK();
    return 0;
}

/* ---- direct calls: drain, then the real backend on this thread --------------- */
#define SYNC_RET(op, T, call) do { LOCK(); COUNT(op); drain_begin(); T r_ = call; drain_end(); UNLOCK(); return r_; } while (0)
#define SYNC_VOID(op, call)   do { LOCK(); COUNT(op); drain_begin(); call; drain_end(); UNLOCK(); } while (0)

static int px_init(void* user, u32 width, u32 height)
{ (void)user; SYNC_RET(OP_COUNT - 1, int, s_real->init(s_real->user, width, height)); }
static void px_shutdown(void* user)
{ (void)user; SYNC_VOID(OP_COUNT - 1, s_real->shutdown(s_real->user)); }
static void px_submit_and_wait(void* user, u32 reason)
{ (void)user; SYNC_VOID(OP_SUBMIT_WAIT, s_real->submit_and_wait(s_real->user, reason)); }
static u32 px_texture_create(void* user, rsx_be_format fmt, u32 w, u32 h, u32 mips, u32 faces, u32 remap, u32 rsx_fmt)
{ (void)user; SYNC_RET(OP_TEX_CREATE, u32, s_real->texture_create(s_real->user, fmt, w, h, mips, faces, remap, rsx_fmt)); }
static u32 px_color_target_create(void* user, rsx_be_format fmt, u32 w, u32 h, const void* seed, u32 seed_row_bytes)
{
    (void)user;
    LOCK();
    COUNT(OP_CT_CREATE);
    drain_begin();
    const u32 r = s_real->color_target_create(s_real->user, fmt, w, h, seed, seed_row_bytes);
    drain_end();
    if (r && r < MT_TARGETS) {
        s_targets[r].used = 1; s_targets[r].fmt = (u32)fmt; s_targets[r].w = w; s_targets[r].h = h;
    }
    UNLOCK();
    return r;
}
static u32 px_depth_target_create(void* user, u32 w, u32 h)
{ (void)user; SYNC_RET(OP_DT_CREATE, u32, s_real->depth_target_create(s_real->user, w, h)); }
static u32 px_depth_snapshot(void* user, u32 depth, u32 w, u32 h)
{ (void)user; SYNC_RET(OP_DEPTH_SNAP, u32, s_real->depth_snapshot(s_real->user, depth, w, h)); }
static u32 px_pipeline_create(void* user, const char* vs, const char* ps, const rsx_be_render_state* rs,
                              const rsx_vertex_layout_plan* layout, u32 vertex_stride,
                              rsx_be_format rt_fmt, u32 rt_count)
{ (void)user; SYNC_RET(OP_PIPE_CREATE, u32, s_real->pipeline_create(s_real->user, vs, ps, rs, layout, vertex_stride, rt_fmt, rt_count)); }
static void px_readback(void* user, u32 surface, u32 x, u32 y, u32 w, u32 h, void* out, u32 out_pitch)
{ (void)user; SYNC_VOID(OP_READBACK, s_real->readback(s_real->user, surface, x, y, w, h, out, out_pitch)); }
static int px_depth_readback(void* user, u32 depth, u32 x, u32 y, u32 w, u32 h, float* out, u32 out_pitch)
{ (void)user; SYNC_RET(OP_DEPTH_READBACK, int, s_real->depth_readback(s_real->user, depth, x, y, w, h, out, out_pitch)); }

/* ---- public ------------------------------------------------------------------- */
static int sd_flag(const char* name)
{
#ifdef __SWITCH__
    char path[128];
    snprintf(path, sizeof path, "sdmc:/switch/dbz-budokai-3-switch/%s", name);
    FILE* f = fopen(path, "r");
    if (f) fclose(f);
    return f != NULL;
#else
    (void)name;
    return 0;
#endif
}

const rsx_draw_backend* rsx_mt_backend_wrap(const rsx_draw_backend* real)
{
    if (!real) return NULL;
    if (sd_flag("render-mt-off")) {
        fprintf(stderr, "[bemt] deferred draw backend OFF (render-mt-off)\n");
        return real;
    }
    s_real = real;
    for (unsigned i = 0; i < MT_BUFS; i++) s_free[i] = &s_pool[i];
    s_free_n = MT_BUFS;
    s_cur = s_free[--s_free_n];
    s_threaded = !sd_flag("render-mt-sync");
    s_dedup = !sd_flag("render-dedup-off");
    fprintf(stderr, "[bemt] redundant binds %s\n",
            s_dedup ? "dropped" : "recorded (render-dedup-off)");
    if (s_threaded) {
        pthread_t t;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 256 * 1024);
        if (pthread_create(&t, &a, draw_thread, NULL) != 0) s_threaded = 0;
        else pthread_detach(t);
        pthread_attr_destroy(&a);
    }

    memset(&s_proxy, 0, sizeof s_proxy);
    s_proxy.user = NULL;
    s_proxy.init = px_init;
    s_proxy.shutdown = px_shutdown;
    s_proxy.submit_and_wait = px_submit_and_wait;
    s_proxy.texture_create = px_texture_create;
    s_proxy.texture_upload = px_texture_upload;
    s_proxy.texture_release = px_texture_release;
    s_proxy.color_target_create = px_color_target_create;
    s_proxy.color_target_release = px_color_target_release;
    s_proxy.color_target_write = real->color_target_write ? px_color_target_write : NULL;
    s_proxy.surface_view = real->surface_view ? px_surface_view : NULL;
    s_proxy.depth_target_create = px_depth_target_create;
    s_proxy.depth_target_release = px_depth_target_release;
    s_proxy.depth_snapshot = real->depth_snapshot ? px_depth_snapshot : NULL;
    s_proxy.pipeline_create = px_pipeline_create;
    s_proxy.pipeline_release = px_pipeline_release;
    s_proxy.bind_targets = px_bind_targets;
    s_proxy.bind_pipeline = px_bind_pipeline;
    s_proxy.bind_vs_constants = px_bind_vs_constants;
    s_proxy.bind_ps_constants = px_bind_ps_constants;
    s_proxy.bind_textures = px_bind_textures;
    s_proxy.bind_vertex_textures = px_bind_vertex_textures;
    s_proxy.set_viewport = px_set_viewport;
    s_proxy.set_scissor = px_set_scissor;
    s_proxy.set_stencil_ref = px_set_stencil_ref;
    s_proxy.draw = px_draw;
    s_proxy.clear_color = px_clear_color;
    s_proxy.clear_depth_stencil = px_clear_depth_stencil;
    s_proxy.present = px_present;
    s_proxy.readback = real->readback ? px_readback : NULL;
    s_proxy.depth_readback = real->depth_readback ? px_depth_readback : NULL;
    s_proxy.surface_blit = real->surface_blit ? px_surface_blit : NULL;
    fprintf(stderr, "[bemt] deferred draw backend ON (%s)\n",
            s_threaded ? "draw thread" : "replayed on the engine's thread (render-mt-sync)");
    return &s_proxy;
}
