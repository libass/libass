/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CHECKASM_CHECKASM_H
#define CHECKASM_CHECKASM_H

#include "config.h"

#include "ass_bitmap_engine.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#if !ARCH_X86_64 || !defined(_WIN32)
#include <setjmp.h>
#define checkasm_context jmp_buf
#define checkasm_save_context() setjmp(checkasm_context_buf)
#define checkasm_load_context() longjmp(checkasm_context_buf, 1)
#elif WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
/* setjmp/longjmp on 64-bit Windows will try to use SEH to unwind the stack,
 * which doesn't work for assembly functions without unwind information. */
#define checkasm_context CONTEXT
#define checkasm_save_context() RtlCaptureContext(&checkasm_context_buf)
#define checkasm_load_context() RtlRestoreContext(&checkasm_context_buf, NULL)
#else
#define checkasm_context char
#define checkasm_save_context()
#define checkasm_load_context()
#endif

int xor128_rand(void);
#define rnd xor128_rand

void checkasm_check_rasterizer(unsigned cpu_flag);
void checkasm_check_blend_bitmaps(unsigned cpu_flag);
void checkasm_check_be_blur(unsigned cpu_flag);
void checkasm_check_blur(unsigned cpu_flag);

void *checkasm_check_func(void *func, const char *name, ...);
int checkasm_bench_func(void);
int checkasm_fail_func(const char *msg, ...);
void checkasm_update_bench(int iterations, uint64_t cycles);
void checkasm_report(const char *name, ...);
void checkasm_set_signal_handler_state(int enabled);
extern checkasm_context checkasm_context_buf;

/* float compare utilities */
int float_near_ulp(float a, float b, unsigned max_ulp);
int float_near_abs_eps(float a, float b, float eps);
int float_near_abs_eps_ulp(float a, float b, float eps, unsigned max_ulp);
int float_near_ulp_array(const float *a, const float *b, unsigned max_ulp,
                         int len);
int float_near_abs_eps_array(const float *a, const float *b, float eps,
                             int len);
int float_near_abs_eps_array_ulp(const float *a, const float *b, float eps,
                                 unsigned max_ulp, int len);

#define BENCH_RUNS (1 << 16) /* Trade-off between accuracy and speed */

/* Decide whether or not the specified function needs to be tested */
#define check_func(func, ...)\
    (func_ref = checkasm_check_func((func_new = func), __VA_ARGS__))

/* Declare the function prototype. The first argument is the return value,
 * the remaining arguments are the function parameters. Naming parameters
 * is optional. */
#define declare_func(ret, ...)\
    declare_new(ret, __VA_ARGS__)\
    void *func_ref, *func_new;\
    typedef ret func_type(__VA_ARGS__);\
    checkasm_save_context()

/* Indicate that the current test has failed */
#define fail() checkasm_fail_func("%s:%d", __FILE__, __LINE__)

/* Print the test outcome */
#define report checkasm_report

/* Call the reference function */
#define call_ref(...)\
    (checkasm_set_signal_handler_state(1),\
     ((func_type *)func_ref)(__VA_ARGS__));\
    checkasm_set_signal_handler_state(0)

