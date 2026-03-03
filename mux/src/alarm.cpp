/*! \file alarm.cpp
 * \brief mux_alarm module.
 *
 * This module implements an Alarm Clock mechanism used to help abbreviate
 * work as part of limiting CPU usage.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

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

/*! \brief Alarm Clock Constructor.
 *
 * Launches the alarm thread which waits on the condition variable.
 */
mux_alarm::mux_alarm()
{
    alarm_thread_ = std::thread(&mux_alarm::alarm_proc, this);
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
