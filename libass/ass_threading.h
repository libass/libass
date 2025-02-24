/*
 * Copyright (C) 2023 libass contributors
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBASS_THREADING_H
#define LIBASS_THREADING_H

#include "config.h"

#include "ass_compat.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef HAVE_STDATOMIC_H

#include <stdatomic.h>
#define HAVE_ATOMICS 1

typedef intptr_t AtomicInt;

#elif defined(_WIN32)

#define HAVE_ATOMICS 1

#ifdef _WIN64
#define INTERLOCKED(op, order) Interlocked ## op ## order ## 64
typedef LONG64 AtomicInt;
#else
#define INTERLOCKED(op, order) Interlocked ## op ## order
typedef LONG AtomicInt;
#endif

// For some reason, Windows doesn't provide Release versions of InterlockedExchange,
// so just fall back on the default acq_rel/seq_cst versions
#define InterlockedExchangeRelease64 InterlockedExchange64
#define InterlockedExchangeRelease InterlockedExchange

#define memory_order_seq_cst
#define memory_order_acq_rel
#define memory_order_acquire Acquire
#define memory_order_release Release
#define memory_order_consume Release
#define memory_order_relaxed NoFence

#define IS_RELAXED(order) ((#order)[0] == 'N')
#define MAYBE_FENCE(order) (!IS_RELAXED(order) ? MemoryBarrier() : (void)0)

#define DO_COMPARE_EXCHANGE_STRONG(order) \
static inline bool INTERLOCKED(DoCompareExchangePointer, order)(void *obj, void *expected, void *desired) \
{ \
    void *old = *(void**)expected; \
    return (*(void**)expected = InterlockedCompareExchangePointer ## order((void**)obj, expected, desired)) == old; \
}

DO_COMPARE_EXCHANGE_STRONG()
DO_COMPARE_EXCHANGE_STRONG(Acquire)
DO_COMPARE_EXCHANGE_STRONG(Release)
DO_COMPARE_EXCHANGE_STRONG(NoFence)

#ifndef atomic_compare_exchange_strong_explicit
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, order, order2) \
    INTERLOCKED(DoCompareExchangePointer, order)(obj, expected, desired)
#endif

#ifndef atomic_compare_exchange_weak_explicit
#define atomic_compare_exchange_weak_explicit atomic_compare_exchange_strong_explicit
#endif

#ifndef atomic_store_explicit
#define atomic_store_explicit(obj, desired, order) ((*(obj) = (desired)), MAYBE_FENCE(order))
#endif

#ifndef atomic_load_explicit
#define atomic_load_explicit(obj, order) (MAYBE_FENCE(order), *(obj))
#endif

#ifndef atomic_fetch_add_explicit
#define atomic_fetch_add_explicit(obj, arg, order) INTERLOCKED(ExchangeAdd, order)(obj, arg)
#endif

#ifndef atomic_fetch_sub_explicit
#define atomic_fetch_sub_explicit(obj, arg, order) INTERLOCKED(ExchangeAdd, order)(obj, -(AtomicInt)arg)
#endif

#ifndef atomic_init
#define atomic_init(obj, arg) (*(obj) = (arg))
#endif

#ifndef atomic_exchange_explicit
#define atomic_exchange_explicit(obj, desired, order) INTERLOCKED(Exchange, order)(obj, desired)
#endif

#ifndef _Atomic
#define _Atomic
#endif

#else // HAVE_STDATOMIC_H/_WIN32

#define HAVE_ATOMICS 0

typedef intptr_t AtomicInt;

#ifndef atomic_compare_exchange_strong_explicit
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, order, order2) \
    ((*(obj) == *(expected)) ? \
        (*(obj) = (desired), true) : \
        (*(expected) = *(obj), false))
#endif

#ifndef atomic_compare_exchange_weak
#define atomic_compare_exchange_weak_explicit atomic_compare_exchange_strong_explicit
#endif

#ifndef atomic_store_explicit
#define atomic_store_explicit(obj, desired, order) (*(obj) = (desired))
#endif

#ifndef atomic_load_explicit
#define atomic_load_explicit(obj, order) (*(obj))
#endif

#define FETCH_OP(name, op) \
static inline intptr_t do_fetch_ ## name(intptr_t *obj, intptr_t arg) \
{ \
    intptr_t ret = *obj; \
    *obj = *obj op arg; \
    return ret; \
}

#ifndef atomic_fetch_add_explicit
FETCH_OP(add, +)
#define atomic_fetch_add_explicit(obj, arg, order) do_fetch_add(obj, arg)
#endif

#ifndef atomic_fetch_sub_explicit
FETCH_OP(sub, -)
#define atomic_fetch_sub_explicit(obj, arg, order) do_fetch_sub(obj, arg)
#endif

#ifndef atomic_init
#define atomic_init(obj, arg) (*(obj) = (arg))
#endif

#ifndef atomic_exchange_explicit
static inline intptr_t do_exchange(intptr_t *obj, intptr_t desired) {
    intptr_t old = *obj;
    *obj = desired;
    return old;
}

#define atomic_exchange_explicit(obj, desired, order) do_exchange(obj, desired)
#endif

#ifndef _Atomic
#define _Atomic
#endif

#endif // HAVE_STDATOMIC_H/_WIN32

#if HAVE_ATOMICS

#ifdef CONFIG_W32THREAD

#define ENABLE_THREADS 1

struct ThreadStruct {
    void *(*start_routine)(void *);
    void *arg;
    void *ret;
    HANDLE handle;
};

typedef struct ThreadStruct *pthread_t;

typedef SRWLOCK            pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;

static inline int pthread_mutex_init(pthread_mutex_t *mtx, const void *attr)
{
    assert(!attr);
    InitializeSRWLock(mtx);
    return 0;
}

#define pthread_mutex_destroy(x) ((void) 0) // No-op
#define pthread_mutex_lock AcquireSRWLockExclusive
#define pthread_mutex_unlock ReleaseSRWLockExclusive

static inline bool pthread_cond_init(pthread_cond_t *cond, const void *attr)
{
    assert(!attr);
    InitializeConditionVariable(cond);
    return 0;
}

#define pthread_cond_destroy(x) ((void) 0) // No-op
#define pthread_cond_signal WakeConditionVariable
#define pthread_cond_broadcast WakeAllConditionVariable

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return (SleepConditionVariableSRW(cond, mutex, INFINITE, 0) == 0) ? 0 : EINVAL;
}

static DWORD WINAPI
#ifdef __GNUC__
__attribute__((force_align_arg_pointer))
#endif
thread_start_func(void *arg)
{
    pthread_t thread = arg;
    thread->ret = thread->start_routine(thread->arg);
    return 0;
}

static inline int pthread_create(pthread_t *threadp, const void *attr, void *(*start_routine)(void *), void *arg)
{
    *threadp = NULL;

    if (attr)
        return EINVAL;

    pthread_t thread = malloc(sizeof(struct ThreadStruct));
    thread->start_routine = start_routine;
    thread->arg = arg;
    if ((thread->handle = CreateThread(NULL, 0, thread_start_func, thread, 0, NULL))) {
        *threadp = thread;
        return 0;
    } else {
        free(thread);
        return EPERM;
    }
}

static inline int pthread_join(pthread_t thread, void **value_ptr)
{
    DWORD ret = WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);

    if (ret == WAIT_OBJECT_0 && value_ptr)
        *value_ptr = thread->ret;

    free(thread);

    return ret == WAIT_OBJECT_0 ? 0 : EINVAL;
}

static inline void thread_set_namew(PCWSTR name)
{
#if ASS_WINAPI_DESKTOP
    HMODULE dll = GetModuleHandleW(L"kernel32.dll");
    if (!dll)
        return;

    HRESULT (WINAPI *func)(HANDLE, PCWSTR) = (void *) GetProcAddress(dll, "SetThreadDescription");
    if (!func)
        return;

    func(GetCurrentThread(), name);
#endif
}
#define thread_set_name(x) thread_set_namew(L"" x)

#elif defined(CONFIG_PTHREAD)

#include <pthread.h>
#define ENABLE_THREADS 1

static inline void thread_set_name(const char *name)
{
#if defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__FreeBSD__)
    pthread_set_name_np(pthread_self(), name);
#endif
}

#endif /* CONFIG_PTHREAD */

