/*! \file stress_backend.cpp
 * \brief Stage 5a: Stress test for IStorageBackend (SQLite and mdbx).
 *
 * Exercises Put/Get/Del/GetAll at scale with random access patterns.
 * Runs against either backend via command-line selection.
 *
 * Usage:
 *   ./stress_backend sqlite [objects=10000] [attrs=10] [ops=100000]
 *   ./stress_backend mdbx   [objects=10000] [attrs=10] [ops=100000]
 */

#include "storage_backend.h"
#include "sqlite_backend.h"
#include "mdbx_backend.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int parse_param(int argc, char *argv[], const char *name, int def)
{
    size_t nlen = strlen(name);
    for (int i = 2; i < argc; i++)
    {
        if (strncmp(argv[i], name, nlen) == 0 && argv[i][nlen] == '=')
        {
            return atoi(argv[i] + nlen + 1);
        }
    }
    return def;
}

using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point t0, Clock::time_point t1)
{
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: stress_backend <sqlite|mdbx>"
                        " [objects=N] [attrs=N] [ops=N]\n");
        return 1;
    }

    const char *backend_name = argv[1];
    int n_objects = parse_param(argc, argv, "objects", 10000);
    int n_attrs   = parse_param(argc, argv, "attrs",   10);
    int n_ops     = parse_param(argc, argv, "ops",     100000);

    // Create backend.
    //
    std::unique_ptr<IStorageBackend> be;
    char tmppath[256];
    bool is_mdbx = false;

    if (strcmp(backend_name, "sqlite") == 0)
    {
        snprintf(tmppath, sizeof(tmppath), "/tmp/stress_sqlite_%d.db", getpid());
        be = std::make_unique<CSQLiteBackend>();
        if (!be->Open(tmppath))
        {
            fprintf(stderr, "Failed to open SQLite backend at %s\n", tmppath);
            return 1;
        }
    }
    else if (strcmp(backend_name, "mdbx") == 0)
    {
        snprintf(tmppath, sizeof(tmppath), "/tmp/stress_mdbx_%d", getpid());
        be = std::make_unique<CMdbxBackend>();
        if (!be->Open(tmppath))
        {
            fprintf(stderr, "Failed to open mdbx backend at %s\n", tmppath);
            return 1;
        }
        is_mdbx = true;
    }
    else
    {
        fprintf(stderr, "Unknown backend: %s (use sqlite or mdbx)\n",
                backend_name);
        return 1;
    }

    printf("=== Stress Test: %s backend ===\n", backend_name);
    printf("  objects=%d  attrs=%d  ops=%d\n\n", n_objects, n_attrs, n_ops);

    // Prepare a reusable value buffer (256 bytes).
    //
    UTF8 value[256];
    memset(value, 'S', sizeof(value));
    value[255] = '\0';

    UTF8 readbuf[8192];
    size_t rlen;

    std::mt19937 rng(42);  // Fixed seed for reproducibility.

    // -------------------------------------------------------------------
    // Phase 1: Populate
    // -------------------------------------------------------------------

    int total_puts = n_objects * n_attrs;
    printf("Phase 1: Populate (%d objects x %d attrs = %d entries)\n",
           n_objects, n_attrs, total_puts);

    auto t0 = Clock::now();
    for (int obj = 0; obj < n_objects; obj++)
    {
        for (int attr = 1; attr <= n_attrs; attr++)
        {
            be->Put(obj, attr, value, 256, 1, 0);
        }
    }
    be->Sync();
    auto t1 = Clock::now();
    double ms = elapsed_ms(t0, t1);

    printf("STRESS_POPULATE:  %-8s %7d puts  %8.1f ms  %6.2f us/op\n",
           backend_name, total_puts, ms, (ms * 1000.0) / total_puts);

    // -------------------------------------------------------------------
    // Phase 2: Random read/write/delete mix
    // -------------------------------------------------------------------

    printf("\nPhase 2: Random mix (%d ops: 80%% read, 15%% write, 5%% del)\n",
           n_ops);

    std::uniform_int_distribution<int> obj_dist(0, n_objects - 1);
    std::uniform_int_distribution<int> attr_dist(1, n_attrs);
    std::uniform_int_distribution<int> pct_dist(0, 99);

    int read_count = 0, write_count = 0, del_count = 0;
    double read_ms = 0, write_ms = 0, del_ms = 0;

    for (int i = 0; i < n_ops; i++)
    {
        int obj  = obj_dist(rng);
        int attr = attr_dist(rng);
        int pct  = pct_dist(rng);

        if (pct < 80)
        {
            // Read
            auto r0 = Clock::now();
            be->Get(obj, attr, readbuf, sizeof(readbuf), &rlen,
                    nullptr, nullptr);
            auto r1 = Clock::now();
            read_ms += elapsed_ms(r0, r1);
            read_count++;
        }
        else if (pct < 95)
        {
            // Write
            auto w0 = Clock::now();
            be->Put(obj, attr, value, 256, 1, 0);
            auto w1 = Clock::now();
            write_ms += elapsed_ms(w0, w1);
            write_count++;
        }
        else
        {
            // Delete
            auto d0 = Clock::now();
            be->Del(obj, attr);
            auto d1 = Clock::now();
            del_ms += elapsed_ms(d0, d1);
            del_count++;
        }
    }
    be->Sync();

    if (read_count > 0)
        printf("STRESS_READ:      %-8s %7d gets  %8.1f ms  %6.2f us/op\n",
               backend_name, read_count, read_ms,
               (read_ms * 1000.0) / read_count);
    if (write_count > 0)
        printf("STRESS_WRITE:     %-8s %7d puts  %8.1f ms  %6.2f us/op\n",
               backend_name, write_count, write_ms,
               (write_ms * 1000.0) / write_count);
    if (del_count > 0)
        printf("STRESS_DELETE:    %-8s %7d dels  %8.1f ms  %6.2f us/op\n",
               backend_name, del_count, del_ms,
               (del_ms * 1000.0) / del_count);

    // -------------------------------------------------------------------
    // Phase 3: GetAll bulk load
    // -------------------------------------------------------------------

    int getall_count = 1000;
    printf("\nPhase 3: GetAll (%d random objects)\n", getall_count);

    auto g0 = Clock::now();
    for (int i = 0; i < getall_count; i++)
    {
        int obj = obj_dist(rng);
        be->GetAll(obj, [](unsigned int, const UTF8 *, size_t, int, int) {});
    }
    auto g1 = Clock::now();
    ms = elapsed_ms(g0, g1);

    printf("STRESS_GETALL:    %-8s %7d calls %8.1f ms  %6.2f us/op\n",
           backend_name, getall_count, ms, (ms * 1000.0) / getall_count);

    // -------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------

    be->Close();

    // Remove temp files.
    //
    if (is_mdbx)
    {
        char buf[300];
        snprintf(buf, sizeof(buf), "rm -rf %s", tmppath);
        system(buf);
    }
    else
    {
        unlink(tmppath);
    }

    printf("\n=== Stress Test: %s complete ===\n", backend_name);
    return 0;
}
