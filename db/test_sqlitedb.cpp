/*! \file test_sqlitedb.cpp
 * \brief Test harness for CSQLiteDB.
 *
 * Validates basic CRUD operations, transactions, bulk loading, and
 * write-through performance characteristics.
 */

#include "sqlitedb.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <chrono>
#include <vector>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct Register_##name { \
        Register_##name() { test_##name(); } \
    } reg_##name; \
    static void test_##name()

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAILED: %s:%d: %s != %s (%d != %d)\n", \
            __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define PASS() do { g_tests_passed++; printf("  PASSED: %s\n", __func__); } while (0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(open_close)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));
    ASSERT_TRUE(db.IsOpen());
    db.Close();
    ASSERT_TRUE(!db.IsOpen());
    PASS();
}

TEST(insert_load_object)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    CSQLiteDB::ObjectRecord obj = {};
    obj.dbref_val = 0;
    obj.location  = -1;
    obj.contents  = 1;
    obj.exits     = -1;
    obj.next      = -1;
    obj.link      = 0;
    obj.owner     = 1;
    obj.parent    = -1;
    obj.zone      = -1;
    obj.pennies   = 0;
    obj.flags1    = 0x10;
    obj.flags2    = 0x20;
    obj.flags3    = 0;
    obj.powers1   = 0;
    obj.powers2   = 0;
    ASSERT_TRUE(db.InsertObject(obj));

    CSQLiteDB::ObjectRecord loaded = {};
    ASSERT_TRUE(db.LoadObject(0, loaded));
    ASSERT_EQ(loaded.dbref_val, 0);
    ASSERT_EQ(loaded.location, -1);
    ASSERT_EQ(loaded.contents, 1);
    ASSERT_EQ(loaded.owner, 1);
    ASSERT_EQ(loaded.flags1, 0x10u);
    ASSERT_EQ(loaded.flags2, 0x20u);

    db.Close();
    PASS();
}

TEST(update_fields)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    CSQLiteDB::ObjectRecord obj = {};
    obj.dbref_val = 5;
    obj.location  = 0;
    obj.contents  = -1;
    obj.exits     = -1;
    obj.next      = -1;
    obj.link      = 0;
    obj.owner     = 1;
    obj.parent    = -1;
    obj.zone      = -1;
    ASSERT_TRUE(db.InsertObject(obj));

    ASSERT_TRUE(db.UpdateLocation(5, 3));
    ASSERT_TRUE(db.UpdateOwner(5, 2));
    ASSERT_TRUE(db.UpdateFlags(5, 0xFF, 0xAA, 0x55));
    ASSERT_TRUE(db.UpdatePennies(5, 1000));

    CSQLiteDB::ObjectRecord loaded = {};
    ASSERT_TRUE(db.LoadObject(5, loaded));
    ASSERT_EQ(loaded.location, 3);
    ASSERT_EQ(loaded.owner, 2);
    ASSERT_EQ(loaded.flags1, 0xFFu);
    ASSERT_EQ(loaded.flags2, 0xAAu);
    ASSERT_EQ(loaded.flags3, 0x55u);
    ASSERT_EQ(loaded.pennies, 1000);

    db.Close();
    PASS();
}

TEST(delete_object)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    CSQLiteDB::ObjectRecord obj = {};
    obj.dbref_val = 10;
    ASSERT_TRUE(db.InsertObject(obj));

    CSQLiteDB::ObjectRecord loaded = {};
    ASSERT_TRUE(db.LoadObject(10, loaded));

    ASSERT_TRUE(db.DeleteObject(10));
    ASSERT_TRUE(!db.LoadObject(10, loaded));

    db.Close();
    PASS();
}

TEST(load_all_objects)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    for (int i = 0; i < 100; i++)
    {
        CSQLiteDB::ObjectRecord obj = {};
        obj.dbref_val = i;
        obj.location  = i - 1;
        obj.owner     = 1;
        ASSERT_TRUE(db.InsertObject(obj));
    }

    int count = 0;
    db.LoadAllObjects([&](const CSQLiteDB::ObjectRecord &rec)
    {
        ASSERT_EQ(rec.dbref_val, count);
        ASSERT_EQ(rec.location, count - 1);
        count++;
    });
    ASSERT_EQ(count, 100);

    db.Close();
    PASS();
}

