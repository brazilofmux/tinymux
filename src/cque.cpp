// cque.cpp -- commands and functions for manipulating the command queue.
//
// $Id: cque.cpp,v 1.15 2000-11-15 02:52:31 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <signal.h>

#include "mudconf.h"
#include "db.h"
#include "htab.h"
#include "interface.h"
#include "match.h"
#include "attrs.h"
#include "flags.h"
#include "powers.h"
#include "command.h"
#include "alloc.h"

extern int FDECL(a_Queue, (dbref, int));
extern void FDECL(s_Queue, (dbref, int));
extern int FDECL(QueueMax, (dbref));

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

    // Win9x - We can really only report the system time with a
    // high-resolution timer. This doesn't seperate the time from
    // this process from other processes that are also running on
    // the same machine, but it's the best we can do on Win9x.
    //
    // The scheduling in Win9x is totally screwed up, so even if
    // Win9x did provide the information, it would certainly be
    // wrong.
    //
    if (bQueryPerformanceAvailable)
    {
        // The QueryPerformanceFrequency call at game startup
        // succeeded. The frequency number is the number of ticks
        // per second.
        //
        INT64 li;
        if (QueryPerformanceCounter((LARGE_INTEGER *)&li))
        {
            li = QP_A*li + (QP_B*li+QP_C)/QP_D;
            ltd.Set100ns(li);
            return ltd;
        }
        bQueryPerformanceAvailable = FALSE;
    }
#endif // WIN32

#if !defined(WIN32) && defined(HAVE_GETRUSAGE)

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    ltd.SetTimeValueStruct(&usage.ru_utime);
    return ltd;

#else // !WIN32 && HAVE_GETRUSAGE

    // Either this Unix doesn't have getrusage or this is a
    // fall-through case for Win32.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    ltd = ltaNow - mudstate.start_time;
    return ltd;

#endif // !WIN32 && HAVE_GETRUSAGE
}

/*
 * ---------------------------------------------------------------------------
 * * add_to: Adjust an object's queue or semaphore count.
 */

static int add_to(dbref player, int am, int attrnum)
{
    int aflags;
    dbref aowner;

    char *atr_gotten = atr_get(player, attrnum, &aowner, &aflags);
    int num = Tiny_atol(atr_gotten);
    free_lbuf(atr_gotten);
    num += am;

    char buff[20];
    int nlen = 0;
    *buff = '\0';
    if (num)
    {
        nlen = Tiny_ltoa(num, buff);
    }
    atr_add_raw_LEN(player, attrnum, buff, nlen);
    return num;
}

// This Task assumes that pEntry is already unlinked from any lists it may have been related to.
//
void Task_RunQueueEntry(void *pEntry, int iUnused)
{
    BQUE *point = (BQUE *)pEntry;
    dbref player = point->player;

    if ((player >= 0) && !Going(player))
    {
        giveto(player, mudconf.waitcost);
        mudstate.curr_enactor = point->cause;
        mudstate.curr_player = player;
        a_Queue(Owner(player), -1);
        point->player = NOTHING;
        if (!Halted(player))
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
            while (command)
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
                        mudstate.inpipe = 1;
                        mudstate.poutnew = alloc_lbuf("process_command.pipe");
                        mudstate.poutbufc = mudstate.poutnew;
                        mudstate.poutobj = player;

                        // No lag check on piped commands.
                        //
                        process_command(player, point->cause, 0, cp, point->env, point->nargs);
                        if (mudstate.pout)
                        {
                            free_lbuf(mudstate.pout);
                            mudstate.pout = NULL;
                        }
                    
                        *mudstate.poutbufc = '\0';
                        mudstate.pout = mudstate.poutnew;
                        cp = parse_to(&command, ';', 0);
                    } 
                    mudstate.inpipe = 0;

                    CLinearTimeAbsolute ltaBegin;
                    ltaBegin.GetUTC();
                    CLinearTimeDelta ltdUsageBegin = GetProcessorUsage();

                    process_command(player, point->cause, 0, cp, point->env, point->nargs);

                    CLinearTimeAbsolute ltaEnd;
                    ltaEnd.GetUTC();

                    CLinearTimeDelta ltdUsageEnd = GetProcessorUsage();
                    CLinearTimeDelta ltd = ltdUsageEnd - ltdUsageBegin;
                    db[player].cpu_time_used += ltd;

                    if (mudstate.pout)
                    {
                        free_lbuf(mudstate.pout);
                        mudstate.pout = NULL;
                    }

                    ltd = ltaEnd - ltaBegin;
                    if (ltd.ReturnSeconds() >= mudconf.max_cmdsecs)
                    {
                        STARTLOG(LOG_PROBLEMS, "CMD", "CPU");
                        log_name_and_loc(player);
                        char *logbuf = alloc_lbuf("do_top.LOG.cpu");
                        long ms = ltd.ReturnMilliseconds();
                        sprintf(logbuf, " queued command taking %d.%02d secs (enactor #%d): ", ms/100, ms%100, point->cause);
                        log_text(logbuf);
                        free_lbuf(logbuf);
                        //log_text(log_cmdbuf);
                        ENDLOG;
                    }
                }
            }
        }
        MEMFREE(point->text);
        free_qentry(point);
    }

    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i][0] = '\0';
        mudstate.glob_reg_len[i] = 0;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * que_want: Do we want this queue entry?
 */

