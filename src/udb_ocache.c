/*
 * Copyright (C) 1991, Marcus J. Ranum. All rights reserved.
 * Slightly whacked by Andrew Molitor. My apologies to the author..
 * 
 * Also banged on by dcm@somewhere.or.other, Jellan on the muds,
 * to be more complex and more efficient.
 * 
 * This version then banged on by Andrew Molitor, 1992, to
 * do object-by-object, hiding the object part inside
 * the cache layer. Upper layers think it's an attribute cache.
 * 
 * $Id: udb_ocache.c,v 1.3 1997/04/16 15:45:37 dpassmor Exp $
 */

/*
 * #define CACHE_DEBUG
 * #define CACHE_VERBOSE
 */
/*
 * First 
 */
#include	"autoconf.h"
#include	"config.h"
#include	"externs.h"
#include	"udb_defs.h"
#include	"mudconf.h"

#ifdef	NOSYSTYPES_H
#include	<types.h>
#else
#include	<sys/types.h>
#endif

extern struct Obj *dddb_get();
extern void logf();

/*
 * This is by far the most complex and kinky code in UnterMUD. You should
 * never need to mess with anything in here - if you value your sanity.
 */


typedef struct cache {
	Obj *op;
	struct cache *nxt;
	struct cache *prv;
} Cache;

typedef struct {
	Cache *head;
	Cache *tail;
} Chain;

typedef struct {
	Chain active;
	Chain mactive;
	Chain old;
	Chain mold;
	int count;
} CacheLst;

/*
 * This is a MUX specific definition, depending on Objname being an
 * * integer type of some sort. 
 */

#define NAMECMP(a,b) ((a)->op) && (((a)->op)->name == (b)->object)

#define DEQUEUE(q, e)	if(e->nxt == (Cache *)0) { \
				if (e->prv != (Cache *)0) { \
					e->prv->nxt = (Cache *)0; \
				} \
				q.tail = e->prv; \
			} else { \
				e->nxt->prv = e->prv; \
			} \
			if(e->prv == (Cache *)0) { \
				if (e->nxt != (Cache *)0) { \
					e->nxt->prv = (Cache *)0; \
				} \
				q.head = e->nxt; \
			} else \
				e->prv->nxt = e->nxt;

#define INSHEAD(q, e)	if (q.head == (Cache *)0) { \
				q.tail = e; \
				e->nxt = (Cache *)0; \
			} else { \
				q.head->prv = e; \
				e->nxt = q.head; \
			} \
			q.head = e; \
			e->prv = (Cache *)0;

#define INSTAIL(q, e)	if (q.head == (Cache *)0) { \
				q.head = e; \
				e->prv = (Cache *)0; \
			} else { \
				q.tail->nxt = e; \
				e->prv = q.tail; \
			} \
			q.tail = e; \
			e->nxt = (Cache *)0;

static Cache *get_free_entry();
static int cache_write();
static void cache_clean();

static Attr *get_attrib();
static void set_attrib();
static void del_attrib();
static void FDECL(objfree, (Obj *));

/*
 * initial settings for cache sizes 
 */
static int cwidth = CACHE_WIDTH;
static int cdepth = CACHE_DEPTH;


/*
 * ntbfw - main cache pointer and list of things to kill off 
 */
static CacheLst *sys_c;

static int cache_initted = 0;
static int cache_frozen = 0;
static int cache_busy = 0;

/*
 * cache stats gathering stuff. you don't like it? comment it out 
 */
time_t cs_ltime;
int cs_writes = 0;		/*

				 * total writes 
				 */
int cs_reads = 0;		/*

				 * total reads 
				 */
int cs_dbreads = 0;		/*

				 * total read-throughs 
				 */
int cs_dbwrites = 0;		/*

				 * total write-throughs 
				 */
int cs_dels = 0;		/*

				 * total deletes 
				 */
int cs_checks = 0;		/*

				 * total checks 
				 */
int cs_rhits = 0;		/*

				 * total reads filled from cache 
				 */
int cs_ahits = 0;		/*

				 * total reads filled active cache 
				 */
int cs_whits = 0;		/*

				 * total writes to dirty cache 
				 */
