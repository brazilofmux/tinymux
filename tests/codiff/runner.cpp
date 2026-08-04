/* runner.cpp -- run one RV64 ELF through the interpreter and the DBT with
 * full visibility into the DBT's counters and (optionally) its trace.
 *
 * tests/dbt/dbt_test can already run an ELF both ways, but it prints only
 * blocks/hits/misses and offers no way to turn on DBT_TRACE_*.  This is the
 * same two routes with the internals exposed, so that a divergence can be
 * localised to a translation event rather than merely observed.
 *
 * Env:
 *   RUN_TRACE=1   DBT_TRACE_TRANSLATE
 *   RUN_TRACE=3   + DBT_TRACE_EXEC (very loud)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dbt.h"
#include "dbt_interp.h"
#include "dbt_elf64.h"

static int io_ecall(rv64_state_t *state, void *user)
{
    rv64_memory_t *mem = static_cast<rv64_memory_t *>(user);
    switch (state->x[17]) {
    case 93: return static_cast<int>(state->x[10]);
    case 64: {
        uint64_t buf = state->x[11], len = state->x[12];
        if (buf + len > mem->size) { state->x[10] = (uint64_t)-1LL; return -1; }
        fwrite(mem->data + buf, 1, len, stdout);
        state->x[10] = len;
        return -1;
    }
    default: return -1;
    }
}

struct dctx { uint8_t *memory; size_t size; };

static int dbt_io_ecall(rv64_ctx_t *ctx, void *user)
{
    dctx *d = static_cast<dctx *>(user);
    switch (ctx->x[17]) {
    case 93: return static_cast<int>(ctx->x[10]);
    case 64: {
        uint64_t buf = ctx->x[11], len = ctx->x[12];
        if (buf + len > d->size) { ctx->x[10] = (uint64_t)-1LL; return -1; }
        fwrite(d->memory + buf, 1, len, stdout);
        ctx->x[10] = len;
        return -1;
    }
    default: return -1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: runner <elf>\n"); return 2; }
    const char *tr = getenv("RUN_TRACE");
    int trace = tr ? atoi(tr) : 0;

    /* ---- interpreter ---- */
    {
        rv64_binary_t bin;
        if (rv64_load_elf(argv[1], &bin) != 0) return 2;
        rv64_state_t st = {};
        st.pc = bin.entry_point;
        st.x[2] = bin.stack_top;
        rv64_memory_t mem = { bin.memory, bin.memory_size };
        printf("--- interp ---\n");
        fflush(stdout);
        rv64_interp_run(&st, &mem, io_ecall, &mem);
        fflush(stdout);
        rv64_free_binary(&bin);
    }

    /* ---- DBT ----
     * RUN_REPS>1 repeats the whole init/run/cleanup cycle in one process,
     * to test whether the divergence needs process-global DBT state rather
     * than anything in this ELF. */
    const char *rp = getenv("RUN_REPS");
    int reps = rp ? atoi(rp) : 1;
    for (int rep = 0; rep < reps; rep++) {
        rv64_binary_t bin;
        if (rv64_load_elf(argv[1], &bin) != 0) return 2;
        dctx d = { bin.memory, bin.memory_size };
        dbt_state_t dbt;
        if (dbt_init(&dbt, bin.memory, bin.memory_size, dbt_io_ecall, &d) != 0) {
            fprintf(stderr, "dbt_init failed\n");
            return 2;
        }
        dbt.trace = trace;
        printf("--- dbt rep=%d ---\n", rep);
        fflush(stdout);
        int rc = dbt_run(&dbt, bin.entry_point, bin.stack_top);
        fflush(stdout);
        printf("rc=%d blocks=%llu hits=%llu misses=%llu chain_hits=%llu "
               "chain_misses=%llu code_full=%llu reclaims=%u code_used=%u "
               "blob_end=%u\n",
               rc,
               (unsigned long long)dbt.blocks_translated,
               (unsigned long long)dbt.cache_hits,
               (unsigned long long)dbt.cache_misses,
               (unsigned long long)dbt.chain_hits,
               (unsigned long long)dbt.chain_misses,
               (unsigned long long)dbt.code_full,
               dbt.reclaims_this_run,
               dbt.code_used, dbt.blob_code_end);
        dbt_cleanup(&dbt);
        rv64_free_binary(&bin);
    }
    return 0;
}