static int que_want(BQUE *entry, dbref ptarg, dbref otarg)
{
    if ((ptarg != NOTHING) && (ptarg != Owner(entry->player)))
        return 0;
    if ((otarg != NOTHING) && (otarg != entry->player))
        return 0;
    return 1;
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
int Halt_HaltedEntries;

int CallBack_HaltQueue(PTASK_RECORD p)
{
    if (p->fpTask == Task_RunQueueEntry || p->fpTask == Task_SemaphoreTimeout)
    {
        // This is a @wait or timed semaphore task.
        //
        BQUE *point = (BQUE *)(p->arg_voidptr);
        if (que_want(point, Halt_Player_Target, Halt_Object_Target))
        {
            Halt_HaltedEntries++;
            if (p->fpTask == Task_SemaphoreTimeout)
            {
                add_to(point->sem, -1, point->attr);
            }
            MEMFREE(point->text);
            free_qentry(point);
            return IU_REMOVE_TASK;
        }
    }
    return IU_NEXT_TASK;
}

/*
 * ---------------------------------------------------------------------------
 * * halt_que: Remove all queued commands from a certain player
 */

int halt_que(dbref player, dbref object)
{
    Halt_Player_Target = player;
    Halt_Object_Target = object;
    Halt_HaltedEntries = 0;

    // Process @wait, timed semaphores, and untimed semaphores.
    //
    scheduler.TraverseUnordered(CallBack_HaltQueue);

    if (player == NOTHING)
    {
        player = Owner(object);
    }
    giveto(player, (mudconf.waitcost * Halt_HaltedEntries));
    if (object == NOTHING)
    {
        s_Queue(player, 0);
    }
    else
    {
        a_Queue(player, -Halt_HaltedEntries);
    }
    return Halt_HaltedEntries;
}

/*
 * ---------------------------------------------------------------------------
 * * do_halt: Command interface to halt_que.
 */

void do_halt(dbref player, dbref cause, int key, char *target)
{
    dbref player_targ, obj_targ;

    if ((key & HALT_ALL) && !(Can_Halt(player)))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }

    // Figure out what to halt.
    //
    if (!target || !*target)
    {
        obj_targ = NOTHING;
        if (key & HALT_ALL)
        {
            player_targ = NOTHING;
        }
        else
        {
            player_targ = Owner(player);
            if (!isPlayer(player))
            {
                obj_targ = player;
            }
        }
    }
    else
    {
        if (Can_Halt(player))
        {
            obj_targ = match_thing(player, target);
        }
        else
        {
            obj_targ = match_controlled(player, target);
        }
        if (obj_targ == NOTHING)
        {
            return;
        }
        if (key & HALT_ALL)
        {
            notify(player, "Can't specify a target and /all");
            return;
        }
        if (isPlayer(obj_targ))
        {
            player_targ = obj_targ;
            obj_targ = NOTHING;
        }
        else
        {
            player_targ = NOTHING;
        }
    }

    int numhalted = halt_que(player_targ, obj_targ);
    if (Quiet(player))
    {
        return;
    }
    if (numhalted == 1)
    {
        notify(Owner(player), "1 queue entries removed.");
    }
    else
    {
        notify(Owner(player), tprintf("%d queue entries removed.", numhalted));
    }
}

int Notify_Key;
int Notify_Num_Done;
int Notify_Num_Max;
int Notify_Sem;
int Notify_Attr;

