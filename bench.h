#pragma once

#if !defined(__x86_64__)
#warning "Not using an amd64 cpu: benchmarking code disabled"

#define BENCH(      name, args... ) name(args)
#define BENCH_VOID( name, args... ) name(args)


#else



static inline uint64_t rdtscll()
{
    // taken from the wikipedia rdtsc article
    uint32_t lo, hi;
    /* We cannot use "=A", since this would use %rax on x86_64 */
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (uint64_t)hi << 32 | lo;
}
// to time a function call "f(a,b,c)", change it to "BENCH(f,a,b,c)". This uses
// __auto_type, so gcc-4.9 or higher is required
#define BENCH( name, args... )                                          \
    ({                                                                  \
        uint64_t t0 = rdtscll();                                        \
        __auto_type res = name(args);                                   \
        uint64_t t1 = rdtscll();                                        \
        fprintf(stderr, #name " took %g cycles\n", (double)(t1-t0));    \
        res;                                                            \
    })
#define BENCH_VOID( name, args... )                                     \
    ({                                                                  \
        uint64_t t0 = rdtscll();                                        \
        name(args);                                                     \
        uint64_t t1 = rdtscll();                                        \
        fprintf(stderr, #name " took %g cycles\n", (double)(t1-t0));    \
        1;                                                              \
    })

#endif
