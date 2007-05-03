/*
 * timer.c -- Subroutines for (system-) timed events 
 */
/*
 * $Id: timer.c,v 1.2 1997/04/16 06:01:57 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include <signal.h>

#include "mudconf.h"
#include "config.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "externs.h"
#include "command.h"

extern void NDECL(pool_reset);
extern void NDECL(do_second);
extern void FDECL(fork_and_dump, (int key));
extern unsigned int FDECL(alarm, (unsigned int seconds));
extern void NDECL(pcache_trim);

void NDECL(init_timer)
{
	mudstate.now = time(NULL);
	mudstate.dump_counter = ((mudconf.dump_offset == 0) ?
		mudconf.dump_interval : mudconf.dump_offset) + mudstate.now;
	mudstate.check_counter = ((mudconf.check_offset == 0) ?
	      mudconf.check_interval : mudconf.check_offset) + mudstate.now;
	mudstate.idle_counter = mudconf.idle_interval + mudstate.now;
	mudstate.mstats_counter = 15 + mudstate.now;
	mudstate.events_counter = 900 + mudstate.now;
	alarm(1);
}

void NDECL(dispatch)
{
	char *cmdsave;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< dispatch >";

	/*
	 * this routine can be used to poll from interface.c 
	 */

	if (!mudstate.alarm_triggered)
		return;
	mudstate.alarm_triggered = 0;
	mudstate.now = time(NULL);

	do_second();

	/*
	 * Free list reconstruction 
	 */

	if ((mudconf.control_flags & CF_DBCHECK) &&
	    (mudstate.check_counter <= mudstate.now)) {
		mudstate.check_counter = mudconf.check_interval + mudstate.now;
		mudstate.debug_cmd = (char *)"< dbck >";
#ifndef MEMORY_BASED
		cache_reset(0);
#endif /*
        * MEMORY_BASED 
        */
		do_dbck(NOTHING, NOTHING, 0);
#ifndef MEMORY_BASED
		cache_reset(0);
#endif /*
        * MEMORY_BASED 
        */
		pcache_trim();
		pool_reset();
	}
	/*
	 * Database dump routines 
	 */

	if ((mudconf.control_flags & CF_CHECKPOINT) &&
	    (mudstate.dump_counter <= mudstate.now)) {
		mudstate.dump_counter = mudconf.dump_interval + mudstate.now;
		mudstate.debug_cmd = (char *)"< dump >";
		fork_and_dump(0);
	}
	/*
	 * Idle user check 
	 */

	if ((mudconf.control_flags & CF_IDLECHECK) &&
	    (mudstate.idle_counter <= mudstate.now)) {
		mudstate.idle_counter = mudconf.idle_interval + mudstate.now;
		mudstate.debug_cmd = (char *)"< idlecheck >";
#ifndef MEMORY_BASED
		cache_reset(0);
#endif /*
        * MEMORY_BASED 
        */
		check_idle();

	}
	/*
	 * Check for execution of attribute events 
	 */

	if ((mudconf.control_flags & CF_EVENTCHECK) &&
	    (mudstate.events_counter <= mudstate.now)) {
		mudstate.events_counter = 900 + mudstate.now;
		mudstate.debug_cmd = (char *)"< eventcheck >";
		check_events();
	}
#ifdef HAVE_GETRUSAGE
	/*
	 * Memory use stats 
	 */

	if (mudstate.mstats_counter <= mudstate.now) {

		int curr;

		mudstate.mstats_counter = 15 + mudstate.now;
		curr = mudstate.mstat_curr;
		if (mudstate.now > mudstate.mstat_secs[curr]) {

			struct rusage usage;

			curr = 1 - curr;
			getrusage(RUSAGE_SELF, &usage);
			mudstate.mstat_ixrss[curr] = usage.ru_ixrss;
			mudstate.mstat_idrss[curr] = usage.ru_idrss;
			mudstate.mstat_isrss[curr] = usage.ru_isrss;
			mudstate.mstat_secs[curr] = mudstate.now;
			mudstate.mstat_curr = curr;
		}
	}
#endif

	/*
	 * reset alarm 
	 */

	alarm(1);
	mudstate.debug_cmd = cmdsave;
}

/*
 * ---------------------------------------------------------------------------
 * * do_timewarp: Adjust various internal timers.
 */

void do_timewarp(player, cause, key, arg)
dbref player, cause;
int key;
char *arg;
{
	int secs;

	secs = atoi(arg);

	if ((key == 0) || (key & TWARP_QUEUE))	/*
						 * Sem/Wait queues 
						 */
		do_queue(player, cause, QUEUE_WARP, arg);
	if (key & TWARP_DUMP)
		mudstate.dump_counter -= secs;
	if (key & TWARP_CLEAN)
		mudstate.check_counter -= secs;
	if (key & TWARP_IDLE)
		mudstate.idle_counter -= secs;
	if (key & TWARP_EVENTS)
		mudstate.events_counter -= secs;
}
