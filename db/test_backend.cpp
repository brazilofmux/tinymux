/*! \file test_backend.cpp
 * \brief Tests for the IStorageBackend interface via CSQLiteBackend.
 *
 * These tests exercise the abstract interface only — no SQLite-specific
 * calls. When the CHashFile backend is implemented, these same tests
 * can run against it to verify behavioral equivalence.
 */

#include "storage_backend.h"
#include "sqlite_backend.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <chrono>
#include <memory>
#include <vector>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAILED: %s:%d: %s != %s (%d != %d)\n", \
            __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define PASS() do { g_tests_passed++; printf("  PASSED: %s\n", __func__); } while (0)

// Factory: creates the backend under test. When CHashFileBackend is ready,
// add a second factory and run the same suite against it.
//
static std::unique_ptr<IStorageBackend> CreateBackend()
{
    auto backend = std::make_unique<CSQLiteBackend>();
    backend->Open(":memory:");
    return backend;
}

// ---------------------------------------------------------------------------
// Interface tests — these use only IStorageBackend methods.
// ---------------------------------------------------------------------------

static void test_backend_open_close()
{
    auto be = CreateBackend();
    ASSERT_TRUE(be->IsOpen());
    be->Close();
    ASSERT_TRUE(!be->IsOpen());
    PASS();
}