int cs_fails = 0;		/*

				 * attempts to grab nonexistent 
				 */
int cs_resets = 0;		/*

				 * total cache resets 
				 */
int cs_syncs = 0;		/*

				 * total cache syncs 
				 */
int cs_objects = 0;		/*

				 * total cache size 
				 */


void cache_repl(cp, new)
Cache *cp;
Obj *new;
{
	if (cp->op != ONULL)
		objfree(cp->op);
	cp->op = new;
}

int cache_init(width, depth)
int width, depth;
{
	int x;
	int y;
	Cache *np, *cp;
	CacheLst *sp;
	static char *ncmsg = "cache_init: cannot allocate cache: ";

	if (cache_initted || sys_c != (CacheLst *) 0)
		return (0);

	/*
	 * If either dimension is specified as non-zero, change it to 
	 */
	/*
	 * that, otherwise use default. Treat dimensions deparately.  
	 */

	if (width)
		cwidth = width;
	if (depth)
		cdepth = depth;

	sp = sys_c = (CacheLst *) malloc((unsigned)cwidth * sizeof(CacheLst));
	if (sys_c == (CacheLst *) 0) {
		logf(ncmsg, (char *)-1, "\n", (char *)0);
		return (-1);
	}
	/*
	 * Pre-allocate the initial cache entries in one chunk 
	 */
	cp = (Cache *) malloc(cwidth * cdepth * sizeof(Cache));
	if (cp == (Cache *) 0) {
		logf(ncmsg, (char *)-1, "\n", (char *)0);
		return (-1);
	}
	for (x = 0; x < cwidth; x++, sp++) {
		sp->active.head = sp->old.head = (Cache *) 0;
		sp->old.tail = (Cache *) 0;
		sp->mactive.head = sp->mold.head = (Cache *) 0;
		sp->mactive.tail = sp->mold.tail = (Cache *) 0;
		sp->count = cdepth;

		for (y = 0; y < cdepth; y++) {

			np = cp++;
			if ((np->nxt = sp->old.head) != (Cache *) 0)
				np->nxt->prv = np;
			else
				sp->old.tail = np;

			np->prv = (Cache *) 0;
			np->op = (Obj *) 0;

			sp->old.head = np;
			cs_objects++;
		}
	}

	/*
	 * mark caching system live 
	 */
	cache_initted++;

	time(&cs_ltime);

	return (0);
}



static void cache_trim(sp)
CacheLst *sp;
{
	Cache *cp;

	while ((sp->count > cdepth) && ((cp = sp->old.tail) != CNULL)) {
		/*
		 * unlink old cache chain tail 
		 */
		if (cp->prv != CNULL) {
			sp->old.tail = cp->prv;
			cp->prv->nxt = cp->nxt;
		} else		/*
				 * took last one 
				 */
			sp->old.head = sp->old.tail = CNULL;

		/*
		 * free object's data 
		 */

		cache_repl(cp, ONULL);
		free(cp);
		sp->count--;
		cs_objects--;
	}
}

void cache_reset(trim)
int trim;
{
	int x;
	CacheLst *sp;

	if (!cache_initted)
		return;

	/*
	 * unchain and rechain each active list at head of old chain 
	 */
	sp = sys_c;
	for (x = 0; x < cwidth; x++, sp++) {
		if (sp->active.head != CNULL) {
			sp->active.tail->nxt = sp->old.head;

			if (sp->old.head != CNULL)
				sp->old.head->prv = sp->active.tail;
			if (sp->old.tail == CNULL)
				sp->old.tail = sp->active.tail;

			sp->old.head = sp->active.head;
			sp->active.head = CNULL;
		}
		if (!cache_busy && sp->mactive.head != CNULL) {
			sp->mactive.tail->nxt = sp->mold.head;

			if (sp->mold.head != CNULL)
				sp->mold.head->prv = sp->mactive.tail;
			if (sp->mold.tail == CNULL)
				sp->mold.tail = sp->mactive.tail;

			sp->mold.head = sp->mactive.head;
			sp->mactive.head = CNULL;
		}
		if (trim && mudconf.cache_trim)
			cache_trim(sp);
	}
	cs_resets++;
}




