/*! \file test_alarm.cpp
 * \brief A guest loop must not be able to outlive the wall-clock alarm (#1571).
 *
 * dbt_run polls max_dispatch and alarm_flag only at the top of its dispatch
 * loop.  Both guards therefore sit *above* the point where control enters
 * native code for a block, and nothing re-checks them until that block exits
 * and dispatch comes round again.  A guest loop whose back-edge is chained
 * directly into native code never comes round again: the process pins at 100%
 * inside dbt_run with no dispatch limit, no alarm, and no counter moving.
 *
 * The failure is invisible from outside, which is why it went unnoticed --
 * every instrument the JIT has reports "nothing is happening".  That is also
 * why it needs a test that fails by *not finishing*: a wrong answer announces
 * itself, a hang does not.
 *
 * Each case runs dbt_run on a worker thread, raises the alarm from the main
 * thread, and gives the worker a bounded grace period.  A run that has not
 * returned by then is the bug, and the test says so and exits non-zero rather
 * than hanging the suite.  The worker is deliberately leaked in that case:
 * it is spinning in native code that will never return, so joining it would
 * hang exactly the way we are trying to report.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

#include "dbt.h"

static constexpr size_t MEM_SIZE = 1u << 20;

// The guest never ECALLs in these cases; if it does, stop the run.
static int alarm_test_ecall(rv64_ctx_t *ctx, void *)
{
    (void)ctx;
    return 0;
}

static void emit(std::vector<uint8_t> &mem, uint64_t at, uint32_t insn)
{
    memcpy(mem.data() + at, &insn, 4);
}

// ---- RV64 encodings, only the few shapes these cases need. --------------

// ADDI rd, rs1, imm
static uint32_t rv_addi(int rd, int rs1, int imm)
{
    return (static_cast<uint32_t>(imm & 0xFFF) << 20)
         | (static_cast<uint32_t>(rs1) << 15) | (0u << 12)
         | (static_cast<uint32_t>(rd) << 7) | 0x13u;
}

// JAL rd, offset  (offset is a signed multiple of 2)
static uint32_t rv_jal(int rd, int32_t off)
{
    uint32_t u = static_cast<uint32_t>(off);
    uint32_t imm = ((u >> 20) & 0x1) << 31
                 | ((u >> 1)  & 0x3FF) << 21
                 | ((u >> 11) & 0x1) << 20
                 | ((u >> 12) & 0xFF) << 12;
    return imm | (static_cast<uint32_t>(rd) << 7) | 0x6Fu;
}

// BNE rs1, rs2, offset
static uint32_t rv_bne(int rs1, int rs2, int32_t off)
{
    uint32_t u = static_cast<uint32_t>(off);
    return (((u >> 12) & 0x1) << 31) | (((u >> 5) & 0x3F) << 25)
         | (static_cast<uint32_t>(rs2) << 20)
         | (static_cast<uint32_t>(rs1) << 15) | (1u << 12)
         | (((u >> 1) & 0xF) << 8) | (((u >> 11) & 0x1) << 7) | 0x63u;
}

// JALR rd, rs1, imm
static uint32_t rv_jalr(int rd, int rs1, int imm)
{
    return (static_cast<uint32_t>(imm & 0xFFF) << 20)
         | (static_cast<uint32_t>(rs1) << 15) | (0u << 12)
         | (static_cast<uint32_t>(rd) << 7) | 0x67u;
}

struct alarm_case {
    const char *name;
    // Fills guest memory and returns the entry pc.
    uint64_t (*build)(std::vector<uint8_t> &mem);
};

// j .    -- the tightest possible loop: one block whose only exit is itself.
static uint64_t build_self_jump(std::vector<uint8_t> &mem)
{
    emit(mem, 0, rv_jal(0, 0));
    return 0;
}

// A two-instruction loop with a live counter, so the block is not degenerate
// and the back-edge is a conditional branch rather than an unconditional jump.
//   loop: addi x1, x1, 1
//         bne  x1, x0, loop
static uint64_t build_counted_loop(std::vector<uint8_t> &mem)
{
    emit(mem, 0, rv_addi(1, 1, 1));
    emit(mem, 4, rv_bne(1, 0, -4));
    return 0;
}

// Two blocks that chain into each other, so the cycle spans a chain edge
// rather than living inside a single block.
//   a: addi x1,x1,1 ; jal x0, b
//   b: addi x2,x2,1 ; jal x0, a
static uint64_t build_two_block_cycle(std::vector<uint8_t> &mem)
{
    emit(mem, 0,  rv_addi(1, 1, 1));
    emit(mem, 4,  rv_jal(0, 4));      // -> 8
    emit(mem, 8,  rv_addi(2, 2, 1));
    emit(mem, 12, rv_jal(0, -12));    // -> 0
    return 0;
}

// A loop the self_loop optimisation declines to take.  rc_loop_overcommits()
// turns it off when the body references more non-pinned guest registers than
// the register cache has free slots, and then the back-edge is emitted as an
// ordinary chained exit instead of a native jump to warm_entry.  That is a
// second, independent route to the same hang, and the budget check at
// warm_entry cannot cover it because there is no warm_entry.
//
//   loop: addi x5..x28, x_, 1   (24 distinct non-pinned registers)
//         addi x1, x1, 1
//         bne  x1, x0, loop
static uint64_t build_reg_heavy_loop(std::vector<uint8_t> &mem)
{
    uint64_t a = 0;
    for (int r = 5; r <= 28; r++) { emit(mem, a, rv_addi(r, r, 1)); a += 4; }
    emit(mem, a, rv_addi(1, 1, 1)); a += 4;
    emit(mem, a, rv_bne(1, 0, -static_cast<int32_t>(a)));
    return 0;
}

// A loop whose body calls a function, so the call is a candidate for
// try_emit_inline_call() -- a native CALL into the callee's translated block.
//
// NEGATIVE CONTROL, and labelled as one because it passes with the fix
// disabled.  The inline call plants a side exit, which costs the block its
// self_loop status, so this shape returns to the dispatcher once per
// iteration and is bounded whether or not the budget check exists.  It
// therefore demonstrates nothing about the fix -- keeping it silent about
// that would make it read as coverage it does not provide.
//
// It earns its place as a regression guard instead: it pins the claim that
// the inline-call path does not create an unbounded loop today.  If block
// formation later learns to keep an inlined call inside a self-loop, this
// case starts failing, which is exactly when someone needs to know.
//
//    0: addi x5, x5, 1
//    4: jal  ra, +16        -> 20
//    8: addi x1, x1, 1
//   12: bne  x1, x0, -12    -> 0
//   16: ecall               (unreached)
//   20: addi x6, x6, 1      (callee)
//   24: jalr x0, ra, 0      (return)
static uint64_t build_inlined_call_loop(std::vector<uint8_t> &mem)
{
    emit(mem,  0, rv_addi(5, 5, 1));
    emit(mem,  4, rv_jal(1, 16));
    emit(mem,  8, rv_addi(1, 1, 1));
    emit(mem, 12, rv_bne(1, 0, -12));
    emit(mem, 16, 0x00000073u);        // ECALL
    emit(mem, 20, rv_addi(6, 6, 1));
    emit(mem, 24, rv_jalr(0, 1, 0));
    return 0;
}

static const alarm_case CASES[] = {
    { "self-jump (j .)",        build_self_jump },
    { "counted loop (bne)",     build_counted_loop },
    { "two-block chain cycle",  build_two_block_cycle },
    { "reg-heavy loop (no self_loop)", build_reg_heavy_loop },
    { "loop with an inlined call",     build_inlined_call_loop },
};

// How long to let the run continue after the alarm is raised.  Generous: the
// point is to distinguish "returned" from "never returns", not to measure
// latency.
static constexpr int GRACE_MS = 5000;

static bool run_case(const alarm_case &c)
{
    // Heap-allocated and deliberately never freed on the failure path: the
    // worker thread may still be running inside dbt_run, and tearing its state
    // out from under it would turn a clean report into a crash.
    auto *mem   = new std::vector<uint8_t>(MEM_SIZE, 0);
    auto *dbt   = new dbt_state_t();
    auto *alarm = new std::atomic<bool>(false);
    auto *done  = new std::atomic<bool>(false);
    auto *rc    = new std::atomic<int>(0);

    uint64_t entry = c.build(*mem);

    if (dbt_init(dbt, mem->data(), MEM_SIZE, alarm_test_ecall, nullptr) != 0) {
        fprintf(stderr, "  FAIL: %s: dbt_init\n", c.name);
        return false;
    }
    dbt->alarm_flag = alarm;

    std::thread worker([dbt, entry, rc, done] {
        rc->store(dbt_run(dbt, entry, MEM_SIZE - 16));
        done->store(true);
    });

    // Let the loop get well established -- translated, chained, and spinning
    // in native code -- before the alarm goes up.  Raising it earlier could
    // be caught by the dispatch-loop poll on the way in, which would test
    // nothing.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    alarm->store(true, std::memory_order_relaxed);

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(GRACE_MS);
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!done->load()) {
        fprintf(stderr,
                "  FAIL: %s: dbt_run did not return %d ms after the alarm\n",
                c.name, GRACE_MS);
        fprintf(stderr,
                "        The guest loop is executing entirely inside chained\n"
                "        native code, so neither max_dispatch nor alarm_flag\n"
                "        is ever polled again (#1571).\n");
        worker.detach();   // still spinning; joining would hang the suite
        return false;
    }

    worker.join();
    // -3 is the alarm return.  Any clean return proves the loop was bounded;
    // -3 proves it was bounded by the alarm specifically.
    if (rc->load() != -3) {
        fprintf(stderr, "  FAIL: %s: dbt_run returned %d, expected -3 (alarm)\n",
                c.name, rc->load());
        dbt_cleanup(dbt);
        return false;
    }

    printf("  ok: %s (dbt_run returned -3)\n", c.name);
    dbt_cleanup(dbt);
    delete alarm; delete done; delete rc; delete dbt; delete mem;
    return true;
}

// max_dispatch is the other guard the issue names.  It is a count, not a
// deadline, so it needs its own case: a loop that never reaches the dispatcher
// cannot exceed a dispatch limit either.
//
// The property under test does not depend on the value of max_dispatch: only
// that dbt_run returns -2 once dispatch_count exceeds it.  10000 cost ~1.2s
// on a fast host and left only a 4x margin against GRACE_MS, so a throttled
// laptop or busy CI runner timed out and the message blamed #1571 (#2101).
// 1000 proves the same bound with ~40x margin and ~1s less wall clock.
//
static constexpr uint64_t MAX_DISPATCH_BOUND = 1000;
static constexpr uint64_t MAX_DISPATCH_PROBE = 100;

// Run the counted loop under max_dispatch and wait up to grace_ms for -2.
// On timeout the worker is detached (still spinning) and true is returned
// in *timed_out.  Caller owns the heap state either way.
static int run_max_dispatch_once(
    uint64_t max_dispatch, int grace_ms, bool *timed_out)
{
    auto *mem  = new std::vector<uint8_t>(MEM_SIZE, 0);
    auto *dbt  = new dbt_state_t();
    auto *done = new std::atomic<bool>(false);
    auto *rc   = new std::atomic<int>(0);

    uint64_t entry = build_counted_loop(*mem);

    if (dbt_init(dbt, mem->data(), MEM_SIZE, alarm_test_ecall, nullptr) != 0) {
        fprintf(stderr, "  FAIL: max_dispatch: dbt_init\n");
        delete done; delete rc; delete dbt; delete mem;
        *timed_out = false;
        return 1;  // non-zero, not -2
    }
    dbt->max_dispatch = max_dispatch;

    std::thread worker([dbt, entry, rc, done] {
        rc->store(dbt_run(dbt, entry, MEM_SIZE - 16));
        done->store(true);
    });

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(grace_ms);
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!done->load()) {
        worker.detach();
        // State deliberately leaked: the worker still owns it.
        *timed_out = true;
        return 0;
    }

    worker.join();
    int result = rc->load();
    dbt_cleanup(dbt);
    delete done; delete rc; delete dbt; delete mem;
    *timed_out = false;
    return result;
}

static bool run_max_dispatch_case()
{
    bool timed_out = false;
    int rc = run_max_dispatch_once(MAX_DISPATCH_BOUND, GRACE_MS, &timed_out);

    if (timed_out) {
        // Discriminate "dispatcher never reached" (#1571) from "host is
        // slower than the budget assumes" (#2101).  A tiny bound on a
        // fresh state: if that returns -2, the dispatcher is fine and
        // only the wall clock was wrong.
        bool probe_timed_out = false;
        int probe_rc = run_max_dispatch_once(
            MAX_DISPATCH_PROBE, GRACE_MS, &probe_timed_out);

        fprintf(stderr,
                "  FAIL: max_dispatch: dbt_run did not return within %d ms\n"
                "        (max_dispatch=%llu).\n",
                GRACE_MS,
                static_cast<unsigned long long>(MAX_DISPATCH_BOUND));
        if (!probe_timed_out && probe_rc == -2) {
            fprintf(stderr,
                    "        Probe with max_dispatch=%llu returned -2, so the\n"
                    "        dispatcher IS being reached — this host is more\n"
                    "        than ~4x slower than the budget assumes, not a\n"
                    "        #1571 regression.  Re-run on an idle box, or lower\n"
                    "        MAX_DISPATCH_BOUND further (#2101).\n",
                    static_cast<unsigned long long>(MAX_DISPATCH_PROBE));
        } else {
            fprintf(stderr,
                    "        Probe with max_dispatch=%llu also failed to return\n"
                    "        -2 in time (timed_out=%d rc=%d).  The loop may never\n"
                    "        return to the dispatcher to be counted (#1571).\n",
                    static_cast<unsigned long long>(MAX_DISPATCH_PROBE),
                    probe_timed_out ? 1 : 0, probe_rc);
        }
        return false;
    }

    if (rc != -2) {
        fprintf(stderr,
                "  FAIL: max_dispatch: dbt_run returned %d, expected -2\n",
                rc);
        return false;
    }

    printf("  ok: max_dispatch bound (dbt_run returned -2)\n");
    return true;
}

int main()
{
    printf("dbt alarm/dispatch bound tests (#1571)\n");

    int failures = 0;
    for (const alarm_case &c : CASES) {
        if (!run_case(c)) failures++;
    }
    if (!run_max_dispatch_case()) failures++;

    if (failures) {
        fprintf(stderr, "\n=== tests/dbt alarm: FAILED (%d case(s)) ===\n",
                failures);
        fprintf(stderr,
                "A guest loop ran past both of dbt_run's safety nets.  On a live\n"
                "server this is a hang that serves no player and logs nothing.\n");
        // Cases that failed left a thread spinning in native code on purpose;
        // _exit avoids running static destructors underneath it.
        fflush(nullptr);
        _exit(1);
    }
    printf("=== tests/dbt alarm: PASSED ===\n");
    return 0;
}
