/*
 * Thin wrapper over the CoreMark algorithms (EEMBC, Apache License 2.0) that replaces core_main.c: it lets a worker thread
 * run CoreMark iterations in small chunks. One context = one copy of the benchmark, like `make XCFLAGS=-DMULTITHREAD=N`.
 */
#ifndef COREMARK_KERNEL_H
#define COREMARK_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct coremark_context coremark_context;

/// Allocates and seeds a context with the standard 2000-byte performance run parameters (seeds 0, 0, 0x66).
coremark_context* coremark_create(void);
void coremark_destroy(coremark_context* ctx);

/// Executes `iterations` CoreMark iterations. One iteration = what the official "Iterations" counter counts.
void coremark_run(coremark_context* ctx, unsigned iterations);

/// Checks the CRCs of the first iteration against the known values of the official performance run. Call it on a fresh
/// context, right after the first coremark_run(). Returns 1 if the kernel computes what the reference one does.
int coremark_validate(const coremark_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* COREMARK_KERNEL_H */