/*
 * search the cache for an object, and if it is not found, thaw it.
 * this code is probably a little bigger than it needs be because I
 * had fun and unrolled all the pointer juggling inline.
 */
Attr *
 cache_get(nam)
Aname *nam;
{
	Cache *cp;
	CacheLst *sp;
	int hv = 0;
	Obj *ret;

	/*
	 * firewall 
	 */
	if (nam == (Aname *) 0 || !cache_initted) {
#ifdef	CACHE_VERBOSE
		logf("cache_get: NULL object name - programmer error\n", (char *)0);
#endif
		return ((Attr *) 0);
	}
#ifdef	CACHE_DEBUG
	printf("get %d/%d\n", nam->object, nam->attrnum);
#endif

	cs_reads++;

	/*
	 * We search the cache for the right Obj, then find the Attrib inside
	 * * * * that. 
	 */

	hv = nam->object % cwidth;
	sp = &sys_c[hv];

	/*
	 * search active chain first 
	 */
	for (cp = sp->active.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			cs_rhits++;
			cs_ahits++;
#ifdef	CACHE_DEBUG
			printf("return active cache -- %d\n", cp->op);
#endif
			/*
			 * Found the Obj, return the Attr within it 
			 */

			return (get_attrib(nam, cp->op));
		}
	}

	/*
	 * search modified active chain next. 
	 */
	for (cp = sp->mactive.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			cs_rhits++;
			cs_ahits++;

#ifdef	CACHE_DEBUG
			printf("return modified active cache -- %d\n", cp->op);
#endif
			return (get_attrib(nam, cp->op));
		}
	}

	/*
	 * search in-active chain now 
	 */
	for (cp = sp->old.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * dechain from in-active chain 
			 */
			DEQUEUE(sp->old, cp);
			/*
			 * insert at head of active chain 
			 */
			INSHEAD(sp->active, cp);

			/*
			 * done 
			 */
			cs_rhits++;
#ifdef	CACHE_DEBUG
			printf("return old cache -- %d\n", cp->op);
#endif
			return (get_attrib(nam, cp->op));
		}
	}

	/*
	 * search modified in-active chain now 
	 */
	for (cp = sp->mold.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * dechain from modified in-active chain 
			 */
			DEQUEUE(sp->mold, cp);

			/*
			 * insert at head of modified active chain 
			 */
			INSHEAD(sp->mactive, cp);

			/*
			 * done 
			 */
			cs_rhits++;

#ifdef	CACHE_DEBUG
			printf("return modified old cache -- %d\n", cp->op);
#endif
			return (get_attrib(nam, cp->op));
		}
	}

	/*
	 * DARN IT - at this point we have a certified, type-A cache miss 
	 */

	/*
	 * thaw the object from wherever. 
	 */

	if ((ret = DB_GET(&(nam->object))) == (Obj *) 0) {
		cs_fails++;
		cs_dbreads++;
#ifdef	CACHE_DEBUG
		printf("Object %d not in db\n", nam->object);
#endif
		return ((Attr *) 0);
	} else
		cs_dbreads++;

	if ((cp = get_free_entry(sp)) == CNULL)
		return ((Attr *) 0);

	cp->op = ret;

	/*
	 * relink at head of active chain 
	 */
	INSHEAD(sp->active, cp);

#ifdef	CACHE_DEBUG
	printf("returning attr %d, object %d loaded into cache -- %d\n",
	       nam->attrnum, nam->object, cp->op);
#endif
	return (get_attrib(nam, ret));
}




/*
 * put an object back into the cache. this is complicated by the
 * fact that when a function calls this with an object, the object
 * is *already* in the cache, since calling functions operate on
 * pointers to the cached objects, and may or may not be actively
 * modifying them. in other words, by the time the object is handed
 * to cache_put, it has probably already been modified, and the cached
 * version probably already reflects those modifications!
 * 
 * so - we do a couple of things: we make sure that the cached
 * object is actually there, and set its dirty bit. if we can't
 * find it - either we have a (major) programming error, or the
 * *name* of the object has been changed, or the object is a totally
 * new creation someone made and is inserting into the world.
 * 
 * in the case of totally new creations, we simply accept the pointer
 * to the object and add it into our environment. freeing it becomes
 * the responsibility of the cache code. DO NOT HAND A POINTER TO
 * CACHE_PUT AND THEN FREE IT YOURSELF!!!!
 * 
 * There are other sticky issues about changing the object pointers
 * of MUDs and their names. This is left as an exercise for the
 * reader.
 */
