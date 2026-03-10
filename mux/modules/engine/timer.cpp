/*! \file timer.cpp
 * \brief Mini-task scheduler for timed events.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

CScheduler scheduler;

// Free List Reconstruction Task routine.
//
void dispatch_FreeListReconstruction(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    if (mudconf.control_flags & CF_DBCHECK)
    {
        const UTF8 *cmdsave = g_debug_cmd;
        g_debug_cmd = T("< dbck >");
        do_dbck(NOTHING, NOTHING, NOTHING, 0, 0);
        Guest.CleanUp();
        pcache_trim();
        pool_reset();
        g_debug_cmd = cmdsave;
    }

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    CLinearTimeDelta ltd;
    ltd.SetSeconds(mudconf.check_interval);
    mudstate.check_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.check_counter, PRIORITY_SYSTEM,
        dispatch_FreeListReconstruction, 0, 0);
}

// Database Dump Task routine.
//
void dispatch_DatabaseDump(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    int nNextTimeInSeconds = mudconf.dump_interval;

    if (mudconf.control_flags & CF_CHECKPOINT)
    {
        const UTF8 *cmdsave = g_debug_cmd;
        g_debug_cmd = T("< dump >");
#if defined(HAVE_WORKING_FORK)
        if (mudstate.dumping)
        {
            // There is a dump in progress. These usually happen very quickly.
            // We will reschedule ourselves to try again in 20 seconds.
            // Ordinarily, you would think "...a dump is a dump...", but some
            // dumps might not be the type of dump we're going to do.
            //
            nNextTimeInSeconds = 20;
        }
        else
#endif // HAVE_WORKING_FORK
        {
            fork_and_dump(0);
        }
        g_debug_cmd = cmdsave;
    }

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    CLinearTimeDelta ltd;
    ltd.SetSeconds(nNextTimeInSeconds);
    mudstate.dump_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.dump_counter, PRIORITY_SYSTEM, dispatch_DatabaseDump, 0, 0);
}

// Idle Check Task routine.
//
void dispatch_IdleCheck(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    if (mudconf.control_flags & CF_IDLECHECK)
    {
        const UTF8 *cmdsave = g_debug_cmd;
        g_debug_cmd = T("< idlecheck >");
        check_idle();
        g_debug_cmd = cmdsave;
    }

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    CLinearTimeDelta ltd;
    ltd.SetSeconds(mudconf.idle_interval);
    mudstate.idle_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.idle_counter, PRIORITY_SYSTEM, dispatch_IdleCheck, 0, 0);
}

void dispatch_KeepAlive(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    send_keepalive_nops();

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    CLinearTimeDelta ltd;
    ltd.SetSeconds(mudconf.keepalive_interval);
    mudstate.keepalive_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.keepalive_counter, PRIORITY_SYSTEM, dispatch_KeepAlive, 0, 0);
}

// Check Events Task routine.
//
void dispatch_CheckEvents(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    if (mudconf.control_flags & CF_EVENTCHECK)
    {
        const UTF8 *cmdsave = g_debug_cmd;
        g_debug_cmd = T("< eventcheck >");
        check_events();
        g_debug_cmd = cmdsave;
    }

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    CLinearTimeDelta ltd = time_15m;
    mudstate.events_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.events_counter, PRIORITY_SYSTEM, dispatch_CheckEvents, 0, 0);
}

void dispatch_CacheTick(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    const UTF8 *cmdsave = g_debug_cmd;
    g_debug_cmd = T("< cachetick >");

    CLinearTimeDelta ltd = 0;
    if (mudconf.cache_tick_period <= ltd)
    {
        mudconf.cache_tick_period.SetSeconds(1);
    }

    cache_tick();

    // Schedule ourselves again.
    //
    CLinearTimeAbsolute ltaNextTime;
    ltaNextTime.GetUTC();
    ltaNextTime += mudconf.cache_tick_period;
    scheduler.DeferTask(ltaNextTime, PRIORITY_SYSTEM, dispatch_CacheTick, 0, 0);
    g_debug_cmd = cmdsave;
}

static void dispatch_CanRestart(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);
    mudstate.bCanRestart = true;
}

void init_timer(void)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    // Setup re-occuring Free List Reconstruction task.
    //
    CLinearTimeDelta ltd;
    ltd.SetSeconds((mudconf.check_offset == 0) ? mudconf.check_interval : mudconf.check_offset);
    mudstate.check_counter  = ltaNow + ltd;
    scheduler.DeferTask(mudstate.check_counter, PRIORITY_SYSTEM,
        dispatch_FreeListReconstruction, 0, 0);

    // Setup re-occuring Database Dump task.
    //
    ltd.SetSeconds((mudconf.dump_offset == 0) ? mudconf.dump_interval : mudconf.dump_offset);
    mudstate.dump_counter  = ltaNow + ltd;
    scheduler.DeferTask(mudstate.dump_counter, PRIORITY_SYSTEM,
        dispatch_DatabaseDump, 0, 0);

    // Setup re-occuring Idle Check task.
    //
    ltd.SetSeconds(mudconf.idle_interval);
    mudstate.idle_counter   = ltaNow + ltd;
    scheduler.DeferTask(mudstate.idle_counter, PRIORITY_SYSTEM,
        dispatch_IdleCheck, 0, 0);

    // Setup re-occuring Check Events task.
    //
    mudstate.events_counter = ltaNow + time_15s;
    scheduler.DeferTask(mudstate.events_counter, PRIORITY_SYSTEM,
        dispatch_CheckEvents, 0, 0);

    // Setup re-occuring KeepAlive task.
    //
    ltd.SetSeconds(mudconf.keepalive_interval);
    mudstate.keepalive_counter = ltaNow + ltd;
    scheduler.DeferTask(mudstate.keepalive_counter, PRIORITY_SYSTEM, dispatch_KeepAlive, 0, 0);

    // Setup re-occuring cache_tick task.
    //
    ltd.SetSeconds(0);
    if (mudconf.cache_tick_period <= ltd)
    {
        mudconf.cache_tick_period.SetSeconds(1);
    }
    scheduler.DeferTask(ltaNow+mudconf.cache_tick_period, PRIORITY_SYSTEM,
        dispatch_CacheTick, 0, 0);

    // Setup one-shot task to enable restarting 10 seconds after startmux.
    //
    scheduler.DeferTask(ltaNow+time_15s, PRIORITY_OBJECT, dispatch_CanRestart, 0, 0);
}

/*
 * ---------------------------------------------------------------------------
 * * do_timewarp: Adjust various internal timers.
 */