#define inc_ref(x) (void) (atomic_fetch_add_explicit(x, 1, memory_order_relaxed))
#define dec_ref(x) (atomic_fetch_sub_explicit(x, 1, memory_order_acq_rel) - 1)

#else /* HAVE_ATOMICS */

static inline void inc_ref(AtomicInt *count) { ++(*count); }
static inline AtomicInt dec_ref(AtomicInt *count) { return --(*count); }

#endif /* HAVE_ATOMICS */

#ifdef ENABLE_THREADS

static inline unsigned default_threads(void)
{
    if (getenv("LIBASS_NO_THREADS"))
        return 1;

#if HAVE_SCHED_GETAFFINITY && defined(CPU_COUNT)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (!sched_getaffinity(0, sizeof(cpuset), &cpuset))
        return CPU_COUNT(&cpuset);
#elif defined(_SC_NPROCESSORS_ONLN)
    long sc = sysconf(_SC_NPROCESSORS_ONLN);
    if (sc < 0)
        return 1;
    return sc;
#elif defined(_WIN32)
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    return info.dwNumberOfProcessors;
#endif

    return 1;
}

#else /* ENABLE_THREADS */

#define ENABLE_THREADS 0

#define pthread_mutex_lock(x) do {} while(0)
#define pthread_mutex_unlock(x) do {} while(0)
#define pthread_cond_signal(x) do {} while(0)
#define pthread_cond_broadcast(x) do {} while(0)

#endif /* !ENABLE_THREADS */

#endif                          /* LIBASS_THREADING_H */