int
#ifdef RADIX_COMPRESSION
 cache_put(nam, obj, len)
int len;

#else
 cache_put(nam, obj)
#endif				/*
				 * RADIX_COMPRESSION 
				 */
Aname *nam;
Attr *obj;
{
	Cache *cp;
	CacheLst *sp;
	Obj *newobj;
	int hv = 0;

	/*
	 * firewall 
	 */
	if (obj == (Attr *) 0 || nam == (Aname *) 0 || !cache_initted) {
#ifdef	CACHE_VERBOSE
		logf("cache_put: NULL object/name - programmer error\n", (char *)0);
#endif
		return (1);
	}
	cs_writes++;

	/*
	 * generate hash 
	 */
	hv = nam->object % cwidth;
	sp = &sys_c[hv];

	/*
	 * step one, search active chain, and if we find the obj, dirty it 
	 */
	for (cp = sp->active.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * Right object, set the attribute 
			 */

#ifdef RADIX_COMPRESSION
			set_attrib(nam, cp->op, obj, len);
#else
			set_attrib(nam, cp->op, obj);
#endif /*
        * RADIX_COMPRESSION 
        */

#ifdef	CACHE_DEBUG
			printf("cache_put object %d, attr %d -- %d\n",
			       nam->object, nam->attrnum, cp->op);
#endif
			DEQUEUE(sp->active, cp);
			INSHEAD(sp->mactive, cp);
			cs_whits++;
			return (0);
		}
	}

	/*
	 * step two, search modified active chain, and if we find the obj, *
	 * * * we're done 
	 */
	for (cp = sp->mactive.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * Right object, set the attribute 
			 */

#ifdef RADIX_COMPRESSION
			set_attrib(nam, cp->op, obj, len);
#else
			set_attrib(nam, cp->op, obj);
#endif /*
        * RADIX_COMPRESSION 
        */

#ifdef	CACHE_DEBUG
			printf("cache_put object %d,attr %d -- %d\n",
			       nam->object, nam->attrnum, cp->op);
#endif
			cs_whits++;
			return (0);
		}
	}

	/*
	 * step three, search in-active chain.  If we find it, move it to the 
	 * 
	 * *  * *  * * mod active 
	 */
	for (cp = sp->old.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * Right obj. Set the attrib. 
			 */

#ifdef RADIX_COMPRESSION
			set_attrib(nam, cp->op, obj, len);
#else
			set_attrib(nam, cp->op, obj);
#endif

#ifdef	CACHE_DEBUG
			printf("cache_put object %d, attr %d -- %d\n",
			       nam->object, nam->attrnum, cp->op);
#endif
			DEQUEUE(sp->old, cp);
			INSHEAD(sp->mactive, cp);
			cs_whits++;
			return (0);
		}
	}

	/*
	 * step four, search modified in-active chain 
	 */
	for (cp = sp->mold.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {

			/*
			 * Right object. Set the attribute. 
			 */

#ifdef RADIX_COMPRESSION
			set_attrib(nam, cp->op, obj, len);
#else
			set_attrib(nam, cp->op, obj);
#endif /*
        * RADIX_COMPRESSION 
        */

#ifdef	CACHE_DEBUG
			printf("cache_put %d,%d %d\n",
			       nam->object, nam->attrnum, cp->op);
#endif
			DEQUEUE(sp->mold, cp);
			INSHEAD(sp->mactive, cp);
			cs_whits++;
			return (0);
		}
	}

	/*
	 * Ok, now the fact that we're caching objects, and pretending to * * 
	 * 
	 * * cache attributes starts to *hurt*. We gotta try to get the
	 * object * * * in to cache, see.. 
	 */

	newobj = DB_GET(&(nam->object));
	if (newobj == (Obj *) 0) {

		/*
		 * SHIT! Totally NEW object! 
		 */

		newobj = (Obj *) malloc(sizeof(Obj));
		if (newobj == (Obj *) 0)
			return (1);

		newobj->atrs = ATNULL;
		newobj->name = nam->object;
	}
	/*
	 * Now we got the thing, hang the new version of the attrib on it. 
	 */

