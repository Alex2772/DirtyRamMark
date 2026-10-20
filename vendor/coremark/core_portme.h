/*
 * CoreMark port for DirtyRamMark: only the algorithms (list, matrix, state machine) are used, the framework (core_main.c)
 * is replaced by coremark_kernel.c which runs one independent context per worker thread.
 * Based on simple/core_portme.h of https://github.com/eembc/coremark (Apache License 2.0, see LICENSE.md).
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>

#define HAS_FLOAT  0
#define HAS_TIME_H 0
#define USE_CLOCK  0
#define HAS_STDIO  0
#define HAS_PRINTF 0

typedef unsigned long CORE_TICKS;

#define COMPILER_VERSION "unspecified"
#define COMPILER_FLAGS   "-O2"
#define MEM_LOCATION     "Heap"

typedef signed short   ee_s16;
typedef unsigned short ee_u16;
typedef signed int     ee_s32;
typedef double         ee_f32;
typedef unsigned char  ee_u8;
typedef unsigned int   ee_u32;
typedef size_t         ee_ptr_int;
typedef size_t         ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD  MEM_MALLOC

#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK    0
#define USE_SOCKET  0

#define MAIN_HAS_NOARGC    0
#define MAIN_HAS_NORETURN  0

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

#ifndef ee_printf
#define ee_printf(...) ((void)0)
#endif

#endif /* CORE_PORTME_H */