TEST(attribute_crud)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    const UTF8 *value = (const UTF8 *)"Hello, World!";
    size_t vlen = strlen((const char *)value) + 1;
    ASSERT_TRUE(db.PutAttribute(0, 42, value, vlen));

    UTF8 buf[256];
    size_t rlen = 0;
    ASSERT_TRUE(db.GetAttribute(0, 42, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, vlen);
    ASSERT_TRUE(0 == memcmp(buf, value, vlen));

    // Update
    const UTF8 *value2 = (const UTF8 *)"Updated";
    size_t vlen2 = strlen((const char *)value2) + 1;
    ASSERT_TRUE(db.PutAttribute(0, 42, value2, vlen2));
    ASSERT_TRUE(db.GetAttribute(0, 42, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, vlen2);
    ASSERT_TRUE(0 == memcmp(buf, value2, vlen2));

    // Delete
    ASSERT_TRUE(db.DelAttribute(0, 42));
    ASSERT_TRUE(!db.GetAttribute(0, 42, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, (size_t)0);

    db.Close();
    PASS();
}

TEST(attribute_get_all)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    // Set 50 attributes on object #7.
    //
    for (int i = 1; i <= 50; i++)
    {
        char val[64];
        snprintf(val, sizeof(val), "attr_value_%d", i);
        ASSERT_TRUE(db.PutAttribute(7, i,
            (const UTF8 *)val, strlen(val) + 1));
    }

    // Also set some on a different object to make sure they're not returned.
    //
    ASSERT_TRUE(db.PutAttribute(8, 1, (const UTF8 *)"other", 6));

    int count = 0;
    db.GetAllAttributes(7, [&](int attrnum, const UTF8 *value, size_t len)
    {
        count++;
        (void)attrnum;
        (void)value;
        (void)len;
    });
    ASSERT_EQ(count, 50);

    db.Close();
    PASS();
}

TEST(del_all_attributes)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    for (int i = 1; i <= 10; i++)
    {
        ASSERT_TRUE(db.PutAttribute(3, i, (const UTF8 *)"x", 2));
    }
    ASSERT_TRUE(db.PutAttribute(4, 1, (const UTF8 *)"keep", 5));

    ASSERT_TRUE(db.DelAllAttributes(3));

    // All attrs on #3 should be gone.
    //
    int count = 0;
    db.GetAllAttributes(3, [&](int, const UTF8 *, size_t) { count++; });
    ASSERT_EQ(count, 0);

    // Attr on #4 should still exist.
    //
    UTF8 buf[64];
    size_t rlen;
    ASSERT_TRUE(db.GetAttribute(4, 1, buf, sizeof(buf), &rlen));

    db.Close();
    PASS();
}

TEST(transactions)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    ASSERT_TRUE(db.Begin());
    for (int i = 0; i < 10; i++)
    {
        CSQLiteDB::ObjectRecord obj = {};
        obj.dbref_val = i;
        ASSERT_TRUE(db.InsertObject(obj));
    }
    ASSERT_TRUE(db.Commit());

    // Verify all objects exist.
    //
    int count = 0;
    db.LoadAllObjects([&](const CSQLiteDB::ObjectRecord &) { count++; });
    ASSERT_EQ(count, 10);

    // Rollback test.
    //
    ASSERT_TRUE(db.Begin());
    CSQLiteDB::ObjectRecord obj = {};
    obj.dbref_val = 999;
    ASSERT_TRUE(db.InsertObject(obj));
    ASSERT_TRUE(db.Rollback());

    CSQLiteDB::ObjectRecord loaded = {};
    ASSERT_TRUE(!db.LoadObject(999, loaded));

    db.Close();
    PASS();
}

TEST(statistics)
{
    CSQLiteDB db;
    ASSERT_TRUE(db.Open(":memory:"));

    db.ResetStats();

    CSQLiteDB::ObjectRecord obj = {};
    obj.dbref_val = 0;
    db.InsertObject(obj);
    db.InsertObject(obj);  // replace
    db.UpdateLocation(0, 5);
    db.PutAttribute(0, 1, (const UTF8 *)"x", 2);

    UTF8 buf[64];
    size_t rlen;
    db.GetAttribute(0, 1, buf, sizeof(buf), &rlen);
    db.DelAttribute(0, 1);

    CSQLiteDB::Stats s = db.GetStats();
    ASSERT_EQ(s.obj_inserts, (uint64_t)2);
    ASSERT_EQ(s.obj_updates, (uint64_t)1);
    ASSERT_EQ(s.attr_puts, (uint64_t)1);
    ASSERT_EQ(s.attr_gets, (uint64_t)1);
    ASSERT_EQ(s.attr_dels, (uint64_t)1);

    db.Close();
    PASS();
}

