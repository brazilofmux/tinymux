/*
 * Copyright (C) 1991, Marcus J. Ranum. All rights reserved.
 * 
 * $Id: udb_ochunk.c,v 1.9 1998/06/09 22:35:33 dpassmor Exp $
 */

/* configure all options BEFORE including system stuff. */
#include	"autoconf.h"
#include	"udb_defs.h"
#include	"db.h"
#include	"mudconf.h"

#ifdef VMS
#include	<malloc.h>
#include        <types.h>
#include        <file.h>
#include        <unixio.h>
#include        "vms_dbm.h"
#else
#ifndef NEXT
#ifndef MALLOC_IN_STDLIB_H
#include	<malloc.h>
#endif
#endif /* NEXT */
#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/file.h>

#include	"gdbm.h"
#endif /* VMS */

#include	"udb.h"
#include	"config.h"
#include	"externs.h"
#include	"alloc.h"

#ifndef STANDALONE
extern void FDECL(dump_database_internal, (int));
#endif

#define DEFAULT_DBMCHUNKFILE "mudDB"

static char *dbfile = DEFAULT_DBMCHUNKFILE;
static int db_initted = 0;

static GDBM_FILE dbp = (GDBM_FILE) 0;

static datum dat;
static datum key;
static datum nextkey;

/* gdbm_reorganize compresses unused space in the db */

int dddb_optimize()
{
	return (gdbm_reorganize(dbp));
}

static void gdbm_panic(mesg)
char *mesg;
{
	log_text(tprintf("GDBM panic: %s\n", mesg));
}

int dddb_init()
{
	static char *copen = "db_init cannot open ";
	char fnam[MAXPATHLEN];
	char *gdbm_error;
	struct stat sbuf;
	int fxp, block_size;

	for (block_size = 1; block_size < LBUF_SIZE; block_size = block_size << 1) ;

	if ((dbp = gdbm_open(dbfile, block_size, GDBM_WRCREAT, 0600, gdbm_panic)) == (GDBM_FILE) 0) {
		gdbm_error = (char *)gdbm_strerror(gdbm_errno);
		logf(copen, dbfile, " ", (char *)-1, "\n", gdbm_error, "\n", (char *)0);
		return (1);
	}

	db_initted = 1;
	return (0);
}

dddb_setfile(fil)
char *fil;
{
	char *xp;

	if (db_initted)
		return (1);

	/* KNOWN memory leak. can't help it. it's small */
	xp = (char *)malloc((unsigned)strlen(fil) + 1);
	if (xp == (char *)0)
		return (1);
	(void)strcpy(xp, fil);
	dbfile = xp;
	return (0);
}

int dddb_close()
{
	if (dbp != (GDBM_FILE) 0) {
		gdbm_close(dbp);
		dbp = (GDBM_FILE) 0;
	}
	db_initted = 0;
	return (0);
}

Obj *dddb_get(nam)
Objname *nam;
{
	Obj *ret;
	char *bp;
	int i;
	
	if (!db_initted)
		return ((Obj *) 0);

	key.dptr = (char *)nam;
	key.dsize = sizeof(Objname);
	dat = gdbm_fetch(dbp, key);

	if (dat.dptr == (char *)0) {
		return ((Obj *) 0);
	}
	
	/* if the file is badly formatted, ret == Obj * 0 */
	if ((ret = objfromFILE(dat.dptr)) == (Obj *) 0) {
		logf("db_get: cannot decode ", nam, "\n", (char *)0);
		free(dat.dptr);
		return NULL;
	}
	
#ifndef STANDALONE	
	/* Check to make sure the requested name and stored name are the
	   same */
	
	if (*nam != ret->name) {
		STARTLOG(LOG_ALWAYS, "BUG", "CRUPT")
			log_text((char *)tprintf("Database is corrupt, object %d. Exiting.", (int)ret->name));
		ENDLOG
		raw_broadcast(0, "Game: Database corruption detected, exiting.");
		exit(8);
	}
#endif
	free(dat.dptr);	
	return (ret);
}

int dddb_put(obj, nam)
Obj *obj;
Objname *nam;
{
	int nsiz;

	if (!db_initted)
		return (1);

	nsiz = obj_siz(obj);

	key.dptr = (char *)nam;
	key.dsize = sizeof(Objname);
	
	/* make table entry */
	dat.dptr = (char *)malloc(nsiz);
	dat.dsize = nsiz;

	if (objtoFILE(obj, dat.dptr) != 0) {
		logf("db_put: can't save ", nam, " ", (char *)-1, "\n", (char *)0);
		free(dat.dptr);
		return (1);
	}

	if (gdbm_store(dbp, key, dat, GDBM_REPLACE)) {
		logf("db_put: can't gdbm_store ", nam, " ", (char *)-1, "\n", (char *)0);
		free(dat.dptr);
		return (1);
	}

	free(dat.dptr);
	return (0);
}

int dddb_del(nam)
Objname *nam;
{

	if (!db_initted)
		return (-1);

	key.dptr = (char *)nam;
	key.dsize = sizeof(Objname);
	dat = gdbm_fetch(dbp, key);

	/* not there? */
	if (dat.dptr == (char *)0)
		return (0);

	free(dat.dptr);
	
	/* drop key from db */
	if (gdbm_delete(dbp, key)) {
		logf("db_del: can't delete key ", nam, "\n", (char *)0);
		return (1);
	}
	return (0);
}