#if CONFIG_ASM
#if ARCH_X86
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define readtime() (_mm_lfence(), __rdtsc())
#else
static inline uint64_t readtime(void) {
    uint32_t eax, edx;
    __asm__ __volatile__("lfence\nrdtsc" : "=a"(eax), "=d"(edx));
    return (((uint64_t)edx) << 32) | eax;
}
#define readtime readtime
#endif
#elif (ARCH_AARCH64 || ARCH_ARM) && defined(__APPLE__)
#include <mach/mach_time.h>
#define readtime() mach_absolute_time()
#elif ARCH_AARCH64
#ifdef _MSC_VER
#include <windows.h>
#define readtime() (_InstructionSynchronizationBarrier(), ReadTimeStampCounter())
#else
static inline uint64_t readtime(void) {
    uint64_t cycle_counter;
    /* This requires enabling user mode access to the cycle counter (which
     * can only be done from kernel space).
     * This could also read cntvct_el0 instead of pmccntr_el0; that register
     * might also be readable (depending on kernel version), but it has much
     * worse precision (it's a fixed 50 MHz timer). */
    __asm__ __volatile__("isb\nmrs %0, pmccntr_el0"
                         : "=r"(cycle_counter)
                         :: "memory");
    return cycle_counter;
}
#define readtime readtime
#endif
#elif ARCH_ARM && !defined(_MSC_VER) && __ARM_ARCH >= 7
static inline uint64_t readtime(void) {
    uint32_t cycle_counter;
    /* This requires enabling user mode access to the cycle counter (which
     * can only be done from kernel space). */
    __asm__ __volatile__("isb\nmrc p15, 0, %0, c9, c13, 0"
                         : "=r"(cycle_counter)
                         :: "memory");
    return cycle_counter;
}
#define readtime readtime
#elif ARCH_PPC64LE
static inline uint64_t readtime(void) {
    uint32_t tbu, tbl, temp;

    __asm__ __volatile__(
        "1:\n"
        "mfspr %2,269\n"
        "mfspr %0,268\n"
        "mfspr %1,269\n"
        "cmpw   %2,%1\n"
        "bne    1b\n"
    : "=r"(tbl), "=r"(tbu), "=r"(temp)
    :
    : "cc");

    return (((uint64_t)tbu) << 32) | (uint64_t)tbl;
}
#define readtime readtime
#endif

/* Verifies that clobbered callee-saved registers
 * are properly saved and restored */
void checkasm_checked_call(void *func, ...);

#if ARCH_X86_64
/* YMM and ZMM registers on x86 are turned off to save power when they haven't
 * been used for some period of time. When they are used there will be a
 * "warmup" period during which performance will be reduced and inconsistent
 * which is problematic when trying to benchmark individual functions. We can
 * work around this by periodically issuing "dummy" instructions that uses
 * those registers to keep them powered on. */
void checkasm_simd_warmup(void);

/* The upper 32 bits of 32-bit data types are undefined when passed as function
 * parameters. In practice those bits usually end up being zero which may hide
 * certain bugs, such as using a register containing undefined bits as a pointer
 * offset, so we want to intentionally clobber those bits with junk to expose
 * any issues. The following set of macros automatically calculates a bitmask
 * specifying which parameters should have their upper halves clobbered. */
#ifdef _WIN32
/* Integer and floating-point parameters share "register slots". */
#define IGNORED_FP_ARGS 0
#else
/* Up to 8 floating-point parameters are passed in XMM registers, which are
 * handled orthogonally from integer parameters passed in GPR registers. */
#define IGNORED_FP_ARGS 8
#endif
#ifdef HAVE_C11_GENERIC
#define clobber_type(arg) _Generic((void (*)(void*, arg))NULL,\
     void (*)(void*, int32_t ): clobber_mask |= 1 << mpos++,\
     void (*)(void*, uint32_t): clobber_mask |= 1 << mpos++,\
     void (*)(void*, float   ): mpos += (fp_args++ >= IGNORED_FP_ARGS),\
     void (*)(void*, double  ): mpos += (fp_args++ >= IGNORED_FP_ARGS),\
     default:                   mpos++)
#define init_clobber_mask(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, ...)\
    unsigned clobber_mask = 0;\
    {\
        int mpos = 0, fp_args = 0;\
        clobber_type(a); clobber_type(b); clobber_type(c); clobber_type(d);\
        clobber_type(e); clobber_type(f); clobber_type(g); clobber_type(h);\
        clobber_type(i); clobber_type(j); clobber_type(k); clobber_type(l);\
        clobber_type(m); clobber_type(n); clobber_type(o); clobber_type(p);\
    }
#else
/* Skip parameter clobbering on compilers without support for _Generic() */
#define init_clobber_mask(...) unsigned clobber_mask = 0
#endif
#define declare_new(ret, ...)\
    ret (*checked_call)(__VA_ARGS__, int, int, int, int, int, int, int,\
                        int, int, int, int, int, int, int, int, int,\
                        void*, unsigned) =\
        (void*)checkasm_checked_call;\
    init_clobber_mask(__VA_ARGS__, void*, void*, void*, void*,\
                      void*, void*, void*, void*, void*, void*,\
                      void*, void*, void*, void*, void*);
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     checkasm_simd_warmup(),\
     checked_call(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8,\
                  7, 6, 5, 4, 3, 2, 1, func_new, clobber_mask));\
    checkasm_set_signal_handler_state(0)