int CallBack_NotifySemaphore(PTASK_RECORD p)
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
            if (Notify_Key == NFY_DRAIN)
            {
                // Discard the command
                //
                giveto(point->player, mudconf.waitcost);
                a_Queue(Owner(point->player), -1);
                MEMFREE(point->text);
                free_qentry(point);
                return IU_REMOVE_TASK;
            }
            else
            {
                // Allow the command to run. The priority may have been
                // PRIORITY_SUSPEND, so we need to change it.
                //
                if (Typeof(point->cause) == TYPE_PLAYER)
                {
                    p->iPriority = PRIORITY_PLAYER;
                }
                else
                {
                    p->iPriority = PRIORITY_OBJECT;
                }
                p->fpTask = Task_RunQueueEntry;
                return IU_UPDATE_TASK;
            }
        }
    }
    return IU_NEXT_TASK;
}

/*
 * ---------------------------------------------------------------------------
 * * nfy_que: Notify commands from the queue and perform or discard them.
 */

int nfy_que(dbref sem, int attr, int key, int count)
{
    int cSemaphore = 1;
    if (attr)
    {
        int   aflags;
        dbref aowner;
        char *str = atr_get(sem, attr, &aowner, &aflags);
        cSemaphore = Tiny_atol(str);
        free_lbuf(str);
    }
    
    Notify_Num_Done = 0;
    if (cSemaphore > 0)
    {
        Notify_Key     = key;
        Notify_Sem     = sem;
        Notify_Attr    = attr;
        Notify_Num_Max = count;
        scheduler.TraverseUnordered(CallBack_NotifySemaphore);
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

/*
 * ---------------------------------------------------------------------------
 * * do_notify: Command interface to nfy_que
 */


void do_notify(dbref player, dbref cause, int key, char *what, char *count)
{
    dbref thing, aowner;
    int loccount, attr = 0, aflags;
    ATTR *ap;
    char *obj;
    
    obj = parse_to(&what, '/', 0);
    init_match(player, obj, NOTYPE);
    match_everything(0);
    
    if ((thing = noisy_match_result()) < 0)
    {
        notify(player, "No match.");
    }
    else if (!controls(player, thing) && !Link_ok(thing))
    {
        notify(player, NOPERM_MESSAGE);
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
            /* Do they have permission to set this attribute? */
            atr_pget_info(thing, ap->number, &aowner, &aflags);
            
            if (Set_attr(player, thing, ap, aflags))
            {
                attr = ap->number;
            }
            else
            {
                notify_quiet(player, NOPERM_MESSAGE);
                return;
            }
        }
        
        if (count && *count)
        {
            loccount = Tiny_atol(count);
        }
        else
        {
            loccount = 1;
        }
        if (loccount > 0)
        {
            nfy_que(thing, attr, key, loccount);
            if (!(Quiet(player) || Quiet(thing)))
            {
                if (key == NFY_DRAIN)
                    notify_quiet(player, "Drained.");
                else
                    notify_quiet(player, "Notified.");
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * setup_que: Set up a queue entry.
 */

static BQUE *setup_que(dbref player, dbref cause, char *command, char *args[], int nargs, char *sargs[])
{
    int a;
    BQUE *tmp;
    char *tptr;

    // Can we run commands at all?
    //
    if (Halted(player))
        return NULL;

    // Make sure player can afford to do it.
    //
    a = mudconf.waitcost;
    if (mudconf.machinecost && RandomLong(0, mudconf.machinecost-1) == 0)
    {
        a++;
    }
    if (!payfor(player, a))
    {
        notify(Owner(player), "Not enough money to queue command.");
        return NULL;
    }

    // Wizards and their objs may queue up to db_top+1 cmds. Players are
    // limited to QUEUE_QUOTA. -mnp
    //
    a = QueueMax(Owner(player));
    if (a_Queue(Owner(player), 1) > a)
    {
        notify(Owner(player),
            "Run away objects: too many commands queued.  Halted.");
        halt_que(Owner(player), NOTHING);

        // Halt also means no command execution allowed.
        //
        s_Halted(player);
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

    tptr = tmp->text = (char *)MEMALLOC(tlen);
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
    tmp->player = player;
    tmp->IsTimed = FALSE;
    tmp->sem = NOTHING;
    tmp->attr = 0;
    tmp->cause = cause;
    tmp->nargs = nargs;
    return tmp;
}

/*
 * ---------------------------------------------------------------------------
 * * wait_que: Add commands to the wait or semaphore queues.
 */

void wait_que(dbref player, dbref cause, int wait, dbref sem, int attr, char *command, char *args[], int nargs, char *sargs[])
{
    if (!(mudconf.control_flags & CF_INTERP)) return;

    BQUE *tmp = setup_que(player, cause, command, args, nargs, sargs);
    if (!tmp) return;

    int iPriority;
    if (Typeof(tmp->cause) == TYPE_PLAYER)
    {
        iPriority = PRIORITY_PLAYER;
    }
    else
    {
        iPriority = PRIORITY_OBJECT;
    }

    if (wait == 0)
    {
        // This means that waitime is not used.
        //
        tmp->IsTimed = FALSE;
    }
    else
    {
        tmp->IsTimed = TRUE;
        tmp->waittime.GetUTC();
        CLinearTimeDelta ltdWait;
        ltdWait.SetSeconds(wait);
        tmp->waittime += ltdWait;
    }

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

/*
 * ---------------------------------------------------------------------------
 * * do_wait: Command interface to wait_que
 */

void do_wait(dbref player, dbref cause, int key, char *event, char *cmd, char *cargs[], int ncargs)
{
    dbref thing, aowner;
    int howlong, num, attr, aflags;
    char *what;
    ATTR *ap;
    

    /*
     * If arg1 is all numeric, do simple (non-sem) timed wait. 
     */

    if (is_number(event))
    {
        howlong = Tiny_atol(event);
        wait_que(player, cause, howlong, NOTHING, 0, cmd, cargs, ncargs, mudstate.global_regs);
        return;
    }
    /*
     * Semaphore wait with optional timeout 
     */

    what = parse_to(&event, '/', 0);
    init_match(player, what, NOTYPE);
    match_everything(0);

    thing = noisy_match_result();
    if (!Good_obj(thing))
    {
        notify(player, "No match.");
    }
    else if (!controls(player, thing) && !Link_ok(thing))
    {
        notify(player, NOPERM_MESSAGE);
    }
    else 
    {

        /*
         * Get timeout, default 0 
         */

        if (event && *event && is_number(event))
        {
            attr = A_SEMAPHORE;
            howlong = Tiny_atol(event);
        }
        else
        {
            attr = A_SEMAPHORE;
            howlong = 0;
        }

        if (event && *event && !is_number(event))
        {
            ap = atr_str(event);
            if (!ap)
            {
                attr = mkattr(event);
                if (attr <= 0) {
                    notify_quiet(player, "Invalid attribute.");
                    return;
                }
                ap = atr_num(attr);
            }
            atr_pget_info(thing, ap->number, &aowner, &aflags);
            if (attr && Set_attr(player, thing, ap, aflags))
            {
                attr = ap->number;
                howlong = 0;
            }
            else
            {
                notify_quiet(player, NOPERM_MESSAGE);
                return;
            }
        }
        
        num = add_to(thing, 1, attr);
        if (num <= 0)
        {

            /*
             * thing over-notified, run the command immediately 
             */

            thing = NOTHING;
            howlong = 0;
        }
        wait_que(player, cause, howlong, thing, attr, cmd, cargs, ncargs, mudstate.global_regs);
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
#endif // MEMORY_BASED
    else if (p->fpTask == Task_ProcessCommand)
    {
        notify(Show_Player, tprintf("[%d]Further command quota", ltd.ReturnSeconds()));
    }
#ifdef WIN32
    else if (p->fpTask == Task_FreeDescriptor)
    {
        notify(Show_Player, tprintf("[%d]Delayed descriptor deallocation", ltd.ReturnSeconds()));
    }
#endif // WIN32
    else
    {
        Total_SystemTasks--;
    }
    return IU_NEXT_TASK;
}

void ShowPsLine(BQUE *tmp)
{
    char *bufp = unparse_object(Show_Player, tmp->player, 0);
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
                safe_str((char *)"; Arg", bufp, &bp);
                safe_chr((char)(i + '0'), bufp, &bp);
                safe_str((char *)"='", bufp, &bp);
                safe_str(tmp->env[i], bufp, &bp);
                safe_chr('\'', bufp, &bp);
            }
        }
        *bp = '\0';
        bp = unparse_object(Show_Player, tmp->cause, 0);
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
            Show_bFirstLine = FALSE;
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
            Show_bFirstLine = FALSE;
        }
        ShowPsLine(tmp);
    }
    return IU_NEXT_TASK;
}

/*
 * ---------------------------------------------------------------------------
 * * do_ps: tell player what commands they have pending in the queue
 */
void do_ps(dbref player, dbref cause, int key, char *target)
{
    char *bufp;
    dbref player_targ, obj_targ;

    // Figure out what to list the queue for.
    //
    if ((key & PS_ALL) && !(See_Queue(player))) 
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (!target || !*target)
    {
        obj_targ = NOTHING;
        if (key & PS_ALL)
        {
            player_targ = NOTHING;
        }
        else
        {
            player_targ = Owner(player);
            if (Typeof(player) != TYPE_PLAYER)
                obj_targ = player;
        }
    }
    else
    {
        player_targ = Owner(player);
        obj_targ = match_controlled(player, target);
        if (obj_targ == NOTHING)
        {
            return;
        }
        if (key & PS_ALL)
        {
            notify(player, "Can't specify a target and /all");
            return;
        }
        if (Typeof(obj_targ) == TYPE_PLAYER)
        {
            player_targ = obj_targ;
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
        notify(player, "Illegal combination of switches.");
        return;
    }

    Show_lsaNow.GetUTC();
    Total_SystemTasks = 0;
    Total_RunQueueEntry = 0;
    Shown_RunQueueEntry = 0;
    Total_SemaphoreTimeout = 0;
    Shown_SemaphoreTimeout = 0;
    Show_Player_Target = player_targ;
    Show_Object_Target = obj_targ;
    Show_Key = key;
    Show_Player = player;
    Show_bFirstLine = TRUE;
    scheduler.TraverseOrdered(CallBack_ShowWait);
    Show_bFirstLine = TRUE;
    scheduler.TraverseOrdered(CallBack_ShowSemaphore);
    if (Wizard(player))
    {
        notify(player, "----- System Queue -----");
        scheduler.TraverseOrdered(CallBack_ShowDispatches);
    }

    // Display stats.
    //
    bufp = alloc_mbuf("do_ps");
    sprintf(bufp, "Totals: Wait Queue...%d/%d  Semaphores...%d/%d",
        Shown_RunQueueEntry, Total_RunQueueEntry,
        Shown_SemaphoreTimeout, Total_SemaphoreTimeout);
    notify(player, bufp);
    if (Wizard(player))
    {
        sprintf(bufp, "        System Tasks.....%d", Total_SystemTasks);
        notify(player, bufp);
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

/*
 * ---------------------------------------------------------------------------
 * * do_queue: Queue management
 */

void do_queue(dbref player, dbref cause, int key, char *arg)
{
    int was_disabled;

    was_disabled = 0;
    if (key == QUEUE_KICK)
    {
        int i = Tiny_atol(arg);
        int save_minPriority = scheduler.GetMinPriority();
        if (save_minPriority <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(player, "Warning: automatic dequeueing is disabled.");
            scheduler.SetMinPriority(PRIORITY_CF_DEQUEUE_ENABLED);
        }
        CLinearTimeAbsolute lsaNow;
        lsaNow.GetUTC();
        scheduler.ReadyTasks(lsaNow);
        int ncmds = scheduler.RunTasks(i);
        scheduler.SetMinPriority(save_minPriority);

        if (!Quiet(player))
        {
            notify(player, tprintf("%d commands processed.", ncmds));
        }
    }
    else if (key == QUEUE_WARP)
    {
        int iWarp = Tiny_atol(arg);
        CLinearTimeDelta ltdWarp;
        ltdWarp.SetSeconds(iWarp);
        if (scheduler.GetMinPriority() <= PRIORITY_CF_DEQUEUE_DISABLED)
        {
            notify(player, "Warning: automatic dequeueing is disabled.");
        }

        scheduler.TraverseUnordered(CallBack_Warp);

        if (Quiet(player))
        {
            return;
        }
        if (iWarp > 0)
        {
            notify(player, tprintf("WaitQ timer advanced %d seconds.", iWarp));
        }
        else if (iWarp < 0)
        {
            notify(player, tprintf("WaitQ timer set back %d seconds.", iWarp));
        }
        else
        {
            notify(player, "Object queue appended to player queue.");
        }
    }
}
