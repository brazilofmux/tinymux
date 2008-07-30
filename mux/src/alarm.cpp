/*! \file alarm.cpp
 * \brief CMuxAlarm module.
 *
 * $Id$
 *
 * This module implements an alarm clock mechanism used to help abbreviate
 * work as part of limiting CPU usage.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

CMuxAlarm MuxAlarm;

// OS Dependent Routines:
//
#if defined(WINDOWS_TIME)

static DWORD WINAPI AlarmProc(LPVOID lpParameter)
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

CMuxAlarm::CMuxAlarm(void)
{
    hSemAlarm = CreateSemaphore(NULL, 0, 1, NULL);
    Clear();
    hThread = CreateThread(NULL, 0, AlarmProc, (LPVOID)this, 0, NULL);
}

CMuxAlarm::~CMuxAlarm()
{
    HANDLE hSave = hSemAlarm;
    hSemAlarm = INVALID_HANDLE_VALUE;
    ReleaseSemaphore(hSave, 1, NULL);
    WaitForSingleObject(hThread, 15*FACTOR_100NS_PER_SECOND);
    CloseHandle(hSave);
}

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
    ::Sleep(ltd.ReturnMilliseconds());
}

void CMuxAlarm::SurrenderSlice(void)
{
    ::Sleep(0);
}

void CMuxAlarm::Set(CLinearTimeDelta ltd)
{
    dwWait = ltd.ReturnMilliseconds();
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = true;
}

void CMuxAlarm::Clear(void)
{
    dwWait = INFINITE;
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = false;
}

#elif defined(UNIX_TIME)

CMuxAlarm::CMuxAlarm(void)
{
    bAlarmed = false;
    bAlarmSet = false;
}

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
#if   defined(HAVE_NANOSLEEP)
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
#ifdef HAVE_SETITIMER
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
#if   defined(HAVE_USLEEP)
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

void CMuxAlarm::SurrenderSlice(void)
{
    ::sleep(0);
}

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

void CMuxAlarm::Signal(void)
{
    bAlarmSet = false;
    bAlarmed  = true;
}

#endif // UNIX_TIME