#ifdef RADIX_COMPRESSION
	set_attrib(nam, newobj, obj, len);
#else
	set_attrib(nam, newobj, obj);
#endif /*
        * RADIX_COMPRESSION 
        */

	/*
	 * add it to the cache 
	 */
	if ((cp = get_free_entry(sp)) == CNULL)
		return (1);

	cp->op = newobj;

	/*
	 * link at head of modified active chain 
	 */
	INSHEAD(sp->mactive, cp);

#ifdef	CACHE_DEBUG
	printf("cache_put %d/%d new in cache %d\n", nam->object, nam->attrnum, cp->op);
#endif

	/*
	 * e finito ! 
	 */
	return (0);
}

static Cache *
 get_free_entry(sp)
CacheLst *sp;
{
	Cache *cp;

	/*
	 * if there are no old cache object holders left, allocate one 
	 */
	if ((cp = sp->old.tail) == CNULL) {

		if (!cache_busy && !cache_frozen &&
		    (cp = sp->mold.tail) != CNULL) {
			/*
			 * unlink old cache chain tail 
			 */
			if (cp->prv != CNULL) {
				sp->mold.tail = cp->prv;
				cp->prv->nxt = cp->nxt;
				if (cp->nxt != CNULL) {
					cp->nxt->prv = cp->prv;
				}
			} else	/*
				 * took last one 
				 */
				sp->mold.head = sp->mold.tail = CNULL;

#ifdef	CACHE_DEBUG
			printf("clean object %d from cache %d\n",
			       (cp->op)->name, cp->op);
#endif
			if ((cp->op)->at_count == 0) {
				if (DB_DEL(&((cp->op)->name), x))
					return (CNULL);
				cs_dels++;
			} else {
				if (DB_PUT(cp->op, &((cp->op)->name)))
					return (CNULL);
				cs_dbwrites++;
			}
		} else {
			if ((cp = (Cache *) malloc(sizeof(Cache))) == CNULL)
				fatal("cache get_free_entry: malloc failed", (char *)-1, (char *)0);

			cp->op = (Obj *) 0;
			sp->count++;
			cs_objects++;
		}
	} else {

		/*
		 * unlink old cache chain tail 
		 */
		if (cp->prv != CNULL) {
			sp->old.tail = cp->prv;
			cp->prv->nxt = cp->nxt;
			if (cp->nxt != CNULL) {
				cp->nxt->prv = cp->prv;
			}
		} else
			sp->old.head = sp->old.tail = CNULL;
	}

	/*
	 * free object's data 
	 */

	cache_repl(cp, ONULL);
	return (cp);
}

static int cache_write(cp)
Cache *cp;
{
	while (cp != CNULL) {
#ifdef	CACHE_DEBUG
		printf("sync %d -- %d\n", cp->op->name, cp->op);
#endif
		if (cp->op->at_count == 0) {
			if (DB_DEL(&((cp->op)->name), x)) {
				log_db_err(cp->op->name, 0, "delete");
				return (1);
			}
			cs_dels++;
		} else {
			if (DB_PUT(cp->op, &((cp->op)->name))) {
				log_db_err(cp->op->name, 0, "write");
				return (1);
			}
			cs_dbwrites++;
		}
		cp = cp->nxt;
	}
	return (0);
}

