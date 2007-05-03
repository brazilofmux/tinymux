/*
 * alloc.c - memory allocation subsystem 
 */
/*
 * $Id: alloc.c,v 1.2 1997/04/16 06:00:34 dpassmor Exp $ 
 */
#include "copyright.h"
#include "autoconf.h"

#include "db.h"
#include "alloc.h"
#include "mudconf.h"
#include "externs.h"

typedef struct pool_header {
	int magicnum;		/*
				 * For consistency check 
				 */
	int pool_size;		/*
				 * For consistency check 
				 */
	struct pool_header *next;	/*
					 * Next pool header in chain 
					 */
	struct pool_header *nxtfree;	/*
					 * Next pool header in freelist 
					 */
	char *buf_tag;		/*
				 * Debugging/trace tag 
				 */
} POOLHDR;

typedef struct pool_footer {
	int magicnum;		/*
				 * For consistency check 
				 */
} POOLFTR;

typedef struct pooldata {
	int pool_size;		/*
				 * Size in bytes of a buffer 
				 */
	POOLHDR *free_head;	/*
				 * Buffer freelist head 
				 */
	POOLHDR *chain_head;	/*
				 * Buffer chain head 
				 */
	int tot_alloc;		/*
				 * Total buffers allocated 
				 */
	int num_alloc;		/*
				 * Number of buffers currently allocated 
				 */
	int max_alloc;		/*
				 * Max # buffers allocated at one time 
				 */
	int num_lost;		/*
				 * Buffers lost due to corruption 
				 */
} POOL;

POOL pools[NUM_POOLS];
const char *poolnames[] =
{"Sbufs", "Mbufs", "Lbufs", "Bools", "Descs", "Qentries", "Pcaches"};

#define POOL_MAGICNUM 0xdeadbeef

void pool_init(poolnum, poolsize)
int poolnum, poolsize;
{
	pools[poolnum].pool_size = poolsize;
	pools[poolnum].free_head = NULL;
	pools[poolnum].chain_head = NULL;
	pools[poolnum].tot_alloc = 0;
	pools[poolnum].num_alloc = 0;
	pools[poolnum].max_alloc = 0;
	pools[poolnum].num_lost = 0;
	return;
}

static void pool_err(logsys, logflag, poolnum, tag, ph, action, reason)
int logflag, poolnum;
const char *logsys, *tag, *action, *reason;
POOLHDR *ph;
{
	if (!mudstate.logging) {
		STARTLOG(logflag, logsys, "ALLOC")
			sprintf(mudstate.buffer,
				"%s[%d] (tag %s) %s at %lx. (%s)",
			      action, pools[poolnum].pool_size, tag, reason,
				(long)ph, mudstate.debug_cmd);
		log_text(mudstate.buffer);
		ENDLOG
	} else if (logflag != LOG_ALLOCATE) {
		sprintf(mudstate.buffer,
			"\n***< %s[%d] (tag %s) %s at %lx. >***",
			action, pools[poolnum].pool_size, tag, reason,
			(long)ph);
		log_text(mudstate.buffer);
	}
}

static void pool_vfy(poolnum, tag)
int poolnum;
const char *tag;
{
	POOLHDR *ph, *lastph;
	POOLFTR *pf;
	char *h;
	int psize;

	lastph = NULL;
	psize = pools[poolnum].pool_size;
	for (ph = pools[poolnum].chain_head; ph; lastph = ph, ph = ph->next) {
		h = (char *)ph;
		h += sizeof(POOLHDR);
		h += pools[poolnum].pool_size;
		pf = (POOLFTR *) h;

		if (ph->magicnum != POOL_MAGICNUM) {
			pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
			  "Verify", "header corrupted (clearing freelist)");

			/*
			 * Break the header chain at this point so we don't * 
			 * 
			 * *  * * generate an error for EVERY alloc and free, 
			 * * * also  * we can't continue the scan because the
			 * * next * * pointer might be trash too. 
			 */

			if (lastph)
				lastph->next = NULL;
			else
				pools[poolnum].chain_head = NULL;
			return;	/*
				 * not safe to continue 
				 */
		}
		if (pf->magicnum != POOL_MAGICNUM) {
			pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
				 "Verify", "footer corrupted");
			pf->magicnum = POOL_MAGICNUM;
		}
		if (ph->pool_size != psize) {
			pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
				 "Verify", "header has incorrect size");
		}
	}
}

