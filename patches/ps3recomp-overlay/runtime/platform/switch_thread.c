#include "switch_thread.h"

#include <switch.h>
#include <stdlib.h>

typedef struct SwitchThread {
    Thread native;
    Ps3PlatformThreadEntry entry;
    void *argument;
    struct SwitchThread *next;
} SwitchThread;

/* libnx's threadClose must run after the target exits and from another thread:
 * it unmaps the target stack. A single reaper handles detached workers. */
static Mutex reaper_mutex;
static CondVar reaper_cond;
static Thread reaper_thread;
static SwitchThread *reaper_head;
static SwitchThread *reaper_tail;
static bool reaper_started;

static void switch_thread_entry(void *argument)
{
    SwitchThread *thread = (SwitchThread *)argument;
    thread->entry(thread->argument);
}

static void switch_thread_reaper(void *argument)
{
    (void)argument;
    for (;;) {
        mutexLock(&reaper_mutex);
        while (!reaper_head) condvarWait(&reaper_cond, &reaper_mutex);
        SwitchThread *thread = reaper_head;
        reaper_head = thread->next;
        if (!reaper_head) reaper_tail = NULL;
        mutexUnlock(&reaper_mutex);

        if (R_SUCCEEDED(threadWaitForExit(&thread->native)) &&
            R_SUCCEEDED(threadClose(&thread->native))) free(thread);
    }
}

static Ps3PlatformStatus switch_thread_create(void *context, void **out_thread,
    Ps3PlatformThreadEntry entry, void *argument, uint64_t stack_size,
    int32_t priority)
{
    (void)context;
    if (!out_thread || !entry || stack_size > SIZE_MAX - 0xfff)
        return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_thread = NULL;
    SwitchThread *thread = (SwitchThread *)calloc(1, sizeof(*thread));
    if (!thread) return PS3_PLATFORM_OUT_OF_MEMORY;
    thread->entry = entry;
    thread->argument = argument;
    size_t stack = stack_size ? (size_t)stack_size : 128 * 1024;
    if (stack < 16 * 1024) stack = 16 * 1024;
    stack = (stack + 0xfff) & ~(size_t)0xfff;
    int native_priority = priority > 20 ? 0x18 :
        priority < -19 ? 0x3f : 0x2c - priority;
    if (native_priority < 0x18) native_priority = 0x18;
    if (native_priority > 0x3f) native_priority = 0x3f;
    Result rc = threadCreate(&thread->native, switch_thread_entry, thread,
                             NULL, stack, native_priority, -2);
    if (R_FAILED(rc)) { free(thread); return PS3_PLATFORM_OUT_OF_MEMORY; }
    rc = threadStart(&thread->native);
    if (R_FAILED(rc)) {
        threadClose(&thread->native);
        free(thread);
        return PS3_PLATFORM_IO_ERROR;
    }
    *out_thread = thread;
    return PS3_PLATFORM_OK;
}

static Ps3PlatformStatus switch_thread_join(void *context, void *handle,
                                             uint64_t timeout_us)
{
    (void)context;
    if (!handle) return PS3_PLATFORM_INVALID_ARGUMENT;
    SwitchThread *thread = (SwitchThread *)handle;
    uint64_t timeout_ns = timeout_us == PS3_PLATFORM_WAIT_FOREVER ? UINT64_MAX :
        timeout_us > UINT64_MAX / 1000 ? UINT64_MAX - 1 : timeout_us * 1000;
    Result rc = svcWaitSynchronizationSingle(thread->native.handle, timeout_ns);
    if (R_SUCCEEDED(rc)) return PS3_PLATFORM_OK;
    return R_VALUE(rc) == KERNELRESULT(TimedOut) ? PS3_PLATFORM_TIMEOUT :
        PS3_PLATFORM_IO_ERROR;
}

