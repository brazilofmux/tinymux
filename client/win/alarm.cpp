/*! \file alarm.cpp
 * \brief CMuxAlarm module.
 *
 * This module implements an Alarm Clock mechanism used to help abbreviate
 * work as part of limiting CPU usage.
 */

#include "stdafx.h"

CMuxAlarm MuxAlarm;

// OS Dependent Routines:
//
#if defined(WINDOWS_TIME)

/*! \brief Alarm Clock Thread Procedure.
 *
 * This thread takes requests to wait from dwWait.  If the allowed time runs
 * out, bAlarm is set.
 *
 * \param lParameter  Void pointer to CMuxAlarm instance.
 * \return            Always 1.
 */

DWORD WINAPI CMuxAlarm::AlarmProc(LPVOID lpParameter)
{
    CMuxAlarm *pthis = (CMuxAlarm *)lpParameter;
    DWORD dwWait = pthis->dwWait;
    for (;;)
    {
        HANDLE hSemAlarm = pthis->hSemAlarm;
        if (hSemAlarm == INVALID_HANDLE_VALUE)
        {
            break;
        }
        DWORD dwReason = WaitForSingleObject(hSemAlarm, dwWait);
        if (dwReason == WAIT_TIMEOUT)
        {
            pthis->bAlarmed = true;
            dwWait = INFINITE;
        }
        else
        {
            dwWait = pthis->dwWait;
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

CMuxAlarm::CMuxAlarm(void)
{
    hSemAlarm = CreateSemaphore(NULL, 0, 1, NULL);
    Clear();
    hThread = CreateThread(NULL, 0, AlarmProc, (LPVOID)this, 0, NULL);
}

/*! \brief Alarm Clock Destructor.
 *
 * This function ensures the thread is completely shutdown and all resources
 * are released.
 *
 * \return  none.
 */

CMuxAlarm::~CMuxAlarm()
{
    HANDLE hSave = hSemAlarm;
    hSemAlarm = INVALID_HANDLE_VALUE;
    ReleaseSemaphore(hSave, 1, NULL);
    WaitForSingleObject(hThread, static_cast<DWORD>(15*FACTOR_MS_PER_SECOND));
    CloseHandle(hSave);
}

/*! \brief Sleep Routine.
 *
 * A sleep request does not prevent the Alarm Clock from firing, so typically,
 * the server sleeps while the Alarm Clock is not set.
 *
 * \return  none.
 */

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
    ::Sleep(ltd.ReturnMilliseconds());
}

/*! \brief Surrenders a little time.
 *
 * One most operating system, a request to sleep for 0 is a polite way of
 * giving other threads the remainder of your time slice.
 *
 * \return  none.
 */

void CMuxAlarm::SurrenderSlice(void)
{
    ::Sleep(0);
}

/*! \brief Set the Alarm Clock.
 *
 * This sets the Alarm Clock to fire after a certain time has passed.
 *
 * \return  none.
 */

void CMuxAlarm::Set(CLinearTimeDelta ltd)
{
    dwWait = ltd.ReturnMilliseconds();
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = true;
}

/*! \brief Clear the Alarm Clock.
 *
 * This turns the Alarm Clock off.
 *
 * \return  none.
 */

void CMuxAlarm::Clear(void)
{
    dwWait = INFINITE;
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = false;
}

#elif defined(UNIX_TIME)

/*! \brief Alarm Clock Constructor.
 *
 * The UNIX version of the Alarm Clock is built on signals, so there isn't
 * much to initialize.
 *
 * \return  none.
 */

CMuxAlarm::CMuxAlarm(void)
{
    bAlarmed = false;
    bAlarmSet = false;
}

/*! \brief Sleep Routine.
 *
 * A sleep request does not prevent signals from firing, so typically,
 * the server sleeps while the Alarm Clock is not set.
 *
 * \return  none.
 */

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
#if defined(HAVE_NANOSLEEP)
    struct timespec req;
    ltd.ReturnTimeSpecStruct(&req);
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
    bool   bSaved = false;
    if (bAlarmSet)
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
        bAlarmSet = false;
    }
#endif
#if defined(HAVE_USLEEP)
#define TIME_1S 1000000
    unsigned long usec;
    INT64 usecTotal = ltd.ReturnMicroseconds();

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
    ::sleep(ltd.ReturnSeconds());
#endif
#ifdef HAVE_SETITIMER
    if (bSaved)
    {
        // Restore and re-enabled timer.
        //
        setitimer(ITIMER_PROF, &oit, NULL);
        bAlarmSet = true;
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

void CMuxAlarm::SurrenderSlice(void)
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

void CMuxAlarm::Set(CLinearTimeDelta ltd)
{
#ifdef HAVE_SETITIMER
    struct itimerval it;
    ltd.ReturnTimeValueStruct(&it.it_value);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, NULL);
    bAlarmSet = true;
    bAlarmed  = false;
#endif
}

/*! \brief Clear the Alarm Clock.
 *
 * This turns the Alarm Clock off.
 *
 * \return  none.
 */

void CMuxAlarm::Clear(void)
{
#ifdef HAVE_SETITIMER
    // Turn off the timer.
    //
    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, NULL);
    bAlarmSet = false;
    bAlarmed  = false;
#endif
}

/*! \brief Clear the Alarm Clock.
 *
 * Like the AlarmProc above, this routine is called when a SIGPROF signal
 * occurs indicating that the allowed time has passed.
 *
 * \return  none.
 */

void CMuxAlarm::Signal(void)
{
    bAlarmSet = false;
    bAlarmed  = true;
}

#endif // UNIX_TIME
