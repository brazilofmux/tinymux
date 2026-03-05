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

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
