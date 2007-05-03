
/*
 * A tool to open and traverse the dbm database that goes with
 * your game, and dump out a raw report of where every object lives in
 * the chunkfile, and how many bytes it takes up.
 */
#include "autoconf.h"
#include "config.h"

#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>

#include "gdbm.h"

/*
 * This struct should match the one in udb_ochunk.c 
 */

struct hrec {
	off_t off;		/*
				 * Where it lives in the chunkfile 
				 */
	int siz;		/*
				 * How long it really is, in bytes 
				 */
	unsigned int blox;	/*
				 * How manu blocks it owns now     
				 */
};

static GDBM_FILE dbp = (GDBM_FILE) 0;

static void gdbm_panic(mesg)
{
	fprintf(stderr, "GDBM panic: %s\n", mesg);
}

int main(ac, av)
int ac;
char *av[];
{
	int obj;
	datum key, dat;
	struct hrec hbuf;

	if (ac != 2) {
		fprintf(stderr, "usage: %s <database name>\n", av[0]);
		exit(0);
	}
	/*
	 * open hash table 
	 */
	if ((dbp = gdbm_open(av[1], 4096, GDBM_WRCREAT, 0600, gdbm_panic)) == (GDBM_FILE) 0) {
		fprintf(stderr, "Can't open hdbm database %s\n", av[1]);
		exit(0);
	}
	key = gdbm_firstkey(dbp);
	while (key.dptr != (char *)NULL) {
		dat = gdbm_fetch(dbp, key);
		if (dat.dptr == (char *)NULL) {
			fprintf(stderr, "gdbm database %s inconsistent\n", av[1]);
			exit(0);
		}
		bcopy(dat.dptr, (char *)&hbuf, sizeof(hbuf));	/*
								 * alignment 
								 */
		bcopy(key.dptr, (char *)&obj, sizeof(obj));

		printf("Object %d resides at offset %d and takes %d bytes\n",
		       obj, hbuf.off, hbuf.siz);

		key = gdbm_nextkey(dbp, key);
	}

	exit(1);
}