static void switch_thread_detach(void *context, void *handle)
{
    (void)context;
    if (!handle) return;
    SwitchThread *thread = (SwitchThread *)handle;
    mutexLock(&reaper_mutex);
    if (!reaper_started) {
        condvarInit(&reaper_cond);
        Result rc = threadCreate(&reaper_thread, switch_thread_reaper, NULL,
                                 NULL, 64 * 1024, 0x2c, -2);
        if (R_FAILED(rc)) { mutexUnlock(&reaper_mutex); return; }
        rc = threadStart(&reaper_thread);
        if (R_FAILED(rc)) {
            threadClose(&reaper_thread);
            mutexUnlock(&reaper_mutex);
            return;
        }
        reaper_started = true;
    }
    thread->next = NULL;
    if (reaper_tail) reaper_tail->next = thread;
    else reaper_head = thread;
    reaper_tail = thread;
    condvarWakeOne(&reaper_cond);
    mutexUnlock(&reaper_mutex);
}

static void switch_thread_yield(void *context)
{ (void)context; svcSleepThread(YieldType_WithCoreMigration); }

static Ps3PlatformStatus switch_mutex_create(void *context, void **out_mutex)
{
    (void)context;
    if (!out_mutex) return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_mutex = malloc(sizeof(Mutex));
    if (!*out_mutex) return PS3_PLATFORM_OUT_OF_MEMORY;
    mutexInit((Mutex *)*out_mutex);
    return PS3_PLATFORM_OK;
}

static void switch_mutex_destroy(void *context, void *mutex)
{ (void)context; free(mutex); }

static Ps3PlatformStatus switch_mutex_lock(void *context, void *mutex)
{
    (void)context;
    if (!mutex) return PS3_PLATFORM_INVALID_ARGUMENT;
    mutexLock((Mutex *)mutex);
    return PS3_PLATFORM_OK;
}

static void switch_mutex_unlock(void *context, void *mutex)
{ (void)context; if (mutex) mutexUnlock((Mutex *)mutex); }

static Ps3PlatformStatus switch_condition_create(void *context, void **out_condition)
{
    (void)context;
    if (!out_condition) return PS3_PLATFORM_INVALID_ARGUMENT;
    *out_condition = malloc(sizeof(CondVar));
    if (!*out_condition) return PS3_PLATFORM_OUT_OF_MEMORY;
    condvarInit((CondVar *)*out_condition);
    return PS3_PLATFORM_OK;
}

static void switch_condition_destroy(void *context, void *condition)
{ (void)context; free(condition); }

static Ps3PlatformStatus switch_condition_wait(void *context, void *condition,
                                                void *mutex, uint64_t timeout_us)
{
    (void)context;
    if (!condition || !mutex) return PS3_PLATFORM_INVALID_ARGUMENT;
    uint64_t timeout_ns = timeout_us == PS3_PLATFORM_WAIT_FOREVER ? UINT64_MAX :
        timeout_us > UINT64_MAX / 1000 ? UINT64_MAX - 1 : timeout_us * 1000;
    Result rc = condvarWaitTimeout((CondVar *)condition, (Mutex *)mutex, timeout_ns);
    if (R_SUCCEEDED(rc)) return PS3_PLATFORM_OK;
    return R_VALUE(rc) == KERNELRESULT(TimedOut) ? PS3_PLATFORM_TIMEOUT :
        PS3_PLATFORM_IO_ERROR;
}

static void switch_condition_signal(void *context, void *condition)
{ (void)context; if (condition) condvarWakeOne((CondVar *)condition); }

static void switch_condition_broadcast(void *context, void *condition)
{ (void)context; if (condition) condvarWakeAll((CondVar *)condition); }

const Ps3PlatformThreadOps *ps3_platform_switch_thread(void)
{
    static const Ps3PlatformThreadOps ops = {
        .context = NULL,
        .create = switch_thread_create,
        .join = switch_thread_join,
        .detach = switch_thread_detach,
        .yield = switch_thread_yield,
        .mutex_create = switch_mutex_create,
        .mutex_destroy = switch_mutex_destroy,
        .mutex_lock = switch_mutex_lock,
        .mutex_unlock = switch_mutex_unlock,
        .condition_create = switch_condition_create,
        .condition_destroy = switch_condition_destroy,
        .condition_wait = switch_condition_wait,
        .condition_signal = switch_condition_signal,
        .condition_broadcast = switch_condition_broadcast,
    };
    return &ops;
}