static void cache_clean(sp)
CacheLst *sp;
{
	/*
	 * move the modified inactive chain to the inactive chain 
	 */
	if (sp->mold.head != NULL) {
		if (sp->old.head == NULL) {
			sp->old.head = sp->mold.head;
			sp->old.tail = sp->mold.tail;
		} else {
			sp->old.tail->nxt = sp->mold.head;
			sp->mold.head->prv = sp->old.tail;
			sp->old.tail = sp->mold.tail;
		}
		sp->mold.head = sp->mold.tail = CNULL;
	}
	/*
	 * same for the modified active chain 
	 */
	if (sp->mactive.head != CNULL) {
		if (sp->active.head == CNULL) {
			sp->active.head = sp->mactive.head;
			sp->active.tail = sp->mactive.tail;
		} else {
			sp->active.tail->nxt = sp->mactive.head;
			sp->mactive.head->prv = sp->active.tail;
			sp->active.tail = sp->mactive.tail;
		}
		sp->mactive.head = sp->mactive.tail = CNULL;
	}
}

int NDECL(cache_sync)
{
	int x;
	CacheLst *sp;

	cs_syncs++;

	if (!cache_initted)
		return (1);

	if (cache_frozen)
		return (0);

	cache_reset(0);

	for (x = 0, sp = sys_c; x < cwidth; x++, sp++) {
		if (cache_write(sp->mold.head))
			return (1);
		if (cache_write(sp->mactive.head))
			return (1);
		cache_clean(sp);
		if (mudconf.cache_trim)
			cache_trim(sp);
	}
	return (0);
}

/*
 * Mark this attr as deleted in the cache. The object will flush back to
 * disk without it, eventually.
 */
void cache_del(nam)
Aname *nam;
{
	Cache *cp;
	CacheLst *sp;
	Obj *obj;
	int hv = 0;

	if (nam == (Aname *) 0 || !cache_initted)
		return;

#ifdef CACHE_DEBUG
	printf("cache_del: object %d, attr %d\n", nam->object, nam->attrnum);
#endif

	cs_dels++;

	hv = nam->object % cwidth;
	sp = &sys_c[hv];

	/*
	 * mark dead in cache 
	 */
	for (cp = sp->active.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			DEQUEUE(sp->active, cp);
			INSHEAD(sp->mactive, cp);
			del_attrib(nam, cp->op);
			return;
		}
	}
	for (cp = sp->mactive.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			del_attrib(nam, cp->op);
			return;
		}
	}

	for (cp = sp->old.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			DEQUEUE(sp->old, cp);
			INSHEAD(sp->mactive, cp);
			del_attrib(nam, cp->op);
			return;
		}
	}
	for (cp = sp->mold.head; cp != CNULL; cp = cp->nxt) {
		if (NAMECMP(cp, nam)) {
			DEQUEUE(sp->mold, cp);
			INSHEAD(sp->mactive, cp);
			del_attrib(nam, cp->op);
			return;
		}
	}

	/*
	 * If we got here, the object the attribute's on isn't in cache 
	 */
	/*
	 * At all.  So we get to fish it off of disk, nuke the attrib,  
	 */
	/*
	 * and shove the object in cache, so it'll be flushed back later 
	 */

	obj = DB_GET(&(nam->object));
	if (obj == (Obj *) 0)
		return;

	if ((cp = get_free_entry(sp)) == CNULL)
		return;

	del_attrib(nam, obj);

	cp->op = obj;

	INSHEAD(sp->mactive, cp);
	return;
}

/*
 * And now a totally new suite of functions for manipulating
 * attributes within an Obj. Woo. Woo. Andrew.
 * 
 */

static Attr *
 get_attrib(anam, obj)
Aname *anam;
Obj *obj;
{
	int lo, mid, hi;
	Attrib *a;

#ifdef CACHE_DEBUG
	printf("get_attrib: object %d, attr %d, obj ptr %d\n",
	       anam->object, anam->attrnum, obj);
#endif

	/*
	 * Binary search for the attribute 
	 */

	lo = 0;
	hi = obj->at_count - 1;
	a = obj->atrs;
	while (lo <= hi) {
		mid = ((hi - lo) >> 1) + lo;
		if (a[mid].attrnum == anam->attrnum) {
			return (Attr *) a[mid].data;
			break;
		} else if (a[mid].attrnum > anam->attrnum) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}
#ifdef CACHE_DEBUG
	printf("get_attrib: not found.\n");
#endif
	return ((Attr *) 0);	/*
				 * Not found 
				 */
}