void do_timewarp(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int secs;

    secs = mux_atol(arg);

    // Sem/Wait queues
    //
    if ((key == 0) || (key & TWARP_QUEUE))
    {
        do_queue(executor, caller, enactor, 0, QUEUE_WARP, arg, nullptr, 0);
    }

    // Once these are adjusted, we need to Cancel and reschedule the task.
    //
    CLinearTimeDelta ltd;
    ltd.SetSeconds(secs);
    if (key & TWARP_DUMP)
    {
        mudstate.dump_counter -= ltd;
        scheduler.CancelTask(dispatch_DatabaseDump, 0, 0);
        scheduler.DeferTask(mudstate.dump_counter, PRIORITY_SYSTEM, dispatch_DatabaseDump, 0, 0);
    }
    if (key & TWARP_CLEAN)
    {
        mudstate.check_counter -= ltd;
        scheduler.CancelTask(dispatch_FreeListReconstruction, 0, 0);
        scheduler.DeferTask(mudstate.check_counter, PRIORITY_SYSTEM, dispatch_FreeListReconstruction, 0, 0);
    }
    if (key & TWARP_IDLE)
    {
        mudstate.idle_counter -= ltd;
        scheduler.CancelTask(dispatch_IdleCheck, 0, 0);
        scheduler.DeferTask(mudstate.idle_counter, PRIORITY_SYSTEM, dispatch_IdleCheck, 0, 0);
    }
    if (key & TWARP_EVENTS)
    {
        mudstate.events_counter -= ltd;
        scheduler.CancelTask(dispatch_CheckEvents, 0, 0);
        scheduler.DeferTask(mudstate.events_counter, PRIORITY_SYSTEM, dispatch_CheckEvents, 0, 0);
    }
}


void CScheduler::DeferTask(const CLinearTimeAbsolute& ltaWhen, int iPriority,
                           FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    PTASK_RECORD pTask = new TASK_RECORD;
    if (!pTask) return;

    pTask->ltaWhen = ltaWhen;
    pTask->iPriority = iPriority;
    pTask->fpTask = fpTask;
    pTask->arg_voidptr = arg_voidptr;
    pTask->arg_Integer = arg_Integer;
    pTask->m_Ticket = m_Ticket++;

    // Must add to the WhenHeap so that network is still serviced.
    //
    if (!m_WhenHeap.Insert(pTask))
    {
        delete pTask;
    }
}