#elif ARCH_X86_32
#define declare_new(ret, ...)\
    ret (*checked_call)(void *, __VA_ARGS__, int, int, int, int, int, int,\
                        int, int, int, int, int, int, int, int, int) =\
        (void *)checkasm_checked_call;
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     checked_call(func_new, __VA_ARGS__, 15, 14, 13, 12,\
                  11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1));\
    checkasm_set_signal_handler_state(0)
#elif ARCH_ARM
/* Use a dummy argument, to offset the real parameters by 2, not only 1.
 * This makes sure that potential 8-byte-alignment of parameters is kept
 * the same even when the extra parameters have been removed. */
void checkasm_checked_call_vfp(void *func, int dummy, ...);
#define declare_new(ret, ...)\
    ret (*checked_call)(void *, int dummy, __VA_ARGS__,\
                        int, int, int, int, int, int, int, int,\
                        int, int, int, int, int, int, int) =\
    (void *)checkasm_checked_call_vfp;
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     checked_call(func_new, 0, __VA_ARGS__, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0));\
    checkasm_set_signal_handler_state(0)
#elif ARCH_AARCH64 && !defined(__APPLE__)
void checkasm_stack_clobber(uint64_t clobber, ...);
#define declare_new(ret, ...)\
    ret (*checked_call)(void *, int, int, int, int, int, int, int,\
                        __VA_ARGS__, int, int, int, int, int, int, int, int,\
                        int, int, int, int, int, int, int) =\
    (void *)checkasm_checked_call;
#define CLOB (UINT64_C(0xdeadbeefdeadbeef))
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     checkasm_stack_clobber(CLOB, CLOB, CLOB, CLOB, CLOB, CLOB,\
                            CLOB, CLOB, CLOB, CLOB, CLOB, CLOB,\
                            CLOB, CLOB, CLOB, CLOB, CLOB, CLOB,\
                            CLOB, CLOB, CLOB, CLOB, CLOB),\
     checked_call(func_new, 0, 0, 0, 0, 0, 0, 0, __VA_ARGS__,\
                  7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0));\
    checkasm_set_signal_handler_state(0)
#else
#define declare_new(ret, ...)
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     ((func_type *)func_new)(__VA_ARGS__));\
    checkasm_set_signal_handler_state(0)
#endif
#else /* CONFIG_ASM */
#define declare_new(ret, ...)
/* Call the function */
#define call_new(...)\
    (checkasm_set_signal_handler_state(1),\
     ((func_type *)func_new)(__VA_ARGS__));\
    checkasm_set_signal_handler_state(0)
#endif /* CONFIG_ASM */

/* Benchmark the function */
#ifdef readtime
#define bench_new(...)\
    do {\
        if (checkasm_bench_func()) {\
            func_type *const tfunc = func_new;\
            checkasm_set_signal_handler_state(1);\
            uint64_t tsum = 0;\
            int tcount = 0;\
            for (int ti = 0; ti < BENCH_RUNS; ti++) {\
                uint64_t t = readtime();\
                int talt = 0; (void)talt;\
                tfunc(__VA_ARGS__);\
                talt = 1;\
                tfunc(__VA_ARGS__);\
                talt = 0;\
                tfunc(__VA_ARGS__);\
                talt = 1;\
                tfunc(__VA_ARGS__);\
                t = readtime() - t;\
                if (t*tcount <= tsum*4 && ti > 0) {\
                    tsum += t;\
                    tcount++;\
                }\
            }\
            checkasm_set_signal_handler_state(0);\
            checkasm_update_bench(tcount, tsum);\
        } else {\
            const int talt = 0; (void)talt;\
            call_new(__VA_ARGS__);\
        }\
    } while (0)
#else
#define bench_new(...) do {} while (0)
#endif

#ifdef _MSC_VER
#define ALIGN(ll, a) \
    __declspec(align(a)) ll
#else
#define ALIGN(line, align) \
    line __attribute__((aligned(align)))
#endif

#endif /* CHECKASM_CHECKASM_H */