static void
#ifdef RADIX_COMPRESSION
 set_attrib(anam, obj, value, len)
Aname *anam;
Obj *obj;
Attr *value;
int len;

#else
 set_attrib(anam, obj, value)
Aname *anam;
Obj *obj;
Attr *value;

#endif /*
        * RADIX_COMPRESSION 
        */
{
	int hi, lo, mid;
	Attrib *a;

	/*
	 * Demands made elsewhere insist that we cope with the case of an * * 
	 * 
	 * * empty object. 
	 */

	if (obj->atrs == ATNULL) {
		a = (Attrib *) malloc(sizeof(Attrib));
		if (a == ATNULL)	/*
					 * Fail silently. It's a game. 
					 */
			return;

		obj->atrs = a;
		obj->at_count = 1;
		a[0].attrnum = anam->attrnum;
		a[0].data = (char *)value;
#ifdef RADIX_COMPRESSION
		a[0].size = len;
#else
		a[0].size = ATTR_SIZE(value);
#endif /*
        * RADIX_COMPRESSION 
        */
		return;
	}
	/*
	 * Binary search for the attribute. 
	 */
	lo = 0;
	hi = obj->at_count - 1;

	a = obj->atrs;
	while (lo <= hi) {
		mid = ((hi - lo) >> 1) + lo;
		if (a[mid].attrnum == anam->attrnum) {
			free(a[mid].data);
			a[mid].data = (char *)value;
#ifdef RADIX_COMPRESSION
			a[mid].size = len;
#else
			a[mid].size = ATTR_SIZE(value);
#endif /*
        * RADIX_COMPRESSION 
        */
			return;
		} else if (a[mid].attrnum > anam->attrnum) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}

	/*
	 * If we got here, we didn't find it, so lo = hi + 1, and the * * * * 
	 * attribute should be inserted between them. 
	 */

	a = (Attrib *) realloc(obj->atrs, (obj->at_count + 1) * sizeof(Attrib));

	if (!a) {
		/*
		 * Silently fail. It's just a game. 
		 */

		return;
	}
	/*
	 * Move the stuff upwards one slot. 
	 */
	if (lo < obj->at_count)
		bcopy((char *)(a + lo), (char *)(a + lo + 1),
		      (obj->at_count - lo) * sizeof(Attrib));

	a[lo].data = value;
	a[lo].attrnum = anam->attrnum;
#ifdef RADIX_COMPRESSION
	a[lo].size = len;
#else
	a[lo].size = ATTR_SIZE(value);
#endif /*
        * RADIX_COMPRESSION 
        */
	obj->at_count++;
	obj->atrs = a;

}

static void del_attrib(anam, obj)
Aname *anam;
Obj *obj;
{
	int hi, lo, mid;
	Attrib *a;

#ifdef CACHE_DEBUG
	printf("del_attrib: deleteing attr %d on object %d (%d)\n", anam->attrnum,
	       obj->name, anam->object);
#endif

	if (!obj->at_count || !obj->atrs)
		return;

	if (obj->at_count < 0)
		abort();

	/*
	 * Binary search for the attribute. 
	 */

	lo = 0;
	hi = obj->at_count - 1;
	a = obj->atrs;
	while (lo <= hi) {
		mid = ((hi - lo) >> 1) + lo;
		if (a[mid].attrnum == anam->attrnum) {
			free(a[mid].data);
			obj->at_count--;
			if (mid != obj->at_count)
				bcopy((char *)(a + mid + 1), (char *)(a + mid),
				    (obj->at_count - mid) * sizeof(Attrib));

			if (obj->at_count == 0) {
				free(obj->atrs);
				obj->atrs = ATNULL;
			}
			return;
		} else if (a[mid].attrnum > anam->attrnum) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}
}

/*
 * And something to free all the goo on an Obj, as well as the Obj. 
 */

static void objfree(o)
Obj *o;
{
	int i;
	Attrib *a;

	if (!o->atrs) {
		free(o);
		return;
	}
	a = o->atrs;
	for (i = 0; i < o->at_count; i++)
		free(a[i].data);

	free(a);
	free(o);
}
