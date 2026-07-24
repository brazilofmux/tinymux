/*! \file alarm.cpp
 * \brief mux_alarm module.
 *
 * This module implements an Alarm Clock mechanism used to help abbreviate
 * work as part of limiting CPU usage.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"

mux_alarm alarm_clock;

/*! \brief Alarm Clock Thread Procedure.
 *
 * This thread waits on a condition variable. When set() is called, it waits
 * with a timeout; if the timeout expires, alarmed is set. When clear() is
 * called, the thread returns to waiting indefinitely.
 */
void mux_alarm::alarm_proc()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        // Wait until signaled or timeout expires.
        //
        wake_ = false;
        if (alarm_period_.count() == 0)
        {
            // No alarm set -- wait indefinitely for a signal.
            //
            cv_.wait(lock, [this]{ return wake_; });
        }
        else
        {
            // Alarm is set -- wait with timeout.
            //
            if (!cv_.wait_for(lock, alarm_period_, [this]{ return wake_; }))
            {
                // Timed out -- fire the alarm.
                //
                alarmed.store(true);
                alarm_period_ = std::chrono::milliseconds(0);
                continue;
            }
        }

        if (shutdown_)
        {
            break;
        }
    }
}

/*! \brief Start the worker thread.
 *
 * Caller must hold mutex_.  The new thread blocks on mutex_ until the caller
 * releases it, then reads alarm_period_ directly -- so a wake_ raised before
 * the thread existed is not "lost": a fresh worker does not need waking, it
 * needs a period, and it reads the one already stored.
 */
void mux_alarm::start_thread_locked()
{
    if (!thread_started_)
    {
        thread_started_ = true;
        alarm_thread_ = std::thread(&mux_alarm::alarm_proc, this);
    }
}

/*! \brief Alarm Clock Constructor.
 *
 * Deliberately does NOT launch the alarm thread.
 *
 * alarm_clock is a namespace-scope global in libmux.so, so its constructor
 * runs during static initialization -- while the dynamic loader is still
 * working.  Calling pthread_create from an ELF constructor is a known glibc
 * hazard (thread startup needs TLS allocation, which contends with the
 * loader), and it deadlocked this process before main in roughly 14% of runs
 * (measured 7/50): both threads parked in futex_wait, zero output.  That made
 * `make test` hang nondeterministically on tests/netaddr/test_netaddr and hit
 * any binary linking -lmux.  LD_BIND_NOW=1 did not help, ruling out lazy
 * binding.
 *
 * The thread is created on first set() instead -- by which time static
 * initialization is long finished.  A clock nobody has armed needs no worker:
 * alarmed stays false, which is exactly right.
 */
mux_alarm::mux_alarm()
{
}

/*! \brief Alarm Clock Destructor.
 *
 * This function ensures the thread is completely shutdown and all resources
 * are released.
 */
mux_alarm::~mux_alarm()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        wake_ = true;
        cv_.notify_one();
    }
    if (alarm_thread_.joinable())
    {
        alarm_thread_.join();
    }
}

/*! \brief Sleep Routine.
 *
 * A sleep request does not prevent the Alarm Clock from firing, so typically,
 * the server sleeps while the Alarm Clock is not set.
 */
void mux_alarm::sleep(CLinearTimeDelta sleep_period)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(sleep_period.ReturnMilliseconds()));
}

/*! \brief Surrenders a little time.
 *
 * On most operating systems, a request to yield is a polite way of giving
 * other threads the remainder of your time slice.
 */
void mux_alarm::surrender_slice()
{
    std::this_thread::yield();
}

/*! \brief Set the Alarm Clock.
 *
 * This sets the Alarm Clock to fire after a certain time has passed.
 */
void mux_alarm::set(CLinearTimeDelta alarm_period)
{
    std::lock_guard<std::mutex> lock(mutex_);
    alarm_period_ = std::chrono::milliseconds(
        alarm_period.ReturnMilliseconds());
    alarmed.store(false);
    alarm_set_ = true;
    wake_ = true;

    // First arming creates the worker.  Arming is the only operation that
    // needs one, and by now we are well past static initialization.
    //
    start_thread_locked();

    cv_.notify_one();
}

/*! \brief Clear the Alarm Clock.
 *
 * This turns the Alarm Clock off.
 */
void mux_alarm::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    alarm_period_ = std::chrono::milliseconds(0);
    alarmed.store(false);
    alarm_set_ = false;
    wake_ = true;
    cv_.notify_one();
}