void CScheduler::DeferImmediateTask(int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    PTASK_RECORD pTask = new TASK_RECORD;
    if (!pTask) return;

    //pTask->ltaWhen = ltaWhen;
    pTask->iPriority = iPriority;
    pTask->fpTask = fpTask;
    pTask->arg_voidptr = arg_voidptr;
    pTask->arg_Integer = arg_Integer;
    pTask->m_Ticket = m_Ticket++;

    // Must add to the WhenHeap so that network is still serviced.
    //
    if (!m_WhenHeap.Insert(pTask))
    {
        delete pTask;
    }
}

void CScheduler::CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    m_WhenHeap.CancelTask(fpTask, arg_voidptr, arg_Integer);
    m_PriorityHeap.CancelTask(fpTask, arg_voidptr, arg_Integer);
}

void CScheduler::ReadyTasks(const CLinearTimeAbsolute& ltaNow)
{
    // Move ready-to-run tasks off the WhenHeap and onto the PriorityHeap.
    //
    PTASK_RECORD pTask = m_WhenHeap.PeekAtTopmost();
    while (  pTask
          && pTask->ltaWhen < ltaNow)
    {
        pTask = m_WhenHeap.RemoveTopmost();
        if (pTask)
        {
            if (  nullptr == pTask->fpTask
               || !m_PriorityHeap.Insert(pTask))
            {
                delete pTask;
            }
        }
        pTask = m_WhenHeap.PeekAtTopmost();
    }
}

int CScheduler::RunTasks(const CLinearTimeAbsolute& ltaNow)
{
    ReadyTasks(ltaNow);
    if (mudconf.active_q_chunk)
    {
        return RunTasks(mudconf.active_q_chunk);
    }
    else
    {
        return RunAllTasks();
    }
}

int CScheduler::RunTasks(int iCount)
{
    int nTasks = 0;
    while (iCount--)
    {
        PTASK_RECORD pTask = m_PriorityHeap.PeekAtTopmost();
        if (!pTask) break;

        if (pTask->iPriority > m_minPriority)
        {
            // This is related to CF_DEQUEUE and also to untimed (SUSPENDED)
            // semaphore entries that we would like to manage together with
            // the timed ones.
            //
            break;
        }
        pTask = m_PriorityHeap.RemoveTopmost();
        if (pTask)
        {
            if (pTask->fpTask)
            {
                pTask->fpTask(pTask->arg_voidptr, pTask->arg_Integer);
                nTasks++;
            }
            delete pTask;
        }
    }
    return nTasks;
}

int CScheduler::RunAllTasks(void)
{
    int nTotalTasks = 0;

    int nTasks;
    do
    {
        nTasks = RunTasks(100);
        nTotalTasks += nTasks;
    } while (nTasks);

    return nTotalTasks;
}

bool CScheduler::WhenNext(CLinearTimeAbsolute  *ltaWhen)
{
    // Check the Priority Queue first.
    //
    PTASK_RECORD pTask = m_PriorityHeap.PeekAtTopmost();
    if (pTask)
    {
        if (pTask->iPriority <= m_minPriority)
        {
            ltaWhen->SetSeconds(0);
            return true;
        }
    }

    // Check the When Queue next.
    //
    pTask = m_WhenHeap.PeekAtTopmost();
    if (pTask)
    {
        *ltaWhen = pTask->ltaWhen;
        return true;
    }
    return false;
}

void CScheduler::TraverseUnordered(SCHLOOK *pfLook)
{
    if (m_WhenHeap.TraverseUnordered(pfLook))
    {
        m_PriorityHeap.TraverseUnordered(pfLook);
    }
}

void CScheduler::TraverseOrdered(SCHLOOK *pfLook)
{
    m_PriorityHeap.TraverseOrdered(pfLook);
    m_WhenHeap.TraverseOrdered(pfLook);
}

void CScheduler::SetMinPriority(int arg_minPriority)
{
    m_minPriority = arg_minPriority;
}

void CScheduler::Shrink(void)
{
    m_WhenHeap.Shrink();
    m_PriorityHeap.Shrink();
}
