// cque.cpp -- commands and functions for manipulating the command queue.
//
// $Id: cque.cpp,v 1.16 2004-05-16 23:02:52 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <signal.h>

#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "powers.h"

extern int  a_Queue(dbref, int);
extern int  QueueMax(dbref);
bool break_called = false;

CLinearTimeDelta GetProcessorUsage(void)
{
    CLinearTimeDelta ltd;
#ifdef WIN32
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        FILETIME ftCreate;
        FILETIME ftExit;
        FILETIME ftKernel;
        FILETIME ftUser;
        fpGetProcessTimes(hGameProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);
        ltd.Set100ns(*(INT64 *)(&ftUser));
        return ltd;
    }
#endif

#if !defined(WIN32) && defined(HAVE_GETRUSAGE)

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    ltd.SetTimeValueStruct(&usage.ru_utime);
    return ltd;

#else

    // Either this Unix doesn't have getrusage or this is a
    // fall-through case for Win32.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    ltd = ltaNow - mudstate.start_time;
    return ltd;

#endif
}

// ---------------------------------------------------------------------------
// add_to: Adjust an object's queue or semaphore count.
//
static int add_to(dbref executor, int am, int attrnum)
{
    int aflags;
    dbref aowner;

    char *atr_gotten = atr_get(executor, attrnum, &aowner, &aflags);
    int num = mux_atol(atr_gotten);
    free_lbuf(atr_gotten);
    num += am;

    char buff[20];
    int nlen = 0;
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
void Task_RunQueueEntry(void *pEntry, int iUnused)
{
    BQUE *point = (BQUE *)pEntry;
    dbref executor = point->executor;

    if (Good_obj(executor) && !Going(executor))
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
                if (point->scr[i])
                {
                    int n = strlen(point->scr[i]);
                    memcpy(mudstate.global_regs[i], point->scr[i], n+1);
                    mudstate.glob_reg_len[i] = n;
                }
                else
                {
                    mudstate.global_regs[i][0] = '\0';
                    mudstate.glob_reg_len[i] = 0;
                }
            }

            char *command = point->comm;
            break_called = false;
            while (command && !break_called)
            {
                char *cp = parse_to(&command, ';', 0);
                if (cp && *cp)
                {
                    int numpipes = 0;
                    while (  command
                          && (*command == '|')
                          && (numpipes < mudconf.ntfy_nest_lim))
                    {
                        command++;
                        numpipes++;
                        mudstate.inpipe = true;
                        mudstate.poutnew = alloc_lbuf("process_command.pipe");
                        mudstate.poutbufc = mudstate.poutnew;
                        mudstate.poutobj = executor;

                        // No lag check on piped commands.
                        //
                        process_command(executor, point->caller, point->enactor,
                            false, cp, point->env, point->nargs);
                        if (mudstate.pout)
                        {
                            free_lbuf(mudstate.pout);
                            mudstate.pout = NULL;
                        }

                        *mudstate.poutbufc = '\0';
                        mudstate.pout = mudstate.poutnew;
                        cp = parse_to(&command, ';', 0);
                    }
                    mudstate.inpipe = false;

                    CLinearTimeAbsolute ltaBegin;
                    ltaBegin.GetUTC();
                    MuxAlarm.Set(mudconf.max_cmdsecs);
                    CLinearTimeDelta ltdUsageBegin = GetProcessorUsage();

                    char *log_cmdbuf = process_command(executor, point->caller,
                        point->enactor, false, cp, point->env, point->nargs);

                    CLinearTimeAbsolute ltaEnd;
                    ltaEnd.GetUTC();
                    if (MuxAlarm.bAlarmed)
                    {
                        notify(executor, "GAME: Expensive activity abbreviated.");
                        halt_que(executor, NOTHING);
                        s_Flags(point->enactor, FLAG_WORD1, Flags(point->enactor) | HALT);
                    }
                    MuxAlarm.Clear();

                    CLinearTimeDelta ltdUsageEnd = GetProcessorUsage();
                    CLinearTimeDelta ltd = ltdUsageEnd - ltdUsageBegin;
                    db[executor].cpu_time_used += ltd;

                    if (mudstate.pout)
                    {
                        free_lbuf(mudstate.pout);
                        mudstate.pout = NULL;
                    }

                    ltd = ltaEnd - ltaBegin;
                    if (ltd > mudconf.rpt_cmdsecs)
                    {
                        STARTLOG(LOG_PROBLEMS, "CMD", "CPU");
                        log_name_and_loc(executor);
                        char *logbuf = alloc_lbuf("do_top.LOG.cpu");
                        sprintf(logbuf, " queued command taking %s secs (enactor #%d): ",
                            ltd.ReturnSecondsString(4), point->enactor);
                        log_text(logbuf);
                        free_lbuf(logbuf);
                        log_text(log_cmdbuf);
                        ENDLOG;
                    }
                }
            }
        }
        MEMFREE(point->text);
        point->text = NULL;
        free_qentry(point);
    }

    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i][0] = '\0';
        mudstate.glob_reg_len[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// que_want: Do we want this queue entry?
//
static bool que_want(BQUE *entry, dbref ptarg, dbref otarg)
{
    if ((ptarg != NOTHING) && (ptarg != Owner(entry->executor)))
        return false;
    return ((otarg == NOTHING) || (otarg == entry->executor));
}

void Task_SemaphoreTimeout(void *pExpired, int iUnused)
{
    // A semaphore has timed out.
    //
    BQUE *point = (BQUE *)pExpired;
    add_to(point->sem, -1, point->attr);
    point->sem = NOTHING;
    Task_RunQueueEntry(point, 0);
}

dbref Halt_Player_Target;
dbref Halt_Object_Target;
int   Halt_Entries;
dbref Halt_Player_Run;
dbref Halt_Entries_Run;

int CallBack_HaltQueue(PTASK_RECORD p)
{
    if (  p->fpTask == Task_RunQueueEntry
       || p->fpTask == Task_SemaphoreTimeout)
    {
        // This is a @wait or timed semaphore task.
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
                add_to(point->sem, -1, point->attr);
            }
            MEMFREE(point->text);
            point->text = NULL;
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
void do_halt(dbref executor, dbref caller, dbref enactor, int key, char *target)
{
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
            notify(executor, "Can't specify a target and /all");
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
    notify(Owner(executor), tprintf("%d queue entr%s removed.", numhalted, numhalted == 1 ? "y" : "ies"));
}

int Notify_Key;
int Notify_Num_Done;
int Notify_Num_Max;
int Notify_Sem;
int Notify_Attr;

// NFY_DRAIN or NFY_NFYALL
//
int CallBack_NotifySemaphoreDrainOrAll(PTASK_RECORD p)
{
    if (p->fpTask == Task_SemaphoreTimeout)
    {
        // This represents a semaphore.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (  point->sem == Notify_Sem
           && (  point->attr == Notify_Attr
              || !Notify_Attr))
        {
            Notify_Num_Done++;
            if (Notify_Key == NFY_DRAIN)
            {
                // Discard the command
                //
                giveto(point->executor, mudconf.waitcost);
                a_Queue(Owner(point->executor), -1);
                MEMFREE(point->text);
                point->text = NULL;
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

// NFY_NFY or NFY_QUIET
//
int CallBack_NotifySemaphoreFirstOrQuiet(PTASK_RECORD p)
{
    // If we've notified enough, exit.
    //
    if (  Notify_Key == NFY_NFY
       && Notify_Num_Done >= Notify_Num_Max)
    {
        return IU_DONE;
    }

    if (p->fpTask == Task_SemaphoreTimeout)
    {
        // This represents a semaphore.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (  point->sem == Notify_Sem
           && (  point->attr == Notify_Attr
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
        char *str = atr_get(sem, attr, &aowner, &aflags);
        cSemaphore = mux_atol(str);
        free_lbuf(str);
    }

    Notify_Num_Done = 0;
    if (cSemaphore > 0)
    {
        Notify_Key     = key;
        Notify_Sem     = sem;
        Notify_Attr    = attr;
        Notify_Num_Max = count;
        if (  key == NFY_NFY
           || key == NFY_QUIET)
        {
            scheduler.TraverseOrdered(CallBack_NotifySemaphoreFirstOrQuiet);
        }
        else
        {
            scheduler.TraverseUnordered(CallBack_NotifySemaphoreDrainOrAll);
        }
    }

    // Update the sem waiters count.
    //
    if (key == NFY_NFY)
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
    int   key,
    int   nargs,
    char *what,
    char *count
)
{
    int loccount, attr = 0;
    ATTR *ap;
    char *obj = parse_to(&what, '/', 0);
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
        if (!what || !*what)
        {
            ap = NULL;
        }
        else
        {
            ap = atr_str(what);
        }

        if (!ap)
        {
            attr = A_SEMAPHORE;
        }
        else
        {
            // Do they have permission to set this attribute?
            //
            if (bCanSetAttr(executor, thing, ap))
            {
                attr = ap->number;
            }
            else
            {
                notify_quiet(executor, NOPERM_MESSAGE);
                return;
            }
        }

        if (count && *count)
        {
            loccount = mux_atol(count);
        }
        else
        {
            loccount = 1;
        }
        if (loccount > 0)
        {
            nfy_que(thing, attr, key, loccount);
            if (  (!(Quiet(executor) || Quiet(thing)))
               && key != NFY_QUIET)
            {
                if (key == NFY_DRAIN)
                {
                    notify_quiet(executor, "Drained.");
                }
                else
                {
                    notify_quiet(executor, "Notified.");
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// setup_que: Set up a queue entry.
//
static BQUE *setup_que(dbref executor, dbref caller, dbref enactor,
                       char *command, char *args[], int nargs, char *sargs[])
{
    int a;
    BQUE *tmp;

    // Can we run commands at all?
    //
    if (Halted(executor))
        return NULL;

    // Make sure executor can afford to do it.
    //
    a = mudconf.waitcost;
    if (mudconf.machinecost && RandomINT32(0, mudconf.machinecost-1) == 0)
    {
        a++;
    }
    if (!payfor(executor, a))
    {
        notify(Owner(executor), "Not enough money to queue command.");
        return NULL;
    }

    // Wizards and their objs may queue up to db_top+1 cmds. Players are
    // limited to QUEUE_QUOTA. -mnp
    //
    a = QueueMax(Owner(executor));
    if (a_Queue(Owner(executor), 1) > a)
    {
        notify(Owner(executor),
            "Run away objects: too many commands queued.  Halted.");
        halt_que(Owner(executor), NOTHING);

        // Halt also means no command execution allowed.
        //
        s_Halted(executor);
        return NULL;
    }

    // We passed all the tests.
    //

    // Calculate the length of the save string.
    //
    unsigned int tlen = 0;
    static unsigned int nCommand;
    static unsigned int nLenEnv[NUM_ENV_VARS];
    static unsigned int nLenRegs[MAX_GLOBAL_REGS];

    if (command)
    {
        nCommand = strlen(command) + 1;
        tlen = nCommand;
    }
    if (nargs > NUM_ENV_VARS)
    {
        nargs = NUM_ENV_VARS;
    }
    for (a = 0; a < nargs; a++)
    {
        if (args[a])
        {
            nLenEnv[a] = strlen(args[a]) + 1;
            tlen += nLenEnv[a];
        }
    }
    if (sargs)
    {
        for (a = 0; a < MAX_GLOBAL_REGS; a++)
        {
            if (sargs[a])
            {
                nLenRegs[a] = strlen(sargs[a]) + 1;
                tlen += nLenRegs[a];
            }
        }
    }

    // Create the qeue entry and load the save string.
    //
    tmp = alloc_qentry("setup_que.qblock");
    tmp->comm = NULL;

    char *tptr = tmp->text = (char *)MEMALLOC(tlen);
    (void)ISOUTOFMEMORY(tptr);

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
            tmp->env[a] = NULL;
        }
    }
    for ( ; a < NUM_ENV_VARS; a++)
    {
        tmp->env[a] = NULL;
    }
    for (a = 0; a < MAX_GLOBAL_REGS; a++)
    {
        tmp->scr[a] = NULL;
    }
    if (sargs)
    {
        for (a = 0; a < MAX_GLOBAL_REGS; a++)
        {
            if (sargs[a])
            {
                memcpy(tptr, sargs[a], nLenRegs[a]);
                tmp->scr[a] = tptr;
                tptr += nLenRegs[a];
            }
        }
    }

    // Load the rest of the queue block.
    //
    tmp->executor = executor;
    tmp->IsTimed = false;
    tmp->sem = NOTHING;
    tmp->attr = 0;
    tmp->enactor = enactor;
    tmp->caller = caller;
    tmp->nargs = nargs;
    return tmp;
}

// ---------------------------------------------------------------------------
// wait_que: Add commands to the wait or semaphore queues.
//
void wait_que
(
    dbref executor,
    dbref caller,
    dbref enactor,
    bool bTimed,
    CLinearTimeAbsolute &ltaWhen,
    dbref sem,
    int   attr,
    char *command,
    char *args[],
    int   nargs,
    char *sargs[]
)
{
    if (!(mudconf.control_flags & CF_INTERP))
    {
        return;
    }

    BQUE *tmp = setup_que(executor, caller, enactor, command, args, nargs, sargs);
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
    tmp->sem = sem;
    tmp->attr = attr;

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

// ---------------------------------------------------------------------------
// do_wait: Command interface to wait_que
//
void do_wait
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int key,
    char *event,
    char *cmd,
    char *cargs[],
    int ncargs
)
{
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
        wait_que(executor, caller, enactor, true, ltaWhen, NOTHING, 0, cmd,
            cargs, ncargs, mudstate.global_regs);
        return;
    }

    // Semaphore wait with optional timeout.
    //
    char *what = parse_to(&event, '/', 0);
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
        int attr = A_SEMAPHORE;
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
                ATTR *ap = atr_str(event);
                if (!ap)
                {
                    attr = mkattr(executor, event);
                    if (attr <= 0)
                    {
                        notify_quiet(executor, "Invalid attribute.");
                        return;
                    }
                    ap = atr_num(attr);
                }
                else
                {
                    attr = ap->number;
                }
                if (!bCanSetAttr(executor, thing, ap))
                {
                    notify_quiet(executor, NOPERM_MESSAGE);
                    return;
                }
            }
        }

        int num = add_to(thing, 1, attr);
        if (num <= 0)
        {
            // Thing over-notified, run the command immediately.
            //
            thing = NOTHING;
            bTimed = false;
        }
        wait_que(executor, caller, enactor, bTimed, ltaWhen, thing, attr,
            cmd, cargs, ncargs, mudstate.global_regs);
    }
}

CLinearTimeAbsolute Show_lsaNow;
int Total_SystemTasks;
int Total_RunQueueEntry;
int Shown_RunQueueEntry;
int Total_SemaphoreTimeout;
int Shown_SemaphoreTimeout;
dbref Show_Player_Target;
dbref Show_Object_Target;
int Show_Key;
dbref Show_Player;
int Show_bFirstLine;

#ifdef WIN32
void Task_FreeDescriptor(void *arg_voidptr, int arg_Integer);
#endif
void dispatch_DatabaseDump(void *pUnused, int iUnused);
void dispatch_FreeListReconstruction(void *pUnused, int iUnused);
void dispatch_IdleCheck(void *pUnused, int iUnused);
void dispatch_CheckEvents(void *pUnused, int iUnused);
#ifndef MEMORY_BASED
void dispatch_CacheTick(void *pUnused, int iUnused);
#endif

int CallBack_ShowDispatches(PTASK_RECORD p)
{
    Total_SystemTasks++;
    CLinearTimeDelta ltd = p->ltaWhen - Show_lsaNow;
    if (p->fpTask == dispatch_DatabaseDump)
    {
        notify(Show_Player, tprintf("[%d]auto-@dump", ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_FreeListReconstruction)
    {
        notify(Show_Player, tprintf("[%d]auto-@dbck", ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_IdleCheck)
    {
        notify(Show_Player, tprintf("[%d]Check for idle players", ltd.ReturnSeconds()));
    }
    else if (p->fpTask == dispatch_CheckEvents)
    {
        notify(Show_Player, tprintf("[%d]Test for @daily time", ltd.ReturnSeconds()));
    }
#ifndef MEMORY_BASED
    else if (p->fpTask == dispatch_CacheTick)
    {
        notify(Show_Player, tprintf("[%d]Database cache tick", ltd.ReturnSeconds()));
    }
#endif
    else if (p->fpTask == Task_ProcessCommand)
    {
        notify(Show_Player, tprintf("[%d]Further command quota", ltd.ReturnSeconds()));
    }
#ifdef WIN32
    else if (p->fpTask == Task_FreeDescriptor)
    {
        notify(Show_Player, tprintf("[%d]Delayed descriptor deallocation", ltd.ReturnSeconds()));
    }
#endif
    else
    {
        Total_SystemTasks--;
    }
    return IU_NEXT_TASK;
}

void ShowPsLine(BQUE *tmp)
{
    char *bufp = unparse_object(Show_Player, tmp->executor, false);
    if (tmp->IsTimed && (Good_obj(tmp->sem)))
    {
        CLinearTimeDelta ltd = tmp->waittime - Show_lsaNow;
        notify(Show_Player, tprintf("[#%d/%d]%s:%s", tmp->sem, ltd.ReturnSeconds(), bufp, tmp->comm));
    }
    else if (tmp->IsTimed)
    {
        CLinearTimeDelta ltd = tmp->waittime - Show_lsaNow;
        notify(Show_Player, tprintf("[%d]%s:%s", ltd.ReturnSeconds(), bufp, tmp->comm));
    }
    else if (Good_obj(tmp->sem))
    {
        notify(Show_Player, tprintf("[#%d]%s:%s", tmp->sem, bufp, tmp->comm));
    }
    else
    {
        notify(Show_Player, tprintf("%s:%s", bufp, tmp->comm));
    }
    char *bp = bufp;
    if (Show_Key == PS_LONG)
    {
        for (int i = 0; i < tmp->nargs; i++)
        {
            if (tmp->env[i] != NULL)
            {
                safe_str("; Arg", bufp, &bp);
                safe_chr((char)(i + '0'), bufp, &bp);
                safe_str("='", bufp, &bp);
                safe_str(tmp->env[i], bufp, &bp);
                safe_chr('\'', bufp, &bp);
            }
        }
        *bp = '\0';
        bp = unparse_object(Show_Player, tmp->enactor, false);
        notify(Show_Player, tprintf("   Enactor: %s%s", bp, bufp));
        free_lbuf(bp);
    }
    free_lbuf(bufp);
}

int CallBack_ShowWait(PTASK_RECORD p)
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
            notify(Show_Player, "----- Wait Queue -----");
            Show_bFirstLine = false;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

int CallBack_ShowSemaphore(PTASK_RECORD p)
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
            notify(Show_Player, "----- Semaphore Queue -----");
            Show_bFirstLine = false;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

// ---------------------------------------------------------------------------
// do_ps: tell executor what commands they have pending in the queue
//
void do_ps(dbref executor, dbref caller, dbref enactor, int key, char *target)
{
    char *bufp;
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
            notify(executor, "Can't specify a target and /all");
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
        notify(executor, "Illegal combination of switches.");
        return;
    }

    Show_lsaNow.GetUTC();
    Total_SystemTasks = 0;
    Total_RunQueueEntry = 0;
    Shown_RunQueueEntry = 0;
    Total_SemaphoreTimeout = 0;
    Shown_SemaphoreTimeout = 0;
    Show_Player_Target = executor_targ;
    Show_Object_Target = obj_targ;
    Show_Key = key;
    Show_Player = executor;
    Show_bFirstLine = true;
    scheduler.TraverseOrdered(CallBack_ShowWait);
    Show_bFirstLine = true;
    scheduler.TraverseOrdered(CallBack_ShowSemaphore);
    if (Wizard(executor))
    {
        notify(executor, "----- System Queue -----");
        scheduler.TraverseOrdered(CallBack_ShowDispatches);
    }

    // Display stats.
    //
    bufp = alloc_mbuf("do_ps");
    sprintf(bufp, "Totals: Wait Queue...%d/%d  Semaphores...%d/%d",
        Shown_RunQueueEntry, Total_RunQueueEntry,
        Shown_SemaphoreTimeout, Total_SemaphoreTimeout);
    notify(executor, bufp);
    if (Wizard(executor))
    {
        sprintf(bufp, "        System Tasks.....%d", Total_SystemTasks);
        notify(executor, bufp);
    }
    free_mbuf(bufp);
}

CLinearTimeDelta ltdWarp;
int CallBack_Warp(PTASK_RECORD p)
{
    if (p->fpTask == Task_RunQueueEntry || p->fpTask == Task_SemaphoreTimeout)
    {
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (point->IsTimed)
        {
            point->waittime -= ltdWarp;
            return IU_UPDATE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

// ---------------------------------------------------------------------------
// do_queue: Queue management
//
void do_queue(dbref executor, dbref caller, dbref enactor, int key, char *arg)
{
    if (key == QUEUE_KICK)
    {
        int i = mux_atol(arg);
        int save_minPriority = scheduler.GetMinPriority();
        if (save_minPriority <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(executor, "Warning: automatic dequeueing is disabled.");
            scheduler.SetMinPriority(PRIORITY_CF_DEQUEUE_ENABLED);
        }
        CLinearTimeAbsolute lsaNow;
        lsaNow.GetUTC();
        scheduler.ReadyTasks(lsaNow);
        int ncmds = scheduler.RunTasks(i);
        scheduler.SetMinPriority(save_minPriority);

        if (!Quiet(executor))
        {
            notify(executor, tprintf("%d commands processed.", ncmds));
        }
    }
    else if (key == QUEUE_WARP)
    {
        int iWarp = mux_atol(arg);
        CLinearTimeDelta ltdWarp;
        ltdWarp.SetSeconds(iWarp);
        if (scheduler.GetMinPriority() <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(executor, "Warning: automatic dequeueing is disabled.");
        }

        scheduler.TraverseUnordered(CallBack_Warp);

        if (Quiet(executor))
        {
            return;
        }
        if (iWarp > 0)
        {
            notify(executor, tprintf("WaitQ timer advanced %d seconds.", iWarp));
        }
        else if (iWarp < 0)
        {
            notify(executor, tprintf("WaitQ timer set back %d seconds.", iWarp));
        }
        else
        {
            notify(executor, "Object queue appended to player queue.");
        }
    }
}
