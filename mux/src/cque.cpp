/*! \file cque.cpp
 * \brief Commands and functions for manipulating the command queue.
 *
 * This forms the upper-level command list queue, and includes timed commands
 * and semaphores.  The lower-level task implementation is found in timer.cpp.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <csignal>

#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "mathutil.h"
#include "powers.h"

bool break_called = false;

static CLinearTimeDelta GetProcessorUsage(void)
{
    CLinearTimeDelta ltd;
#if defined(WINDOWS_PROCESSES)

    FILETIME ftCreate;
    FILETIME ftExit;
    FILETIME ftKernel;
    FILETIME ftUser;
    GetProcessTimes(game_process_handle, &ftCreate, &ftExit, &ftKernel, &ftUser);
    ltd.Set100ns(*(INT64 *)(&ftUser));

#endif // WINDOWS_PROCESSES

#if defined(UNIX_PROCESSES)
#if defined(HAVE_GETRUSAGE)

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    ltd.SetTimeValueStruct(&usage.ru_utime);

#else

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    ltd = ltaNow - mudstate.start_time;
#endif
#endif
    return ltd;
}

// ---------------------------------------------------------------------------
// add_to: Adjust an object's queue or semaphore count.
//
static int add_to(dbref executor, int am, int attrnum)
{
    int aflags;
    dbref aowner;

    UTF8 *atr_gotten = atr_get("add_to.68", executor, attrnum, &aowner, &aflags);
    int num = mux_atol(atr_gotten);
    free_lbuf(atr_gotten);
    num += am;

    UTF8 buff[I32BUF_SIZE];
    size_t nlen = 0;
    *buff = '\0';
    if (num)
    {
        nlen = mux_ltoa(num, buff);
    }
    atr_add_raw_LEN(executor, attrnum, buff, nlen);
    return num;
}

// This Task assumes that pEntry is already unlinked from any lists it may
// have been related to.
//
static void Task_RunQueueEntry(void *pEntry, int iUnused)
{
    UNUSED_PARAMETER(iUnused);

    BQUE *point = (BQUE *)pEntry;
    dbref executor = point->executor;

    if (  Good_obj(executor)
       && !Going(executor))
    {
        giveto(executor, mudconf.waitcost);
        mudstate.curr_enactor = point->enactor;
        mudstate.curr_executor = executor;
        a_Queue(Owner(executor), -1);
        point->executor = NOTHING;
        if (!Halted(executor))
        {
            // Load scratch args.
            //
            for (int i = 0; i < MAX_GLOBAL_REGS; i++)
            {
                if (mudstate.global_regs[i])
                {
                    RegRelease(mudstate.global_regs[i]);
                    mudstate.global_regs[i] = nullptr;
                }
                mudstate.global_regs[i] = point->scr[i];
                point->scr[i] = nullptr;
            }

#if defined(STUB_SLAVE)
            if (nullptr != mudstate.pResultsSet)
            {
                mudstate.pResultsSet->Release();
                mudstate.pResultsSet = nullptr;
            }
            mudstate.pResultsSet = point->pResultsSet;
            point->pResultsSet = nullptr;
            mudstate.iRow = point->iRow;
#endif // STUB_SLAVE

            UTF8 *command = point->comm;

            mux_assert(!mudstate.inpipe);
            mux_assert(mudstate.pipe_nest_lev == 0);
            mux_assert(mudstate.poutobj == NOTHING);
            mux_assert(!mudstate.pout);

            break_called = false;
            while (  command
                  && !break_called)
            {
                mux_assert(!mudstate.poutnew);
                mux_assert(!mudstate.poutbufc);

                UTF8 *cp = parse_to(&command, ';', 0);

                if (  cp
                   && *cp)
                {
                    // Will command be piped?
                    //
                    if (  command
                       && *command == '|'
                       && mudstate.pipe_nest_lev < mudconf.ntfy_nest_lim)
                    {
                        command++;
                        mudstate.pipe_nest_lev++;
                        mudstate.inpipe = true;

                        mudstate.poutnew  = alloc_lbuf("process_command.pipe");
                        mudstate.poutbufc = mudstate.poutnew;
                        mudstate.poutobj  = executor;
                    }
                    else
                    {
                        mudstate.inpipe = false;
                        mudstate.poutobj = NOTHING;
                    }

                    CLinearTimeAbsolute ltaBegin;
                    ltaBegin.GetUTC();
                    alarm_clock.set(mudconf.max_cmdsecs);
                    CLinearTimeDelta ltdUsageBegin = GetProcessorUsage();

                    UTF8 *log_cmdbuf = process_command(executor, point->caller,
                        point->enactor, point->eval, false, cp, (const UTF8 **)point->env,
                        point->nargs);

                    CLinearTimeAbsolute ltaEnd;
                    ltaEnd.GetUTC();
                    if (alarm_clock.alarmed)
                    {
                        notify(executor, T("GAME: Expensive activity abbreviated."));
                        s_Flags(point->enactor, FLAG_WORD1, Flags(point->enactor) | HALT);
                        s_Flags(point->executor, FLAG_WORD1, Flags(point->executor) | HALT);
                        halt_que(point->enactor, NOTHING);
                        halt_que(executor, NOTHING);
                    }
                    alarm_clock.clear();

                    CLinearTimeDelta ltdUsageEnd = GetProcessorUsage();
                    CLinearTimeDelta ltd = ltdUsageEnd - ltdUsageBegin;
                    db[executor].cpu_time_used += ltd;

                    ltd = ltaEnd - ltaBegin;
                    if (mudconf.rpt_cmdsecs < ltd)
                    {
                        STARTLOG(LOG_PROBLEMS, "CMD", "CPU");
                        log_name_and_loc(executor);
                        UTF8 *logbuf = alloc_lbuf("do_top.LOG.cpu");
                        mux_sprintf(logbuf, LBUF_SIZE, T(" queued command taking %s secs (enactor #%d): "),
                            ltd.ReturnSecondsString(4), point->enactor);
                        log_text(logbuf);
                        free_lbuf(logbuf);
                        log_text(log_cmdbuf);
                        ENDLOG;
                    }
                }

                // Transition %| value.
                //
                if (mudstate.pout)
                {
                    free_lbuf(mudstate.pout);
                    mudstate.pout = nullptr;
                }
                if (mudstate.poutnew)
                {
                    *mudstate.poutbufc = '\0';
                    mudstate.pout = mudstate.poutnew;
                    mudstate.poutnew  = nullptr;
                    mudstate.poutbufc = nullptr;
                }
            }

            // Clean up %| value.
            //
            if (mudstate.pout)
            {
                free_lbuf(mudstate.pout);
                mudstate.pout = nullptr;
            }
            mudstate.pipe_nest_lev = 0;
            mudstate.inpipe = false;
            mudstate.poutobj = NOTHING;
        }
    }

    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (point->scr[i])
        {
            RegRelease(point->scr[i]);
            point->scr[i] = nullptr;
        }

        if (mudstate.global_regs[i])
        {
            RegRelease(mudstate.global_regs[i]);
            mudstate.global_regs[i] = nullptr;
        }
    }

#if defined(STUB_SLAVE)
    mudstate.iRow = RS_TOP;
    if (nullptr != mudstate.pResultsSet)
    {
        mudstate.pResultsSet->Release();
        mudstate.pResultsSet = nullptr;
    }
#endif // STUB_SLAVE

    MEMFREE(point->text);
    point->text = nullptr;
    free_qentry(point);
}

// ---------------------------------------------------------------------------
// que_want: Do we want this queue entry?
//
static bool que_want(BQUE *entry, dbref ptarg, dbref otarg)
{
    if (  ptarg != NOTHING
       && ptarg != Owner(entry->executor))
    {
        return false;
    }
    return (  otarg == NOTHING
           || otarg == entry->executor);
}

static void Task_SemaphoreTimeout(void *pExpired, int iUnused)
{
    UNUSED_PARAMETER(iUnused);

    // A semaphore has timed out.
    //
    BQUE *point = (BQUE *)pExpired;
    add_to(point->u.s.sem, -1, point->u.s.attr);
    point->u.s.sem = NOTHING;
    Task_RunQueueEntry(point, 0);
}

void Task_SQLTimeout(void *pExpired, int iUnused)
{
    UNUSED_PARAMETER(iUnused);

    // A SQL Query has timed out.  Actually, this isn't supported.
    //
    BQUE *point = (BQUE *)pExpired;
    Task_RunQueueEntry(point, 0);
}

static dbref Halt_Player_Target;
static dbref Halt_Object_Target;
static int   Halt_Entries;
static dbref Halt_Player_Run;
static dbref Halt_Entries_Run;

static int CallBack_HaltQueue(PTASK_RECORD p)
{
    if (  p->fpTask == Task_RunQueueEntry
       || p->fpTask == Task_SQLTimeout
       || p->fpTask == Task_SemaphoreTimeout)
    {
        // This is a @wait, timed Semaphore Task, or timed SQL Query.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (que_want(point, Halt_Player_Target, Halt_Object_Target))
        {
            // Accounting for pennies and queue quota.
            //
            dbref dbOwner = point->executor;
            if (!isPlayer(dbOwner))
            {
                dbOwner = Owner(dbOwner);
            }
            if (dbOwner != Halt_Player_Run)
            {
                if (Halt_Player_Run != NOTHING)
                {
                    giveto(Halt_Player_Run, mudconf.waitcost * Halt_Entries_Run);
                    a_Queue(Halt_Player_Run, -Halt_Entries_Run);
                }
                Halt_Player_Run = dbOwner;
                Halt_Entries_Run = 0;
            }
            Halt_Entries++;
            Halt_Entries_Run++;
            if (p->fpTask == Task_SemaphoreTimeout)
            {
                add_to(point->u.s.sem, -1, point->u.s.attr);
            }

            for (int i = 0; i < MAX_GLOBAL_REGS; i++)
            {
                if (point->scr[i])
                {
                    RegRelease(point->scr[i]);
                    point->scr[i] = nullptr;
                }
            }

            MEMFREE(point->text);
            point->text = nullptr;
            free_qentry(point);
            return IU_REMOVE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

// ------------------------------------------------------------------
//
// halt_que: Remove all queued commands that match (executor, object).
//
// (NOTHING,  NOTHING)    matches all queue entries.
// (NOTHING,  <object>)   matches only queue entries run from <object>.
// (<executor>, NOTHING)  matches only queue entries owned by <executor>.
// (<executor>, <object>) matches only queue entries run from <objects>
//                        and owned by <executor>.
//
int halt_que(dbref executor, dbref object)
{
    Halt_Player_Target = executor;
    Halt_Object_Target = object;
    Halt_Entries       = 0;
    Halt_Player_Run    = NOTHING;
    Halt_Entries_Run   = 0;

    // Process @wait, timed semaphores, and untimed semaphores.
    //
    scheduler.TraverseUnordered(CallBack_HaltQueue);

    if (Halt_Player_Run != NOTHING)
    {
        giveto(Halt_Player_Run, mudconf.waitcost * Halt_Entries_Run);
        a_Queue(Halt_Player_Run, -Halt_Entries_Run);
        Halt_Player_Run = NOTHING;
    }
    return Halt_Entries;
}

// ---------------------------------------------------------------------------
// do_halt: Command interface to halt_que.
//
void do_halt(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *target, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref executor_targ, obj_targ;

    if ((key & HALT_ALL) && !Can_Halt(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    // Figure out what to halt.
    //
    if (!target || !*target)
    {
        obj_targ = NOTHING;
        if (key & HALT_ALL)
        {
            executor_targ = NOTHING;
        }
        else
        {
            executor_targ = Owner(executor);
            if (!isPlayer(executor))
            {
                obj_targ = executor;
            }
        }
    }
    else
    {
        if (Can_Halt(executor))
        {
            obj_targ = match_thing(executor, target);
        }
        else
        {
            obj_targ = match_controlled(executor, target);
        }
        if (!Good_obj(obj_targ))
        {
            return;
        }
        if (key & HALT_ALL)
        {
            notify(executor, T("Can\xE2\x80\x99t specify a target and /all"));
            return;
        }
        if (isPlayer(obj_targ))
        {
            executor_targ = obj_targ;
            obj_targ = NOTHING;
        }
        else
        {
            executor_targ = NOTHING;
        }
    }

    int numhalted = halt_que(executor_targ, obj_targ);
    if (Quiet(executor))
    {
        return;
    }
    notify(Owner(executor), tprintf(T("%d queue entr%s removed."), numhalted, numhalted == 1 ? "y" : "ies"));
}

static int Notify_Key;
static int Notify_Num_Done;
static int Notify_Num_Max;
static int Notify_Sem;
static int Notify_Attr;

// NFY_DRAIN or NFY_NFYALL
//
static int CallBack_NotifySemaphoreDrainOrAll(PTASK_RECORD p)
{
    if (p->fpTask == Task_SemaphoreTimeout)
    {
        // This represents a semaphore.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (  point->u.s.sem == Notify_Sem
           && (  point->u.s.attr == Notify_Attr
              || !Notify_Attr))
        {
            Notify_Num_Done++;
            if (NFY_DRAIN == (Notify_Key & NFY_MASK))
            {
                // Discard the command
                //
                giveto(point->executor, mudconf.waitcost);
                a_Queue(Owner(point->executor), -1);

                for (int i = 0; i < MAX_GLOBAL_REGS; i++)
                {
                    if (point->scr[i])
                    {
                        RegRelease(point->scr[i]);
                        point->scr[i] = nullptr;
                    }
                }

                MEMFREE(point->text);
                point->text = nullptr;
                free_qentry(point);

                return IU_REMOVE_TASK;
            }
            else
            {
                // Allow the command to run. The priority may have been
                // PRIORITY_SUSPEND, so we need to change it.
                //
                if (isPlayer(point->enactor))
                {
                    p->iPriority = PRIORITY_PLAYER;
                }
                else
                {
                    p->iPriority = PRIORITY_OBJECT;
                }
                p->ltaWhen.GetUTC();
                p->fpTask = Task_RunQueueEntry;
                return IU_UPDATE_TASK;
            }
        }
    }
    return IU_NEXT_TASK;
}

// NFY_NFY
//
static int CallBack_NotifySemaphoreFirst(PTASK_RECORD p)
{
    // If we've notified enough, exit.
    //
    if (  NFY_NFY == (Notify_Key & NFY_MASK)
       && Notify_Num_Done >= Notify_Num_Max)
    {
        return IU_DONE;
    }

    if (p->fpTask == Task_SemaphoreTimeout)
    {
        // This represents a semaphore.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (  point->u.s.sem == Notify_Sem
           && (  point->u.s.attr == Notify_Attr
              || !Notify_Attr))
        {
            Notify_Num_Done++;

            // Allow the command to run. The priority may have been
            // PRIORITY_SUSPEND, so we need to change it.
            //
            if (isPlayer(point->enactor))
            {
                p->iPriority = PRIORITY_PLAYER;
            }
            else
            {
                p->iPriority = PRIORITY_OBJECT;
            }
            p->ltaWhen.GetUTC();
            p->fpTask = Task_RunQueueEntry;
            return IU_UPDATE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

// ---------------------------------------------------------------------------
// nfy_que: Notify commands from the queue and perform or discard them.

int nfy_que(dbref sem, int attr, int key, int count)
{
    int cSemaphore = 1;
    if (attr)
    {
        int   aflags;
        dbref aowner;
        UTF8 *str = atr_get("nfy_que.562", sem, attr, &aowner, &aflags);
        cSemaphore = mux_atol(str);
        free_lbuf(str);
    }

    Notify_Num_Done = 0;
    if (0 < cSemaphore)
    {
        Notify_Key     = key;
        Notify_Sem     = sem;
        Notify_Attr    = attr;
        Notify_Num_Max = count;
        if (NFY_NFY == (key & NFY_MASK))
        {
            scheduler.TraverseOrdered(CallBack_NotifySemaphoreFirst);
        }
        else
        {
            scheduler.TraverseUnordered(CallBack_NotifySemaphoreDrainOrAll);
        }
    }

    // Update the sem waiters count.
    //
    if (NFY_NFY == (key & NFY_MASK))
    {
        add_to(sem, -count, attr);
    }
    else
    {
        atr_clr(sem, attr);
    }

    return Notify_Num_Done;
}

// ---------------------------------------------------------------------------
// do_notify: Command interface to nfy_que

void do_notify
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *what,
    UTF8 *count,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *obj = parse_to(&what, '/', 0);
    init_match(executor, obj, NOTYPE);
    match_everything(0);

    dbref thing = noisy_match_result();
    if (!Good_obj(thing))
    {
        return;
    }
    if (!Controls(executor, thing) && !Link_ok(thing))
    {
        notify(executor, NOPERM_MESSAGE);
    }
    else
    {
        int atr = A_SEMAPHORE;
        if (  what
           && what[0] != '\0')
        {
            UTF8 *AttributeName = what;
            int i = mkattr(executor, AttributeName);
            if (0 < i)
            {
                atr = i;
                if (atr != A_SEMAPHORE)
                {
                    // Do they have permission to set this attribute?
                    //
                    ATTR *ap = (ATTR *)anum_get(atr);
                    if (!bCanSetAttr(executor, thing, ap))
                    {
                        notify_quiet(executor, NOPERM_MESSAGE);
                        return;
                    }
                }
            }
        }

        int loccount;
        if (  count
           && count[0] != '\0')
        {
            loccount = mux_atol(count);
        }
        else
        {
            loccount = 1;
        }

        if (0 < loccount)
        {
            nfy_que(thing, atr, key, loccount);
            if (  !Quiet(executor)
               && !Quiet(thing)
               && !(key & NFY_QUIET))
            {
                if (NFY_DRAIN == (key & NFY_MASK))
                {
                    notify_quiet(executor, T("Drained."));
                }
                else
                {
                    notify_quiet(executor, T("Notified."));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// setup_que: Set up a queue entry.
//
static BQUE *setup_que
(
    dbref    executor,
    dbref    caller,
    dbref    enactor,
    int      eval,
    UTF8    *command,
    int      nargs,
    const UTF8 *args[],
    reg_ref *sargs[]
)
{
    // Can we run commands at all?
    //
    if (Halted(executor))
    {
        return nullptr;
    }

    // Make sure executor can afford to do it.
    //
    int a = mudconf.waitcost;
    if (mudconf.machinecost && RandomINT32(0, mudconf.machinecost-1) == 0)
    {
        a++;
    }
    if (!payfor(executor, a))
    {
        notify(Owner(executor), T("Not enough money to queue command."));
        return nullptr;
    }

    // Wizards and their objs may queue up to db_top+1 cmds. Players are
    // limited to QUEUE_QUOTA. -mnp
    //
    a = QueueMax(Owner(executor));
    if (a < a_Queue(Owner(executor), 1))
    {
        a_Queue(Owner(executor), -1);

        notify(Owner(executor),
            T("Run away objects: too many commands queued.  Halted."));
        halt_que(Owner(executor), NOTHING);

        // Halt also means no command execution allowed.
        //
        s_Halted(executor);
        return nullptr;
    }

    // We passed all the tests.
    //

    // Calculate the length of the save string.
    //
    size_t tlen = 0;
    static size_t nCommand;
    static size_t nLenEnv[NUM_ENV_VARS];

    if (command)
    {
        nCommand = strlen((char *)command) + 1;
        tlen = nCommand;
    }

    if (NUM_ENV_VARS < nargs)
    {
        nargs = NUM_ENV_VARS;
    }

    for (a = 0; a < nargs; a++)
    {
        if (args[a])
        {
            nLenEnv[a] = strlen((char *)args[a]) + 1;
            tlen += nLenEnv[a];
        }
    }

    // Create the qeue entry and load the save string.
    //
    BQUE *tmp = alloc_qentry("setup_que.qblock");
    tmp->comm = nullptr;

    UTF8 *tptr = tmp->text = (UTF8 *)MEMALLOC(tlen);
    ISOUTOFMEMORY(tptr);

    if (command)
    {
        memcpy(tptr, command, nCommand);
        tmp->comm = tptr;
        tptr += nCommand;
    }

    for (a = 0; a < nargs; a++)
    {
        if (args[a])
        {
            memcpy(tptr, args[a], nLenEnv[a]);
            tmp->env[a] = tptr;
            tptr += nLenEnv[a];
        }
        else
        {
            tmp->env[a] = nullptr;
        }
    }

    for ( ; a < NUM_ENV_VARS; a++)
    {
        tmp->env[a] = nullptr;
    }

    if (sargs)
    {
        for (a = 0; a < MAX_GLOBAL_REGS; a++)
        {
            tmp->scr[a] = sargs[a];
            if (sargs[a])
            {
                RegAddRef(sargs[a]);
            }
        }
    }
    else
    {
        for (a = 0; a < MAX_GLOBAL_REGS; a++)
        {
            tmp->scr[a] = nullptr;
        }
    }

#if defined(STUB_SLAVE)
    tmp->iRow = mudstate.iRow;
    tmp->pResultsSet = mudstate.pResultsSet;
    if (nullptr != mudstate.pResultsSet)
    {
        mudstate.pResultsSet->AddRef();
    }
#endif // STUB_SLAVE

    // Load the rest of the queue block.
    //
    tmp->executor = executor;
    tmp->IsTimed = false;
    tmp->u.s.sem = NOTHING;
    tmp->u.s.attr = 0;
    tmp->enactor = enactor;
    tmp->caller = caller;
    tmp->eval = eval;
    tmp->nargs = nargs;
    return tmp;
}

// ---------------------------------------------------------------------------
// wait_que: Add commands to the wait or semaphore queues.
//
void wait_que
(
    dbref    executor,
    dbref    caller,
    dbref    enactor,
    int      eval,
    bool     bTimed,
    CLinearTimeAbsolute &ltaWhen,
    dbref    sem,
    int      attr,
    UTF8    *command,
    int      nargs,
    const UTF8 *args[],
    reg_ref *sargs[]
)
{
    if (!(mudconf.control_flags & CF_INTERP))
    {
        return;
    }

    BQUE *tmp = setup_que(executor, caller, enactor, eval,
        command,
        nargs, args,
        sargs);

    if (!tmp)
    {
        return;
    }

    int iPriority;
    if (isPlayer(tmp->enactor))
    {
        iPriority = PRIORITY_PLAYER;
    }
    else
    {
        iPriority = PRIORITY_OBJECT;
    }

    tmp->IsTimed = bTimed;
    tmp->waittime = ltaWhen;
    tmp->u.s.sem = sem;
    tmp->u.s.attr = attr;

    if (sem == NOTHING)
    {
        // Not a semaphore, so let it run it immediately or put it on
        // the wait queue.
        //
        if (tmp->IsTimed)
        {
            scheduler.DeferTask(tmp->waittime, iPriority, Task_RunQueueEntry, tmp, 0);
        }
        else
        {
            scheduler.DeferImmediateTask(iPriority, Task_RunQueueEntry, tmp, 0);
        }
    }
    else
    {
        if (!tmp->IsTimed)
        {
            // In this case, the timeout task below will never run,
            // but it allows us to manage all semaphores together in
            // the same data structure.
            //
            iPriority = PRIORITY_SUSPEND;
        }
        scheduler.DeferTask(tmp->waittime, iPriority, Task_SemaphoreTimeout, tmp, 0);
    }
}

#if defined(STUB_SLAVE)
bool   QueryComplete_bDone   = false;
UINT32 QueryComplete_hQuery  = 0;
CResultsSet *QueryComplete_prsResultsSet = nullptr;

static int CallBack_QueryComplete(PTASK_RECORD p)
{
    if (QueryComplete_bDone)
    {
        return IU_DONE;
    }

    if (Task_SQLTimeout == p->fpTask)
    {
        // This represents a query.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (point->u.hQuery == QueryComplete_hQuery)
        {
            p->iPriority = PRIORITY_OBJECT;
            p->ltaWhen.GetUTC();
            p->fpTask    = Task_RunQueueEntry;

            point->u.s.sem    = NOTHING;
            point->u.s.attr   = 0;
            QueryComplete_prsResultsSet->AddRef();
            point->pResultsSet = QueryComplete_prsResultsSet;
            point->iRow = RS_TOP;

            QueryComplete_bDone = true;
            return IU_UPDATE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

// This can be called as a side-effect of talking with the stubslave.
// Therefore, we only want to raise the priority of the corresponding take
// from SUSPENDED to OBJECT.
//
void query_complete(UINT32 hQuery, UINT32 iError, CResultsSet *prsResultsSet)
{
    if (nullptr != prsResultsSet)
    {
        prsResultsSet->SetError(iError);
    }

    QueryComplete_bDone   = false;
    QueryComplete_hQuery  = hQuery;
    QueryComplete_prsResultsSet = prsResultsSet;
    scheduler.TraverseUnordered(CallBack_QueryComplete);
    QueryComplete_prsResultsSet = nullptr;
}
#endif // STUB_SLAVE

// ---------------------------------------------------------------------------
// sql_que: Add commands to the sql queue.
//
void sql_que
(
    dbref    executor,
    dbref    caller,
    dbref    enactor,
    int      eval,
    dbref    thing,
    int      attr,
    UTF8    *dbname,
    UTF8    *query,
    int      nargs,
    const UTF8 *args[],
    reg_ref *sargs[]
)
{
    static UINT32 next_handle = 0;

    if (  !(mudconf.control_flags & CF_INTERP)
       || nullptr == mudstate.pIQueryControl)
    {
        return;
    }

    ATTR *pattr = atr_num(attr);
    if (nullptr == pattr)
    {
        return;
    }

    UTF8 mbuf[MBUF_SIZE];
    mux_sprintf(mbuf, MBUF_SIZE, T("@trigger #%d/%s"), thing, pattr->name);
    BQUE *tmp = setup_que(executor, caller, enactor, eval,
        mbuf,
        nargs, args,
        sargs);

    if (!tmp)
    {
        return;
    }

    UINT32 hQuery = next_handle++;

    tmp->u.hQuery = hQuery;

    scheduler.DeferTask(tmp->waittime, PRIORITY_SUSPEND, Task_SQLTimeout, tmp, 0);
    MUX_RESULT mr = mudstate.pIQueryControl->Query(hQuery, dbname, query);
    if (MUX_FAILED(mr))
    {
        scheduler.CancelTask(Task_SQLTimeout, tmp, next_handle);
    }
}

// ---------------------------------------------------------------------------
// do_wait: Command interface to wait_que
//
void do_wait
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *event,
    UTF8 *cmd,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(nargs);

    CLinearTimeAbsolute ltaWhen;
    CLinearTimeDelta    ltd;

    // If arg1 is all numeric, do simple (non-sem) timed wait.
    //
    if (is_rational(event))
    {
        if (key & WAIT_UNTIL)
        {
            ltaWhen.SetSecondsString(event);
        }
        else
        {
            ltaWhen.GetUTC();
            ltd.SetSecondsString(event);
            ltaWhen += ltd;
        }
        wait_que(executor, caller, enactor, eval, true, ltaWhen, NOTHING, 0,
            cmd,
            ncargs, cargs,
            mudstate.global_regs);
        return;
    }

    // Semaphore wait with optional timeout.
    //
    UTF8 *what = parse_to(&event, '/', 0);
    init_match(executor, what, NOTYPE);
    match_everything(0);

    dbref thing = noisy_match_result();
    if (!Good_obj(thing))
    {
        return;
    }
    else if (!Controls(executor, thing) && !Link_ok(thing))
    {
        notify(executor, NOPERM_MESSAGE);
    }
    else
    {
        // Get timeout, default 0.
        //
        int atr = A_SEMAPHORE;
        bool bTimed = false;
        if (event && *event)
        {
            if (is_rational(event))
            {
                if (key & WAIT_UNTIL)
                {
                    ltaWhen.SetSecondsString(event);
                }
                else
                {
                    ltaWhen.GetUTC();
                    ltd.SetSecondsString(event);
                    ltaWhen += ltd;
                }
                bTimed = true;
            }
            else
            {
                UTF8 *EventAttributeName = (UTF8 *)event;
                ATTR *ap = atr_str(EventAttributeName);
                if (!ap)
                {
                    atr = mkattr(executor, EventAttributeName);
                    if (atr <= 0)
                    {
                        notify_quiet(executor, T("Invalid attribute."));
                        return;
                    }
                    ap = atr_num(atr);
                }
                else
                {
                    atr = ap->number;
                }
                if (!bCanSetAttr(executor, thing, ap))
                {
                    notify_quiet(executor, NOPERM_MESSAGE);
                    return;
                }
            }
        }

        int num = add_to(thing, 1, atr);
        if (num <= 0)
        {
            // Thing over-notified, run the command immediately.
            //
            thing = NOTHING;
            bTimed = false;
        }
        wait_que(executor, caller, enactor, eval, bTimed, ltaWhen, thing, atr,
            cmd,
            ncargs, cargs,
            mudstate.global_regs);
    }
}

// ---------------------------------------------------------------------------
// do_query: Command interface to sql_que
//
void do_query
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *dbref_attr,
    UTF8 *dbname_query,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(nargs);

    if (nullptr == mudstate.pIQueryControl)
    {
        notify_quiet(executor, T("Query server is not available."));
        return;
    }

    if (key & QUERY_SQL)
    {
        // SQL Query.
        //
        dbref thing;
        ATTR *pattr;

        if (!(  parse_attrib(executor, dbref_attr, &thing, &pattr)
             && nullptr != pattr))
        {
            notify_quiet(executor, T("No match."));
            return;
        }

        if (!Controls(executor, thing))
        {
            notify_quiet(executor, T(NOPERM_MESSAGE));
            return;
        }

        UTF8 *pQuery = dbname_query;
        UTF8 *pDBName = parse_to(&pQuery, '/', 0);

        if (nullptr == pQuery)
        {
            notify(executor, T("QUERY: No Query."));
            return;
        }

        sql_que(executor, caller, enactor, eval, thing, pattr->number,
            pDBName, pQuery, ncargs, cargs, mudstate.global_regs);
    }
    else
    {
        notify_quiet(executor, T("At least one query option is required."));
    }
}

static CLinearTimeAbsolute Show_lsaNow;
static int Total_SystemTasks;
static int Total_RunQueueEntry;
static int Shown_RunQueueEntry;
static int Total_SemaphoreTimeout;
static int Shown_SemaphoreTimeout;
static dbref Show_Player_Target;
static dbref Show_Object_Target;
static int Show_Key;
static dbref Show_Player;
static int Show_bFirstLine;

int Total_SQLTimeout;
int Shown_SQLTimeout;

static int CallBack_ShowDispatches(PTASK_RECORD p)
{
    Total_SystemTasks++;
    CLinearTimeDelta ltd = p->ltaWhen - Show_lsaNow;
    if (p->fpTask == dispatch_DatabaseDump)
    {
        notify(Show_Player, tprintf(T("[%d]auto-@dump"), ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_FreeListReconstruction)
    {
        notify(Show_Player, tprintf(T("[%d]auto-@dbck"), ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_IdleCheck)
    {
        notify(Show_Player, tprintf(T("[%d]Check for idle players"), ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_CheckEvents)
    {
        notify(Show_Player, tprintf(T("[%d]Test for @daily time"), ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_KeepAlive)
    {
        notify(Show_Player, tprintf(T("[%d]Keep Alive"), ltd.ReturnSeconds()));
    }
#ifndef MEMORY_BASED
    else if (p->fpTask == dispatch_CacheTick)
    {
        notify(Show_Player, tprintf(T("[%d]Database cache tick"), ltd.ReturnSeconds()));
    }
#endif
    else if (p->fpTask == Task_ProcessCommand)
    {
        notify(Show_Player, tprintf(T("[%d]Further command quota"), ltd.ReturnSeconds()));
    }
#if defined(WINDOWS_NETWORKING)
    else if (p->fpTask == Task_FreeDescriptor)
    {
        notify(Show_Player, tprintf(T("[%d]Delayed descriptor deallocation"), ltd.ReturnSeconds()));
    }
    else if (p->fpTask == Task_DeferredClose)
    {
        notify(Show_Player, tprintf(T("[%d]Delayed socket close"), ltd.ReturnSeconds()));
    }
#endif
    else
    {
        Total_SystemTasks--;
    }
    return IU_NEXT_TASK;
}

static void ShowPsLine(BQUE *tmp)
{
    UTF8 *bufp = unparse_object(Show_Player, tmp->executor, false);
    if (tmp->IsTimed && Good_obj(tmp->u.s.sem))
    {
        CLinearTimeDelta ltd = tmp->waittime - Show_lsaNow;
        notify(Show_Player, tprintf(T("[#%d/%d]%s:%s"), tmp->u.s.sem, ltd.ReturnSeconds(), bufp, tmp->comm));
    }
    else if (tmp->IsTimed)
    {
        CLinearTimeDelta ltd = tmp->waittime - Show_lsaNow;
        notify(Show_Player, tprintf(T("[%d]%s:%s"), ltd.ReturnSeconds(), bufp, tmp->comm));
    }
    else if (Good_obj(tmp->u.s.sem))
    {
        notify(Show_Player, tprintf(T("[#%d]%s:%s"), tmp->u.s.sem, bufp, tmp->comm));
    }
    else
    {
        notify(Show_Player, tprintf(T("%s:%s"), bufp, tmp->comm));
    }
    UTF8 *bp = bufp;
    if (Show_Key == PS_LONG)
    {
        for (int i = 0; i < tmp->nargs; i++)
        {
            if (tmp->env[i] != nullptr)
            {
                safe_str(T("; Arg"), bufp, &bp);
                safe_chr((UTF8)(i + '0'), bufp, &bp);
                safe_str(T("=\xE2\x80\x98"), bufp, &bp);
                safe_str(tmp->env[i], bufp, &bp);
                safe_str(T("\xE2\x80\x99"), bufp, &bp);
            }
        }
        *bp = '\0';
        bp = unparse_object(Show_Player, tmp->enactor, false);
        notify(Show_Player, tprintf(T("   Enactor: %s%s"), bp, bufp));
        free_lbuf(bp);
    }
    free_lbuf(bufp);
}

static int CallBack_ShowWait(PTASK_RECORD p)
{
    if (p->fpTask != Task_RunQueueEntry)
    {
        return IU_NEXT_TASK;
    }

    Total_RunQueueEntry++;
    BQUE *tmp = (BQUE *)(p->arg_voidptr);
    if (que_want(tmp, Show_Player_Target, Show_Object_Target))
    {
        Shown_RunQueueEntry++;
        if (Show_Key == PS_SUMM)
        {
            return IU_NEXT_TASK;
        }
        if (Show_bFirstLine)
        {
            notify(Show_Player, T("----- Wait Queue -----"));
            Show_bFirstLine = false;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

static int CallBack_ShowSemaphore(PTASK_RECORD p)
{
    if (p->fpTask != Task_SemaphoreTimeout)
    {
        return IU_NEXT_TASK;
    }

    Total_SemaphoreTimeout++;
    BQUE *tmp = (BQUE *)(p->arg_voidptr);
    if (que_want(tmp, Show_Player_Target, Show_Object_Target))
    {
        Shown_SemaphoreTimeout++;
        if (Show_Key == PS_SUMM)
        {
            return IU_NEXT_TASK;
        }
        if (Show_bFirstLine)
        {
            notify(Show_Player, T("----- Semaphore Queue -----"));
            Show_bFirstLine = false;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

int CallBack_ShowSQLQueries(PTASK_RECORD p)
{
    if (p->fpTask != Task_SQLTimeout)
    {
        return IU_NEXT_TASK;
    }

    Total_SQLTimeout++;
    BQUE *tmp = (BQUE *)(p->arg_voidptr);
    if (que_want(tmp, Show_Player_Target, Show_Object_Target))
    {
        Shown_SQLTimeout++;
        if (Show_Key == PS_SUMM)
        {
            return IU_NEXT_TASK;
        }
        if (Show_bFirstLine)
        {
            notify(Show_Player, T("----- SQL Queries -----"));
            Show_bFirstLine = false;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

// ---------------------------------------------------------------------------
// do_ps: tell executor what commands they have pending in the queue
//
void do_ps(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *target, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *bufp;
    dbref executor_targ, obj_targ;

    // Figure out what to list the queue for.
    //
    if ((key & PS_ALL) && !See_Queue(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }
    if (!target || !*target)
    {
        obj_targ = NOTHING;
        if (key & PS_ALL)
        {
            executor_targ = NOTHING;
        }
        else
        {
            executor_targ = Owner(executor);
            if (!isPlayer(executor))
            {
                obj_targ = executor;
            }
        }
    }
    else
    {
        executor_targ = Owner(executor);
        obj_targ = match_controlled(executor, target);
        if (obj_targ == NOTHING)
        {
            return;
        }
        if (key & PS_ALL)
        {
            notify(executor, T("Can\xE2\x80\x99t specify a target and /all"));
            return;
        }
        if (isPlayer(obj_targ))
        {
            executor_targ = obj_targ;
            obj_targ = NOTHING;
        }
    }
    key = key & ~PS_ALL;

    switch (key)
    {
    case PS_BRIEF:
    case PS_SUMM:
    case PS_LONG:
        break;

    default:
        notify(executor, T("Illegal combination of switches."));
        return;
    }

    Show_lsaNow.GetUTC();
    Total_SystemTasks = 0;
    Total_RunQueueEntry = 0;
    Shown_RunQueueEntry = 0;
    Total_SemaphoreTimeout = 0;
    Shown_SemaphoreTimeout = 0;
    Total_SQLTimeout = 0;
    Shown_SQLTimeout = 0;
    Show_Player_Target = executor_targ;
    Show_Object_Target = obj_targ;
    Show_Key = key;
    Show_Player = executor;
    Show_bFirstLine = true;
    scheduler.TraverseOrdered(CallBack_ShowWait);
    Show_bFirstLine = true;
    scheduler.TraverseOrdered(CallBack_ShowSemaphore);
    Show_bFirstLine = true;
    scheduler.TraverseOrdered(CallBack_ShowSQLQueries);
    if (Wizard(executor))
    {
        notify(executor, T("----- System Queue -----"));
        scheduler.TraverseOrdered(CallBack_ShowDispatches);
    }

    // Display stats.
    //
    bufp = alloc_mbuf("do_ps");
    mux_sprintf(bufp, MBUF_SIZE, T("Totals: Wait Queue...%d/%d  Semaphores...%d/%d  SQL %d/%d"),
        Shown_RunQueueEntry, Total_RunQueueEntry,
        Shown_SemaphoreTimeout, Total_SemaphoreTimeout,
        Shown_SQLTimeout, Total_SQLTimeout);
    notify(executor, bufp);
    if (Wizard(executor))
    {
        mux_sprintf(bufp, MBUF_SIZE, T("        System Tasks.....%d"), Total_SystemTasks);
        notify(executor, bufp);
    }
    free_mbuf(bufp);
}

static CLinearTimeDelta ltdWarp;
static int CallBack_Warp(PTASK_RECORD p)
{
    if (  p->fpTask == Task_RunQueueEntry
       || p->fpTask == Task_SQLTimeout
       || p->fpTask == Task_SemaphoreTimeout)
    {
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (point->IsTimed)
        {
            point->waittime -= ltdWarp;
            p->ltaWhen -= ltdWarp;
            return IU_UPDATE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

// ---------------------------------------------------------------------------
// do_queue: Queue management
//
void do_queue(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (key == QUEUE_KICK)
    {
        int i = mux_atol(arg);
        int save_minPriority = scheduler.GetMinPriority();
        if (save_minPriority <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(executor, T("Warning: automatic dequeueing is disabled."));
            scheduler.SetMinPriority(PRIORITY_CF_DEQUEUE_ENABLED);
        }
        CLinearTimeAbsolute lsaNow;
        lsaNow.GetUTC();
        scheduler.ReadyTasks(lsaNow);
        int ncmds = scheduler.RunTasks(i);
        scheduler.SetMinPriority(save_minPriority);

        if (!Quiet(executor))
        {
            notify(executor, tprintf(T("%d commands processed."), ncmds));
        }
    }
    else if (key == QUEUE_WARP)
    {
        int iWarp = mux_atol(arg);
        ltdWarp.SetSeconds(iWarp);
        if (scheduler.GetMinPriority() <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(executor, T("Warning: automatic dequeueing is disabled."));
        }

        scheduler.TraverseUnordered(CallBack_Warp);

        if (Quiet(executor))
        {
            return;
        }
        if (0 < iWarp)
        {
            notify(executor, tprintf(T("WaitQ timer advanced %d seconds."), iWarp));
        }
        else if (iWarp < 0)
        {
            notify(executor, tprintf(T("WaitQ timer set back %d seconds."), iWarp));
        }
        else
        {
            notify(executor, T("Object queue appended to player queue."));
        }
    }
}