// ---------------------------------------------------------------------------
// Performance benchmarks
// ---------------------------------------------------------------------------

static void bench_write_through()
{
    printf("\n--- Write-Through Performance ---\n");

    CSQLiteDB db;
    db.Open(":memory:");

    // Benchmark: individual attribute writes (no explicit transaction).
    // This simulates the write-through path during normal gameplay.
    //
    const int N = 10000;
    UTF8 value[256];
    memset(value, 'A', sizeof(value));
    value[255] = '\0';

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; i++)
    {
        db.PutAttribute(i % 100, (i / 100) + 1, value, 256);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d individual attr writes: %.1f ms (%.1f us/write)\n",
        N, ms, (ms * 1000.0) / N);

    // Benchmark: batched writes in a transaction.
    // This simulates bulk operations like object creation.
    //
    t0 = std::chrono::high_resolution_clock::now();
    db.Begin();
    for (int i = 0; i < N; i++)
    {
        db.PutAttribute(i % 100, (i / 100) + 200, value, 256);
    }
    db.Commit();
    t1 = std::chrono::high_resolution_clock::now();

    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d batched attr writes:     %.1f ms (%.1f us/write)\n",
        N, ms, (ms * 1000.0) / N);

    // Benchmark: attribute reads (cache miss — hitting SQLite).
    //
    size_t rlen;
    UTF8 buf[256];
    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; i++)
    {
        db.GetAttribute(i % 100, (i / 100) + 1, buf, sizeof(buf), &rlen);
    }
    t1 = std::chrono::high_resolution_clock::now();

    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d attr reads:              %.1f ms (%.1f us/read)\n",
        N, ms, (ms * 1000.0) / N);

    // Benchmark: object metadata updates (write-through s_Location etc.)
    //
    CSQLiteDB::ObjectRecord obj = {};
    for (int i = 0; i < 100; i++)
    {
        obj.dbref_val = i;
        db.InsertObject(obj);
    }

    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; i++)
    {
        db.UpdateLocation(i % 100, (i + 1) % 100);
    }
    t1 = std::chrono::high_resolution_clock::now();

    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d location updates:        %.1f ms (%.1f us/update)\n",
        N, ms, (ms * 1000.0) / N);

    // Benchmark: bulk attribute load (preloading an object).
    //
    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; i++)
    {
        db.GetAllAttributes(i % 100, [](int, const UTF8 *, size_t) {});
    }
    t1 = std::chrono::high_resolution_clock::now();

    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d bulk object loads:       %.1f ms (%.1f us/load)\n",
        1000, ms, (ms * 1000.0) / 1000);

    // Benchmark: checkpoint.
    //
    t0 = std::chrono::high_resolution_clock::now();
    db.Checkpoint();
    t1 = std::chrono::high_resolution_clock::now();

    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  Checkpoint:                 %.1f ms\n", ms);

    db.Close();
}

// ---------------------------------------------------------------------------
// Disk-based benchmarks
// ---------------------------------------------------------------------------