void pool_check(tag)
const char *tag;
{
	pool_vfy(POOL_LBUF, tag);
	pool_vfy(POOL_MBUF, tag);
	pool_vfy(POOL_SBUF, tag);
	pool_vfy(POOL_BOOL, tag);
	pool_vfy(POOL_DESC, tag);
	pool_vfy(POOL_QENTRY, tag);
}

char *pool_alloc(poolnum, tag)
int poolnum;
const char *tag;
{
	int *p;
	char *h;
	POOLHDR *ph;
	POOLFTR *pf;

	if (mudconf.paranoid_alloc)
		pool_check(tag);
	do {
		if (pools[poolnum].free_head == NULL) {
			h = (char *)malloc(pools[poolnum].pool_size +
					 sizeof(POOLHDR) + sizeof(POOLFTR));
			if (h == NULL)
				abort();
			ph = (POOLHDR *) h;
			h += sizeof(POOLHDR);
			p = (int *)h;
			h += pools[poolnum].pool_size;
			pf = (POOLFTR *) h;
			ph->next = pools[poolnum].chain_head;
			ph->nxtfree = NULL;
			ph->magicnum = POOL_MAGICNUM;
			ph->pool_size = pools[poolnum].pool_size;
			pf->magicnum = POOL_MAGICNUM;
			*p = POOL_MAGICNUM;
			pools[poolnum].chain_head = ph;
			pools[poolnum].max_alloc++;
		} else {
			ph = (POOLHDR *) (pools[poolnum].free_head);
			h = (char *)ph;
			h += sizeof(POOLHDR);
			p = (int *)h;
			h += pools[poolnum].pool_size;
			pf = (POOLFTR *) h;
			pools[poolnum].free_head = ph->nxtfree;

			/*
			 * If corrupted header we need to throw away the * *
			 * * freelist as the freelist pointer may be corrupt. 
			 */

			if (ph->magicnum != POOL_MAGICNUM) {
				pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
					 "Alloc", "corrupted buffer header");

				/*
				 * Start a new free list and record stats 
				 */

				p = NULL;
				pools[poolnum].free_head = NULL;
				pools[poolnum].num_lost +=
					(pools[poolnum].tot_alloc -
					 pools[poolnum].num_alloc);
				pools[poolnum].tot_alloc =
					pools[poolnum].num_alloc;
			}
			/*
			 * Check for corrupted footer, just report and * fix
			 * * * it 
			 */

			if (pf->magicnum != POOL_MAGICNUM) {
				pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
					 "Alloc", "corrupted buffer footer");
				pf->magicnum = POOL_MAGICNUM;
			}
		}
	} while (p == NULL);

	ph->buf_tag = (char *)tag;
	pools[poolnum].tot_alloc++;
	pools[poolnum].num_alloc++;

	pool_err("DBG", LOG_ALLOCATE, poolnum, tag, ph, "Alloc", "buffer");

	/*
	 * If the buffer was modified after it was last freed, log it. 
	 */

	if ((*p != POOL_MAGICNUM) && (!mudstate.logging)) {
		pool_err("BUG", LOG_PROBLEMS, poolnum, tag, ph, "Alloc",
			 "buffer modified after free");
	}
	*p = 0;
	return (char *)p;
}

