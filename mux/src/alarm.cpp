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

// OS Dependent Routines:
//
#if defined(WINDOWS_TIME)

/*! \brief Alarm Clock Thread Procedure.
 *
 * This thread takes requests to wait. If the allowed time runs out, alarmed is set.
 *
 * \param parameter  Void pointer to mux_alarm instance.
 * \return Always 1.
 */

DWORD WINAPI mux_alarm::alarm_proc(LPVOID parameter)
{
    const auto pthis = static_cast<mux_alarm *>(parameter);
    auto milliseconds = pthis->alarm_period_in_milliseconds_;
    for (;;)
    {
        const auto sem_alarm = pthis->semaphore_handle_;
        if (sem_alarm == INVALID_HANDLE_VALUE)
        {
            break;
        }
        const auto reason = WaitForSingleObject(sem_alarm, milliseconds);
        if (reason == WAIT_TIMEOUT)
        {
            pthis->alarmed = true;
            milliseconds = INFINITE;
        }
        else
        {
            milliseconds = pthis->alarm_period_in_milliseconds_;
        }
    }
    return 1;
}

/*! \brief Alarm Clock Constructor.
 *
 * The order of execution in this function is important as the semaphore must
 * be in place and in the correct state before the thread begins to use it.
 *
 * \return  none.
 */

mux_alarm::mux_alarm()
{
    semaphore_handle_ = CreateSemaphore(nullptr, 0, 1, nullptr);
    clear();
    thread_handle_ = CreateThread(nullptr, 0, alarm_proc, static_cast<LPVOID>(this), 0, nullptr);
}

/*! \brief Alarm Clock Destructor.
 *
 * This function ensures the thread is completely shutdown and all resources
 * are released.
 *
 * \return  none.
 */

mux_alarm::~mux_alarm()
{
    const auto save_handle = semaphore_handle_;
    semaphore_handle_ = INVALID_HANDLE_VALUE;
    ReleaseSemaphore(save_handle, 1, nullptr);
    WaitForSingleObject(thread_handle_, static_cast<DWORD>(15 * FACTOR_MS_PER_SECOND));
    CloseHandle(save_handle);
}

/*! \brief Sleep Routine.
 *
 * A sleep request does not prevent the Alarm Clock from firing, so typically,
 * the server sleeps while the Alarm Clock is not set.
 *
 * \return  none.
 */

void mux_alarm::sleep(CLinearTimeDelta sleep_period)
{
    ::Sleep(sleep_period.ReturnMilliseconds());
}

/*! \brief Surrenders a little time.
 *
 * On most operating system, a request to sleep for 0 is a polite way of
 * giving other threads the remainder of your time slice.
 *
 * \return  none.
 */

void mux_alarm::surrender_slice()
{
    ::Sleep(0);
}

/*! \brief Set the Alarm Clock.
 *
 * This sets the Alarm Clock to fire after a certain time has passed.
 *
 * \return  none.
 */

void mux_alarm::set(CLinearTimeDelta alarm_period)
{
    alarm_period_in_milliseconds_ = alarm_period.ReturnMilliseconds();
    ReleaseSemaphore(semaphore_handle_, 1, nullptr);
    alarmed = false;
    alarm_set_ = true;
}

/*! \brief Clear the Alarm Clock.
 *
 * This turns the Alarm Clock off.
 *
 * \return  none.
 */

void mux_alarm::clear()
{
    alarm_period_in_milliseconds_ = INFINITE;
    ReleaseSemaphore(semaphore_handle_, 1, nullptr);
    alarmed = false;
    alarm_set_ = false;
}

#elif defined(UNIX_TIME)

/*! \brief Alarm Clock Constructor.
 *
 * The UNIX version of the Alarm Clock is built on signals, so there isn't
 * much to initialize.
 *
 * \return  none.
 */

mux_alarm::mux_alarm()
{
    alarmed = false;
    alarm_set_ = false;
}

/*! \brief Sleep Routine.
 *
 * A sleep request does not prevent signals from firing, so typically,
 * the server sleeps while the Alarm Clock is not set.
 *
 * \return  none.
 */

void mux_alarm::sleep(CLinearTimeDelta sleep_period)
{
#if defined(HAVE_NANOSLEEP)
    struct timespec req;
    sleep_period.ReturnTimeSpecStruct(&req);
    while (!mudstate.shutdown_flag)
    {
        struct timespec rem;
        if (  nanosleep(&req, &rem) == -1
           && errno == EINTR)
        {
            req = rem;
        }
        else
        {
            break;
        }
    }
#else
#if defined(HAVE_SETITIMER)
    struct itimerval oit;
    bool bSaved = false;
    if (alarm_set_)
    {
        // Save existing timer and disable.
        //
        struct itimerval it;
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = 0;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 0;
        setitimer(ITIMER_PROF, &it, &oit);
        bSaved = true;
        alarm_set_ = false;
    }
#endif
#if defined(HAVE_USLEEP)
#define TIME_1S 1000000
    unsigned long usec;
    INT64 usecTotal = sleep_period.ReturnMicroseconds();

    while (  usecTotal
          && mudstate.shutdown_flag)
    {
        usec = usecTotal;
        if (usecTotal < TIME_1S)
        {
            usec = usecTotal;
        }
        else
        {
            usec = TIME_1S-1;
        }
        usleep(usec);
        usecTotal -= usec;
    }
#else
    ::sleep(sleep_period.ReturnSeconds());
#endif
#ifdef HAVE_SETITIMER
    if (bSaved)
    {
        // Restore and re-enabled timer.
        //
        setitimer(ITIMER_PROF, &oit, nullptr);
        alarm_set_ = true;
    }
#endif
#endif
}

/*! \brief Surrenders a little time.
 *
 * One most operating system, a request to sleep for 0 is a polite way of
 * giving other threads the remainder of your time slice.
 *
 * \return  none.
 */

void mux_alarm::surrender_slice()
{
    ::sleep(0);
}

/*! \brief Set the Alarm Clock.
 *
 * This sets the Alarm Clock to fire after a certain time has passed by
 * requesting a SIG_PROF to fire at that time.  Note that SIG_PROF is used
 * by the profiler, so the Alarm Clock must be disable in autoconf.h before
 * the server can be profiled.
 *
 * \return  none.
 */

void mux_alarm::set(CLinearTimeDelta alarm_period)
{
#ifdef HAVE_SETITIMER
    struct itimerval it;
    alarm_period.ReturnTimeValueStruct(&it.it_value);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, nullptr);
    alarm_set_ = true;
    alarmed  = false;
#endif
}

/*! \brief Clear the Alarm Clock.
 *
 * This turns the Alarm Clock off.
 *
 * \return  none.
 */

void mux_alarm::clear()
{
#ifdef HAVE_SETITIMER
    // Turn off the timer.
    //
    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, nullptr);
    alarm_set_ = false;
    alarmed  = false;
#endif
}

/*! \brief Clear the Alarm Clock.
 *
 * Like the AlarmProc above, this routine is called when a SIGPROF signal
 * occurs indicating that the allowed time has passed.
 *
 * \return  none.
 */

void mux_alarm::signal()
{
    alarm_set_ = false;
    alarmed  = true;
}

#endif // UNIX_TIME
