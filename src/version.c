/*
 * version.c - version information 
 */
/*
 * $Id: version.c,v 1.2 1997/04/16 06:02:06 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "db.h"
#include "mudconf.h"
#include "alloc.h"
#include "externs.h"
#include "patchlevel.h"

/*
 * 1.0.0 TinyMUX 
 */

/*
 * 2.0
 * All known bugs fixed with disk-based.  Played with gdbm, it
 * sucked.  Now using bsd 4.4 hash stuff.
 */

/*
 * 1.12
 * * All known bugs fixed after several days of debugging 1.10/1.11.
 * * Much string-handling braindeath patched, but needs a big overhaul,
 * * really.   GAC 2/10/91
 */

/*
 * 1.11
 * * Fixes for 1.10.  (@name didn't call do_name, etc.)
 * * Added dexamine (debugging examine, dumps the struct, lots of info.)
 */

/*
 * 1.10
 * * Finally got db2newdb working well enough to run from the big (30000
 * * object) db with ATR_KEY and ATR_NAME defined.   GAC 2/3/91
 */

/*
 * TinyCWRU version.c file.  Add a comment here any time you've made a
 * * big enough revision to increment the TinyCWRU version #.
 */

void do_version(player, cause, extra)
dbref player, cause;
int extra;
{
	char *buff;

	notify(player, mudstate.version);
	buff = alloc_mbuf("do_version");
	sprintf(buff, "Build date: %s", MUX_BUILD_DATE);
	notify(player, buff);
	free_mbuf(buff);
}

void NDECL(init_version)
{
#ifdef BETA
#if PATCHLEVEL > 0
	sprintf(mudstate.version, "TinyMUX Beta version %s patchlevel %d #%s",
		MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM);
#else
	sprintf(mudstate.version, "TinyMUX Beta version %s #%s",
		MUX_VERSION, MUX_BUILD_NUM);
#endif /*
        * PATCHLEVEL 
        */
#else /*
       * not BETA 
       */
#if PATCHLEVEL > 0
	sprintf(mudstate.version, "TinyMUX version %s patchlevel %d #%s [%s]",
		MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#else
	sprintf(mudstate.version, "TinyMUX version %s #%s [%s]",
		MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif /*
        * PATCHLEVEL 
        */
#endif /*
        * BETA 
        */
	STARTLOG(LOG_ALWAYS, "INI", "START")
		log_text((char *)"Starting: ");
	log_text(mudstate.version);
	ENDLOG
		STARTLOG(LOG_ALWAYS, "INI", "START")
		log_text((char *)"Build date: ");
	log_text((char *)MUX_BUILD_DATE);
	ENDLOG
}