static void test_backend_put_get()
{
    auto be = CreateBackend();

    const UTF8 *val = (const UTF8 *)"test value";
    size_t vlen = strlen((const char *)val) + 1;
    ASSERT_TRUE(be->Put(0, 1, val, vlen));

    UTF8 buf[256];
    size_t rlen = 0;
    ASSERT_TRUE(be->Get(0, 1, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, vlen);
    ASSERT_TRUE(0 == memcmp(buf, val, vlen));

    be->Close();
    PASS();
}

static void test_backend_put_replace()
{
    auto be = CreateBackend();

    ASSERT_TRUE(be->Put(5, 10, (const UTF8 *)"first", 6));
    ASSERT_TRUE(be->Put(5, 10, (const UTF8 *)"second", 7));

    UTF8 buf[256];
    size_t rlen;
    ASSERT_TRUE(be->Get(5, 10, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, (size_t)7);
    ASSERT_TRUE(0 == memcmp(buf, "second", 7));

    be->Close();
    PASS();
}

static void test_backend_get_missing()
{
    auto be = CreateBackend();

    UTF8 buf[256];
    size_t rlen = 99;
    ASSERT_TRUE(!be->Get(999, 999, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, (size_t)0);

    be->Close();
    PASS();
}

static void test_backend_del()
{
    auto be = CreateBackend();

    ASSERT_TRUE(be->Put(1, 1, (const UTF8 *)"x", 2));
    ASSERT_TRUE(be->Del(1, 1));

    UTF8 buf[64];
    size_t rlen;
    ASSERT_TRUE(!be->Get(1, 1, buf, sizeof(buf), &rlen));

    // Deleting non-existent should succeed.
    //
    ASSERT_TRUE(be->Del(1, 1));

    be->Close();
    PASS();
}

static void test_backend_del_all()
{
    auto be = CreateBackend();

    for (unsigned int a = 1; a <= 20; a++)
    {
        char val[32];
        int len = snprintf(val, sizeof(val), "val_%u", a);
        ASSERT_TRUE(be->Put(7, a, (const UTF8 *)val, len + 1));
    }
    // Different object — should not be affected.
    //
    ASSERT_TRUE(be->Put(8, 1, (const UTF8 *)"keep", 5));

    ASSERT_TRUE(be->DelAll(7));

    // All attrs on object 7 should be gone.
    //
    int count = 0;
    be->GetAll(7, [&](unsigned int, const UTF8 *, size_t) { count++; });
    ASSERT_EQ(count, 0);

    // Object 8 should be untouched.
    //
    UTF8 buf[64];
    size_t rlen;
    ASSERT_TRUE(be->Get(8, 1, buf, sizeof(buf), &rlen));
    ASSERT_EQ(rlen, (size_t)5);

    be->Close();
    PASS();
}

static void test_backend_get_all()
{
    auto be = CreateBackend();

    for (unsigned int a = 1; a <= 30; a++)
    {
        char val[64];
        int len = snprintf(val, sizeof(val), "attr_%u", a);
        ASSERT_TRUE(be->Put(12, a, (const UTF8 *)val, len + 1));
    }

    // Also put attrs on another object.
    //
    ASSERT_TRUE(be->Put(13, 1, (const UTF8 *)"other", 6));

    int count = 0;
    unsigned int last_attrnum = 0;
    be->GetAll(12, [&](unsigned int attrnum, const UTF8 *value, size_t len)
    {
        count++;
        // WITHOUT ROWID with PRIMARY KEY (object, attrnum) returns
        // attrs in attrnum order.
        //
        ASSERT_TRUE(attrnum > last_attrnum);
        last_attrnum = attrnum;
        ASSERT_TRUE(len > 0);
        ASSERT_TRUE(value != nullptr);
    });
    ASSERT_EQ(count, 30);

    be->Close();
    PASS();
}

static void test_backend_sync_tick()
{
    auto be = CreateBackend();

    // These should not crash or fail, even with no data.
    //
    be->Sync();
    be->Tick();

    // After writing data, sync should persist it.
    //
    ASSERT_TRUE(be->Put(0, 1, (const UTF8 *)"data", 5));
    be->Sync();
    be->Tick();

    be->Close();
    PASS();
}

static void test_backend_many_objects()
{
    auto be = CreateBackend();

    // Simulate 1000 objects, 5 attrs each — the interface should handle
    // this without issue.
    //
    for (unsigned int obj = 0; obj < 1000; obj++)
    {
        for (unsigned int a = 1; a <= 5; a++)
        {
            char val[128];
            int len = snprintf(val, sizeof(val), "obj%u_attr%u", obj, a);
            ASSERT_TRUE(be->Put(obj, a, (const UTF8 *)val, len + 1));
        }
    }

    // Spot-check some values.
    //
    UTF8 buf[128];
    size_t rlen;
    ASSERT_TRUE(be->Get(500, 3, buf, sizeof(buf), &rlen));
    ASSERT_TRUE(0 == memcmp(buf, "obj500_attr3", 12));

    ASSERT_TRUE(be->Get(999, 5, buf, sizeof(buf), &rlen));
    ASSERT_TRUE(0 == memcmp(buf, "obj999_attr5", 12));

    // Bulk load a specific object.
    //
    int count = 0;
    be->GetAll(750, [&](unsigned int, const UTF8 *, size_t) { count++; });
    ASSERT_EQ(count, 5);

    be->Close();
    PASS();
}

static void test_backend_large_value()
{
    auto be = CreateBackend();

    // TinyMUX LBUF_SIZE is 8000. Test a large attribute value.
    //
    const size_t LARGE = 8000;
    std::vector<UTF8> big(LARGE, 'X');
    big[LARGE - 1] = '\0';

    ASSERT_TRUE(be->Put(0, 1, big.data(), LARGE));

    std::vector<UTF8> buf(LARGE + 100);
    size_t rlen;
    ASSERT_TRUE(be->Get(0, 1, buf.data(), buf.size(), &rlen));
    ASSERT_EQ(rlen, LARGE);
    ASSERT_TRUE(0 == memcmp(buf.data(), big.data(), LARGE));

    be->Close();
    PASS();
}

// ---------------------------------------------------------------------------
// Performance test through the interface
// ---------------------------------------------------------------------------

static void bench_backend_interface()
{
    printf("\n--- Backend Interface Performance (SQLite, :memory:) ---\n");

    auto be = CreateBackend();

    const int N = 10000;
    UTF8 value[256];
    memset(value, 'B', sizeof(value));
    value[255] = '\0';

    // Writes through interface.
    //
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; i++)
    {
        be->Put(i % 100, (i / 100) + 1, value, 256);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d Put:    %.1f ms (%.1f us/op)\n", N, ms, (ms * 1000.0) / N);

    // Reads through interface.
    //
    UTF8 buf[256];
    size_t rlen;
    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; i++)
    {
        be->Get(i % 100, (i / 100) + 1, buf, sizeof(buf), &rlen);
    }
    t1 = std::chrono::high_resolution_clock::now();
    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d Get:    %.1f ms (%.1f us/op)\n", N, ms, (ms * 1000.0) / N);

    // GetAll through interface.
    //
    t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; i++)
    {
        be->GetAll(i % 100, [](unsigned int, const UTF8 *, size_t) {});
    }
    t1 = std::chrono::high_resolution_clock::now();
    ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("  %d GetAll: %.1f ms (%.1f us/op)\n", 1000, ms, (ms * 1000.0) / 1000);

    be->Close();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("IStorageBackend Test Suite (SQLite)\n");
    printf("===================================\n\n");

    test_backend_open_close();
    test_backend_put_get();
    test_backend_put_replace();
    test_backend_get_missing();
    test_backend_del();
    test_backend_del_all();
    test_backend_get_all();
    test_backend_sync_tick();
    test_backend_many_objects();
    test_backend_large_value();

    bench_backend_interface();

    printf("\n===================================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
