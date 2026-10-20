/* See coremark_kernel.h. The framework part is derived from core_main.c / simple/core_portme.c of EEMBC CoreMark. */
#include "coremark_kernel.h"

#include <stdlib.h>

#include "coremark.h"

ee_s32 get_seed_32(int i);   /* core_util.c */

/* performance run parameters; read through volatile so the compiler can't fold them (that is what the rules require) */
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = 0;
volatile ee_s32 seed5_volatile = 0;
ee_u32 default_num_contexts = 1;

void* portable_malloc(ee_size_t size) { return malloc(size); }
void portable_free(void* p) { free(p); }

static const ee_u16 LIST_CRC = 0xe714, MATRIX_CRC = 0x1fd7, STATE_CRC = 0x8e3a;

struct coremark_context {
    core_results results;
};

void* iterate(void* pres) {
    core_results* res = (core_results*)pres;
    const ee_u32 iterations = res->iterations;
    res->crc = 0;
    res->crclist = 0;
    res->crcmatrix = 0;
    res->crcstate = 0;
    for (ee_u32 i = 0; i < iterations; i++) {
        ee_u16 crc = core_bench_list(res, 1);
        res->crc = crcu16(crc, res->crc);
        crc = core_bench_list(res, -1);
        res->crc = crcu16(crc, res->crc);
        if (i == 0) {
            res->crclist = res->crc;
        }
    }
    return NULL;
}

coremark_context* coremark_create(void) {
    coremark_context* ctx = (coremark_context*)calloc(1, sizeof(coremark_context));
    if (!ctx) {
        return NULL;
    }
    core_results* r = &ctx->results;
    r->seed1 = (ee_s16)get_seed_32(1);
    r->seed2 = (ee_s16)get_seed_32(2);
    r->seed3 = (ee_s16)get_seed_32(3);
    r->execs = ALL_ALGORITHMS_MASK;
    if (r->seed1 == 0 && r->seed2 == 0 && r->seed3 == 0) {
        r->seed3 = 0x66;
    }
    r->memblock[0] = portable_malloc(TOTAL_DATA_SIZE);
    if (!r->memblock[0]) {
        free(ctx);
        return NULL;
    }
    r->size = TOTAL_DATA_SIZE / NUM_ALGORITHMS;   /* 666 bytes per algorithm: "2K performance run" */
    for (int i = 0; i < NUM_ALGORITHMS; i++) {
        r->memblock[i + 1] = (char*)r->memblock[0] + r->size * i;
    }
    r->list = core_list_init(r->size, (list_head*)r->memblock[1], r->seed1);
    core_init_matrix(r->size, r->memblock[2], (ee_s32)r->seed1 | (((ee_s32)r->seed2) << 16), &r->mat);
    core_init_state(r->size, r->seed1, (ee_u8*)r->memblock[3]);
    return ctx;
}

void coremark_destroy(coremark_context* ctx) {
    if (ctx) {
        portable_free(ctx->results.memblock[0]);
        free(ctx);
    }
}

void coremark_run(coremark_context* ctx, unsigned iterations) {
    ctx->results.iterations = iterations;
    iterate(&ctx->results);
}

int coremark_validate(const coremark_context* ctx) {
    const core_results* r = &ctx->results;
    return r->crclist == LIST_CRC && r->crcmatrix == MATRIX_CRC && r->crcstate == STATE_CRC;
}