static void bench_disk_realistic()
{
    printf("\n--- Disk-Based Realistic Workload ---\n");

    // Clean up any previous run.
    //
    remove("bench_test.db");
    remove("bench_test.db-wal");
    remove("bench_test.db-shm");

    CSQLiteDB db;
    if (!db.Open("bench_test.db"))
    {
        fprintf(stderr, "  Failed to open bench_test.db\n");
        return;
    }

    // Phase 1: Populate — 10,000 objects with 20 attributes each.
    // This simulates loading a medium-sized game database.
    //
    const int NUM_OBJECTS = 10000;
    const int ATTRS_PER_OBJ = 20;

    printf("  Populating %d objects x %d attrs = %d total attrs...\n",
        NUM_OBJECTS, ATTRS_PER_OBJ, NUM_OBJECTS * ATTRS_PER_OBJ);

    auto t0 = std::chrono::high_resolution_clock::now();

    db.Begin();
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        CSQLiteDB::ObjectRecord obj = {};
        obj.dbref_val = i;
        obj.location  = (i > 0) ? (i % 500) : -1;  // 500 rooms
        obj.contents  = -1;
        obj.exits     = -1;
        obj.next      = -1;
        obj.link      = 0;
        obj.owner     = i % 100;
        obj.parent    = -1;
        obj.zone      = i % 10;
        obj.pennies   = 100;
        obj.flags1    = 0x06;  // TYPE_THING
        db.InsertObject(obj);

        for (int a = 1; a <= ATTRS_PER_OBJ; a++)
        {
            char val[256];
            int len = snprintf(val, sizeof(val),
                "Attribute value for object #%d attr %d. "
                "This is a realistic-length attribute string.", i, a);
            db.PutAttribute(i, a, (const UTF8 *)val, len + 1);
        }
    }
    db.Commit();

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  Population:                 %.0f ms (%.1f us/attr)\n",
        ms, (ms * 1000.0) / (NUM_OBJECTS * ATTRS_PER_OBJ));

    // Checkpoint to flush WAL so steady-state starts clean.
    //
    db.Checkpoint();

    // Phase 2: Steady-state gameplay simulation.
    //
    // A realistic command mix for one "second" of a busy game:
    //   - 500 attribute reads   (look, examine, $commands, locks)
    //   - 50 attribute writes   (@set, counters, +sheet)
    //   - 30 location updates   (movement — contents/next/location)
    //   - 5 bulk object loads   (entering a room — preload contents)
    //   - 1 object creation     (occasional @create)
    //
    // We run 10 "seconds" to get stable numbers.
    //
    printf("\n  Steady-state simulation (10 rounds of mixed operations):\n");

    const int ROUNDS = 10;
    const int READS_PER_ROUND = 500;
    const int WRITES_PER_ROUND = 50;
    const int MOVES_PER_ROUND = 30;
    const int PRELOADS_PER_ROUND = 5;

    UTF8 buf[8192];
    size_t rlen;
    uint32_t lcg = 12345;  // Simple LCG for deterministic "random" access

    // Track worst-case latencies.
    //
    double worst_write_us = 0.0;
    double worst_read_us = 0.0;
    double worst_move_us = 0.0;
    double worst_preload_us = 0.0;

    std::vector<double> round_times;

    for (int round = 0; round < ROUNDS; round++)
    {
        auto round_start = std::chrono::high_resolution_clock::now();

        // Attribute reads.
        //
        for (int i = 0; i < READS_PER_ROUND; i++)
        {
            lcg = lcg * 1664525 + 1013904223;
            int obj = lcg % NUM_OBJECTS;
            int attr = (lcg >> 16) % ATTRS_PER_OBJ + 1;

            auto wt0 = std::chrono::high_resolution_clock::now();
            db.GetAttribute(obj, attr, buf, sizeof(buf), &rlen);
            auto wt1 = std::chrono::high_resolution_clock::now();

            double us = std::chrono::duration<double, std::micro>(wt1 - wt0).count();
            if (us > worst_read_us) worst_read_us = us;
        }

        // Attribute writes (write-through, no explicit transaction).
        //
        for (int i = 0; i < WRITES_PER_ROUND; i++)
        {
            lcg = lcg * 1664525 + 1013904223;
            int obj = lcg % NUM_OBJECTS;
            int attr = (lcg >> 16) % ATTRS_PER_OBJ + 1;

            char val[256];
            int len = snprintf(val, sizeof(val),
                "Updated value round %d write %d on #%d/%d", round, i, obj, attr);

            auto wt0 = std::chrono::high_resolution_clock::now();
            db.PutAttribute(obj, attr, (const UTF8 *)val, len + 1);
            auto wt1 = std::chrono::high_resolution_clock::now();

            double us = std::chrono::duration<double, std::micro>(wt1 - wt0).count();
            if (us > worst_write_us) worst_write_us = us;
        }

        // Location updates (simulating player movement).
        // Each move touches location + contents + next on 2-3 objects.
        //
        for (int i = 0; i < MOVES_PER_ROUND; i++)
        {
            lcg = lcg * 1664525 + 1013904223;
            int obj = 500 + (lcg % (NUM_OBJECTS - 500));  // non-room object
            int dest = lcg % 500;  // move to a room

            auto wt0 = std::chrono::high_resolution_clock::now();
            db.Begin();
            db.UpdateLocation(obj, dest);
            db.UpdateContents(dest, obj);
            db.UpdateNext(obj, -1);
            db.Commit();
            auto wt1 = std::chrono::high_resolution_clock::now();

            double us = std::chrono::duration<double, std::micro>(wt1 - wt0).count();
            if (us > worst_move_us) worst_move_us = us;
        }

        // Bulk object preloads (entering a room).
        //
        for (int i = 0; i < PRELOADS_PER_ROUND; i++)
        {
            lcg = lcg * 1664525 + 1013904223;
            int obj = lcg % NUM_OBJECTS;

            auto wt0 = std::chrono::high_resolution_clock::now();
            db.GetAllAttributes(obj, [](int, const UTF8 *, size_t) {});
            auto wt1 = std::chrono::high_resolution_clock::now();

            double us = std::chrono::duration<double, std::micro>(wt1 - wt0).count();
            if (us > worst_preload_us) worst_preload_us = us;
        }

        // Occasional object creation.
        //
        {
            CSQLiteDB::ObjectRecord obj = {};
            obj.dbref_val = NUM_OBJECTS + round;
            obj.location  = 0;
            obj.owner     = 1;
            obj.flags1    = 0x06;
            db.Begin();
            db.InsertObject(obj);
            for (int a = 1; a <= ATTRS_PER_OBJ; a++)
            {
                char val[128];
                int len = snprintf(val, sizeof(val), "New object attr %d", a);
                db.PutAttribute(obj.dbref_val, a, (const UTF8 *)val, len + 1);
            }
            db.Commit();
        }

        auto round_end = std::chrono::high_resolution_clock::now();
        double round_ms = std::chrono::duration<double, std::milli>(round_end - round_start).count();
        round_times.push_back(round_ms);
    }

    // Report results.
    //
    double total_ms = 0;
    for (double t : round_times) total_ms += t;
    double avg_ms = total_ms / ROUNDS;
    double max_ms = *std::max_element(round_times.begin(), round_times.end());

    printf("    Round time (avg):         %.1f ms\n", avg_ms);
    printf("    Round time (worst):       %.1f ms\n", max_ms);
    printf("    Worst single write:       %.1f us\n", worst_write_us);
    printf("    Worst single read:        %.1f us\n", worst_read_us);
    printf("    Worst move (3 updates):   %.1f us\n", worst_move_us);
    printf("    Worst preload (20 attrs): %.1f us\n", worst_preload_us);

    int ops_per_round = READS_PER_ROUND + WRITES_PER_ROUND
        + MOVES_PER_ROUND * 3 + PRELOADS_PER_ROUND + ATTRS_PER_OBJ + 1;
    printf("    Ops per round:            %d\n", ops_per_round);
    printf("    Avg us/op:                %.1f\n", (avg_ms * 1000.0) / ops_per_round);

    // Phase 3: Checkpoint under load.
    //
    printf("\n  Checkpoint (after steady-state):\n");
    t0 = std::chrono::high_resolution_clock::now();
    db.Checkpoint();
    t1 = std::chrono::high_resolution_clock::now();
    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("    Checkpoint time:          %.1f ms\n", ms);

    // Phase 4: Full database load (simulating startup/restart).
    //
    printf("\n  Full database load (startup simulation):\n");
    int obj_count = 0;
    t0 = std::chrono::high_resolution_clock::now();
    db.LoadAllObjects([&](const CSQLiteDB::ObjectRecord &)
    {
        obj_count++;
    });
    t1 = std::chrono::high_resolution_clock::now();
    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("    Load %d objects:         %.1f ms\n", obj_count, ms);

    // Report final statistics.
    //
    CSQLiteDB::Stats s = db.GetStats();
    printf("\n  Cumulative statistics:\n");
    printf("    obj_inserts:  %lu\n", (unsigned long)s.obj_inserts);
    printf("    obj_updates:  %lu\n", (unsigned long)s.obj_updates);
    printf("    obj_loads:    %lu\n", (unsigned long)s.obj_loads);
    printf("    attr_gets:    %lu\n", (unsigned long)s.attr_gets);
    printf("    attr_puts:    %lu\n", (unsigned long)s.attr_puts);
    printf("    attr_dels:    %lu\n", (unsigned long)s.attr_dels);
    printf("    attr_bulk:    %lu\n", (unsigned long)s.attr_bulk_loads);

    db.Close();

    // Report file sizes.
    //
    FILE *f = fopen("bench_test.db", "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        printf("\n  Database file size:         %.1f MB\n", sz / (1024.0 * 1024.0));
    }

    // Clean up.
    //
    remove("bench_test.db");
    remove("bench_test.db-wal");
    remove("bench_test.db-shm");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("CSQLiteDB Test Suite\n");
    printf("====================\n\n");

    // Tests run via static initialization above.
    // Now run benchmarks.
    //
    bench_write_through();
    bench_disk_realistic();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
