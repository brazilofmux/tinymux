// Unit tests for mux_alarm — the per-command wall-clock abort used to cut off
// runaway commands (alarm_clock.set/clear, alarm_clock.alarmed).
//
// Why this exists: alarm_clock is a namespace-scope global in libmux.so, and
// its constructor used to spawn the worker thread.  That runs during static
// initialization, and pthread_create from an ELF constructor is a known glibc
// hazard -- it deadlocked before main in ~14% of runs (7/50 measured), which
// made `make test` hang nondeterministically on tests/netaddr/test_netaddr and
// hit any binary linking -lmux.
//
// The worker is now created lazily on first set().  That moves a startup
// hazard into the arming path, so the arming path needs a mechanical guard:
// if lazy start ever regresses, the alarm silently never fires and CPU
// limiting quietly stops working -- a failure the smoke suite cannot see.
//
// Merely running this binary also samples the deadlock: it links libmux and
// therefore exercises static init.
//
// Build/run: make test

#include <cstdio>
#include <chrono>
#include <thread>

#include "autoconf.h"
#include "config.h"
#include "timeutil.h"

// --- tiny test framework ---------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char *what)
{
    if (cond)
    {
        g_pass++;
        printf("ok   - %s\n", what);
    }
    else
    {
        g_fail++;
        printf("FAIL - %s\n", what);
    }
}

static CLinearTimeDelta ms(long n)
{
    CLinearTimeDelta d;
    d.SetMilliseconds(n);
    return d;
}

// Poll until `alarmed` turns true or the budget runs out.  Polling rather
// than sleeping a fixed span keeps the test fast when it passes and still
// tolerant of a loaded machine when it does not.
static bool wait_alarmed(int budget_ms)
{
    const int step_ms = 10;
    for (int waited = 0; waited < budget_ms; waited += step_ms)
    {
        if (alarm_clock.alarmed.load())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
    return alarm_clock.alarmed.load();
}

int main()
{
    printf("== mux_alarm ==\n");

    // 1. A clock nobody has armed must not be alarmed -- and with lazy start
    //    there is no worker thread yet at all.
    check(!alarm_clock.alarmed.load(), "fresh clock is not alarmed");

    // 2. First arming must create the worker AND fire.  This is the lazy-start
    //    regression guard: pre-fix the thread already existed, so a broken
    //    lazy start would show up only here.
    alarm_clock.set(ms(150));
    check(wait_alarmed(3000), "first set() fires (worker started lazily)");

    // 3. clear() disarms.
    alarm_clock.clear();
    check(!alarm_clock.alarmed.load(), "clear() disarms");

    // 4. Re-arming with the worker already running must fire again.
    alarm_clock.set(ms(150));
    check(wait_alarmed(3000), "second set() fires (worker reused)");
    alarm_clock.clear();

    // 5. Clearing before the period elapses must cancel, not fire late.
    alarm_clock.set(ms(2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    alarm_clock.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check(!alarm_clock.alarmed.load(), "clear() cancels a pending alarm");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (0 == g_fail) ? 0 : 1;
}