void pool_free(poolnum, buf)
int poolnum;
char **buf;
{
	int *ibuf;
	char *h;
	POOLHDR *ph;
	POOLFTR *pf;

	ibuf = (int *)*buf;
	h = (char *)ibuf;
	h -= sizeof(POOLHDR);
	ph = (POOLHDR *) h;
	h = (char *)ibuf;
	h += pools[poolnum].pool_size;
	pf = (POOLFTR *) h;
	if (mudconf.paranoid_alloc)
		pool_check(ph->buf_tag);

	/*
	 * Make sure the buffer header is good.  If it isn't, log the error * 
	 * 
	 * * and * throw away the buffer. 
	 */

	if (ph->magicnum != POOL_MAGICNUM) {
		pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
			 "corrupted buffer header");
		pools[poolnum].num_lost++;
		pools[poolnum].num_alloc--;
		pools[poolnum].tot_alloc--;
		return;
	}
	/*
	 * Verify the buffer footer.  Don't unlink if damaged, just repair 
	 */

	if (pf->magicnum != POOL_MAGICNUM) {
		pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
			 "corrupted buffer footer");
		pf->magicnum = POOL_MAGICNUM;
	}
	/*
	 * Verify that we are not trying to free someone else's buffer 
	 */

	if (ph->pool_size != pools[poolnum].pool_size) {
		pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
			 "Attempt to free into a different pool.");
		return;
	}
	pool_err("DBG", LOG_ALLOCATE, poolnum, ph->buf_tag, ph, "Free",
		 "buffer");

	/*
	 * Make sure we aren't freeing an already free buffer.  If we are, *
	 * * * log an error, otherwise update the pool header and stats  
	 */

	if (*ibuf == POOL_MAGICNUM) {
		pool_err("BUG", LOG_BUGS, poolnum, ph->buf_tag, ph, "Free",
			 "buffer already freed");
	} else {
		*ibuf = POOL_MAGICNUM;
		ph->nxtfree = pools[poolnum].free_head;
		pools[poolnum].free_head = ph;
		pools[poolnum].num_alloc--;
	}
}

static char *pool_stats(poolnum, text)
int poolnum;
const char *text;
{
	char *buf;

	buf = alloc_mbuf("pool_stats");
	sprintf(buf, "%-15s %5d%9d%9d%9d%9d", text, pools[poolnum].pool_size,
		pools[poolnum].num_alloc, pools[poolnum].max_alloc,
		pools[poolnum].tot_alloc, pools[poolnum].num_lost);
	return buf;
}

static void pool_trace(player, poolnum, text)
dbref player;
int poolnum;
const char *text;
{
	POOLHDR *ph;
	int numfree, *ibuf;
	char *h;

	numfree = 0;
	notify(player, tprintf("----- %s -----", text));
	for (ph = pools[poolnum].chain_head; ph != NULL; ph = ph->next) {
		if (ph->magicnum != POOL_MAGICNUM) {
			notify(player, "*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***");
			notify(player,
			       tprintf("%d free %s (before corruption)",
				       numfree, text));
			return;
		}
		h = (char *)ph;
		h += sizeof(POOLHDR);
		ibuf = (int *)h;
		if (*ibuf != POOL_MAGICNUM)
			notify(player, ph->buf_tag);
		else
			numfree++;
	}
	notify(player, tprintf("%d free %s", numfree, text));
}

static void list_bufstat(player, poolnum, pool_name)
dbref player;
int poolnum;
const char *pool_name;
{
	char *buff;

	buff = pool_stats(poolnum, poolnames[poolnum]);
	notify(player, buff);
	free_mbuf(buff);
}

void list_bufstats(player)
dbref player;
{
	int i;

	notify(player, "Buffer Stats     Size    InUse    Total   Allocs     Lost");
	for (i = 0; i < NUM_POOLS; i++)
		list_bufstat(player, i, poolnames[i]);
}

void list_buftrace(player)
dbref player;
{
	int i;

	for (i = 0; i < NUM_POOLS; i++)
		pool_trace(player, i, poolnames[i]);
}

void pool_reset()
{
	POOLHDR *ph, *phnext, *newchain;
	int i, *ibuf;
	char *h;

	
	for (i = 0; i < NUM_POOLS; i++) {
		newchain = NULL;
		for (ph = pools[i].chain_head; ph != NULL; ph = phnext) {
			h = (char *)ph;
			phnext = ph->next;
			h += sizeof(POOLHDR);
			ibuf = (int *)h;
			if (*ibuf == POOL_MAGICNUM) {
				free(ph);
			} else {
				if (!newchain) {
					newchain = ph;
					ph->next = NULL;
				} else {
					ph->next = newchain;
					newchain = ph;
				}
				ph->nxtfree = NULL;
			}
		}
		pools[i].chain_head = newchain;
		pools[i].free_head = NULL;
		pools[i].max_alloc = pools[i].num_alloc;
	}
}	
 