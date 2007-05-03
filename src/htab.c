 /*
 * htab.c - table hashing routines 
 */
/*
 * $Id: htab.c,v 1.2 1997/04/16 06:01:10 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "db.h"
#include "externs.h"
#include "htab.h"
#include "alloc.h"

#include "mudconf.h"

/*
 * ---------------------------------------------------------------------------
 * * hashval: Compute hash value of a string for a hash table.
 */

int hashval(str, hashmask)
char *str;
int hashmask;
{
	int hash;
	char *sp;

	/*
	 * If the string pointer is null, return 0.  If not, add up the
	 * numeric value of all the characters and return the sum,
	 * modulo the size of the hash table.
	 */

	if (str == NULL)
		return 0;
	hash = 0;
	for (sp = str; *sp; sp++)
		hash = (hash << 5) + hash + *sp;
	return (hash & hashmask);
}


/*
 * ----------------------------------------------------------------------
 * * get_hashmask: Get hash mask for mask-style hashing.
 */

int get_hashmask(size)
int *size;
{
	int tsize;

	/*
	 * Get next power-of-two >= size, return power-1 as the mask * for *
	 * * * ANDing 
	 */

	for (tsize = 1; tsize < *size; tsize = tsize << 1) ;
	*size = tsize;
	return tsize - 1;
}

/*
 * ---------------------------------------------------------------------------
 * * hashinit: Initialize a new hash table.
 */

void hashinit(htab, size)
HASHTAB *htab;
int size;
{
	int i;

	htab->mask = get_hashmask(&size);
	htab->hashsize = size;
	htab->checks = 0;
	htab->scans = 0;
	htab->max_scan = 0;
	htab->hits = 0;
	htab->entries = 0;
	htab->deletes = 0;
	htab->nulls = size;
	htab->entry =
		(HASHARR *) malloc(size * sizeof(struct hashentry *));

	for (i = 0; i < size; i++)
		htab->entry->element[i] = NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * hashreset: Reset hash table stats.
 */

void hashreset(htab)
HASHTAB *htab;
{
	htab->checks = 0;
	htab->scans = 0;
	htab->hits = 0;
}

/*
 * ---------------------------------------------------------------------------
 * * hashfind: Look up an entry in a hash table and return a pointer to its
 * * hash data.
 */

int *hashfind(str, htab)
char *str;
HASHTAB *htab;
{
	int hval, numchecks;
	HASHENT *hptr, *prev;

	numchecks = 0;
	htab->scans++;
	hval = hashval(str, htab->mask);
	for (prev = hptr = htab->entry->element[hval]; hptr != NULL; hptr = hptr->next) {
		numchecks++;
		if (strcmp(str, hptr->target) == 0) {
			htab->hits++;
			if (numchecks > htab->max_scan)
				htab->max_scan = numchecks;
			htab->checks += numchecks;
			hptr->checks++;
			
			/* If the string has been checked more than 20 times,
			 * move it to the head of the chain, and reset the
			 * check counter.
			 */
			 
			if ((hptr->checks > 20) && (hptr != prev)) {
				prev->next = hptr->next;
				hptr->next = htab->entry->element[hval];
				htab->entry->element[hval] = hptr;
				hptr->checks = 0;
			}
			return hptr->data;
		}
		prev = hptr;
	}
	if (numchecks > htab->max_scan)
		htab->max_scan = numchecks;
	htab->checks += numchecks;
	return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * hashadd: Add a new entry to a hash table.
 */

int hashadd(str, hashdata, htab)
char *str;
int *hashdata;
HASHTAB *htab;
{
	int hval;
	HASHENT *hptr;

	/*
	 * Make sure that the entry isn't already in the hash table.  If it
	 * is, exit with an error.  Otherwise, create a new hash block and
	 * link it in at the head of its thread.
	 */

	if (hashfind(str, htab) != NULL)
		return (-1);
	hval = hashval(str, htab->mask);
	htab->entries++;
	if (htab->entry->element[hval] == NULL)
		htab->nulls--;
	hptr = (HASHENT *) malloc(sizeof(HASHENT));
	hptr->target = (char *)strsave(str);
	hptr->data = hashdata;
	hptr->checks = 0;
	hptr->next = htab->entry->element[hval];
	htab->entry->element[hval] = hptr;
	return (0);
}

/*
 * ---------------------------------------------------------------------------
 * * hashdelete: Remove an entry from a hash table.
 */

void hashdelete(str, htab)
char *str;
HASHTAB *htab;
{
	int hval;
	HASHENT *hptr, *last;

	hval = hashval(str, htab->mask);
	last = NULL;
	for (hptr = htab->entry->element[hval];
	     hptr != NULL;
	     last = hptr, hptr = hptr->next) {
		if (strcmp(str, hptr->target) == 0) {
			if (last == NULL)
				htab->entry->element[hval] = hptr->next;
			else
				last->next = hptr->next;
			free(hptr->target);
			free(hptr);
			htab->deletes++;
			htab->entries--;
			if (htab->entry->element[hval] == NULL)
				htab->nulls++;
			return;
		}
	}
}

/*
 * ---------------------------------------------------------------------------
 * * hashflush: free all the entries in a hashtable.
 */

void hashflush(htab, size)
HASHTAB *htab;
int size;
{
	HASHENT *hent, *thent;
	int i;

	for (i = 0; i < htab->hashsize; i++) {
		hent = htab->entry->element[i];
		while (hent != NULL) {
			thent = hent;
			hent = hent->next;
			free(thent->target);
			free(thent);
		}
		htab->entry->element[i] = NULL;
	}

	/*
	 * Resize if needed.  Otherwise, just zero all the stats 
	 */

	if ((size > 0) && (size != htab->hashsize)) {
		free(htab->entry);
		hashinit(htab, size);
	} else {
		htab->checks = 0;
		htab->scans = 0;
		htab->max_scan = 0;
		htab->hits = 0;
		htab->entries = 0;
		htab->deletes = 0;
		htab->nulls = htab->hashsize;
	}
}

/*
 * ---------------------------------------------------------------------------
 * * hashrepl: replace the data part of a hash entry.
 */

int hashrepl(str, hashdata, htab)
char *str;
int *hashdata;
HASHTAB *htab;
{
	HASHENT *hptr;
	int hval;

	hval = hashval(str, htab->mask);
	for (hptr = htab->entry->element[hval];
	     hptr != NULL;
	     hptr = hptr->next) {
		if (strcmp(str, hptr->target) == 0) {
			hptr->data = hashdata;
			return 1;
		}
	}
	return 0;
}

void hashreplall(old, new, htab)
int *old, *new;
HASHTAB *htab;
{
	int hval;
	HASHENT *hptr;

	for (hval = 0; hval < htab->hashsize; hval++)
		for (hptr = htab->entry->element[hval]; hptr != NULL; hptr = hptr->next) {
			if (hptr->data == old)
				hptr->data = new;
		}
}


/*
 * ---------------------------------------------------------------------------
 * * hashinfo: return an mbuf with hashing stats
 */

char *hashinfo(tab_name, htab)
const char *tab_name;
HASHTAB *htab;
{
	char *buff;

	buff = alloc_mbuf("hashinfo");
	sprintf(buff, "%-15s %4d%8d%8d%8d%8d%8d%8d%8d",
		tab_name, htab->hashsize, htab->entries, htab->deletes,
		htab->nulls, htab->scans, htab->hits, htab->checks,
		htab->max_scan);
	return buff;
}

/*
 * Returns the key for the first hash entry in 'htab'. 
 */

int *hash_firstentry(htab)
HASHTAB *htab;
{
	int hval;

	for (hval = 0; hval < htab->hashsize; hval++)
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->data;
		}
	return NULL;
}

int *hash_nextentry(htab)
HASHTAB *htab;
{
	int hval;
	HASHENT *hptr;

	hval = htab->last_hval;
	hptr = htab->last_entry;
	if (hptr->next != NULL) {	/*
					 * We can stay in the same chain 
					 */
		htab->last_entry = hptr->next;
		return hptr->next->data;
	}
	/*
	 * We were at the end of the previous chain, go to the next one 
	 */
	hval++;
	while (hval < htab->hashsize) {
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->data;
		}
		hval++;
	}
	return NULL;
}

char *hash_firstkey(htab)
HASHTAB *htab;
{
	int hval;

	for (hval = 0; hval < htab->hashsize; hval++)
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->target;
		}
	return NULL;
}

char *hash_nextkey(htab)
HASHTAB *htab;
{
	int hval;
	HASHENT *hptr;

	hval = htab->last_hval;
	hptr = htab->last_entry;
	if (hptr->next != NULL) {	/*
					 * We can stay in the same chain 
					 */
		htab->last_entry = hptr->next;
		return hptr->next->target;
	}
	/*
	 * We were at the end of the previous chain, go to the next one 
	 */
	hval++;
	while (hval < htab->hashsize) {
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->target;
		}
		hval++;
	}
	return NULL;
}

#ifndef STANDALONE

int *nhash_firstentry(htab)
NHSHTAB *htab;
{
	int hval;

	for (hval = 0; hval < htab->hashsize; hval++)
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->data;
		}
	return NULL;
}

int *nhash_nextentry(htab)
NHSHTAB *htab;
{
	int hval;
	NHSHENT *hptr;

	hval = htab->last_hval;
	hptr = htab->last_entry;
	if (hptr->next != NULL) {	/*
					 * We can stay in the same chain 
					 */
		htab->last_entry = hptr->next;
		return hptr->next->data;
	}
	/*
	 * We were at the end of the previous chain, go to the next one 
	 */
	hval++;
	while (hval < htab->hashsize) {
		if (htab->entry->element[hval] != NULL) {
			htab->last_hval = hval;
			htab->last_entry = htab->entry->element[hval];
			return htab->entry->element[hval]->data;
		}
		hval++;
	}
	return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * nhashfind: Look up an entry in a numeric hash table and return a pointer
 * * to its hash data.
 */

int *nhashfind(val, htab)
int val;
NHSHTAB *htab;
{
	int hval, numchecks;
	NHSHENT *hptr, *prev;

	numchecks = 0;
	htab->scans++;
	hval = (val & htab->mask);
	for (prev = hptr = htab->entry->element[hval]; hptr != NULL; hptr = hptr->next) {
		numchecks++;
		if (val == hptr->target) {
			htab->hits++;
			if (numchecks > htab->max_scan)
				htab->max_scan = numchecks;
			htab->checks += numchecks;
			hptr->checks++;
			if ((hptr->checks > 20) && (hptr != prev)) {
				prev->next = hptr->next;
				hptr->next = htab->entry->element[hval];
				htab->entry->element[hval] = hptr;
				hptr->checks = 0;
			}
			return hptr->data;
		}
		prev = hptr;
	}
	if (numchecks > htab->max_scan)
		htab->max_scan = numchecks;
	htab->checks += numchecks;
	return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * nhashadd: Add a new entry to a numeric hash table.
 */

int nhashadd(val, hashdata, htab)
int val, *hashdata;
NHSHTAB *htab;
{
	int hval;
	NHSHENT *hptr;

	/*
	 * Make sure that the entry isn't already in the hash table.  If it
	 * is, exit with an error.  Otherwise, create a new hash block and
	 * link it in at the head of its thread.
	 */

	if (nhashfind(val, htab) != NULL)
		return (-1);
	hval = (val & htab->mask);
	htab->entries++;
	if (htab->entry->element[hval] == NULL)
		htab->nulls--;
	hptr = (NHSHENT *) malloc(sizeof(NHSHENT));
	hptr->target = val;
	hptr->data = hashdata;
	hptr->checks = 0;
	hptr->next = htab->entry->element[hval];
	htab->entry->element[hval] = hptr;
	return (0);
}

/*
 * ---------------------------------------------------------------------------
 * * nhashdelete: Remove an entry from a numeric hash table.
 */

void nhashdelete(val, htab)
int val;
NHSHTAB *htab;
{
	int hval;
	NHSHENT *hptr, *last;

	hval = (val & htab->mask);
	last = NULL;
	for (hptr = htab->entry->element[hval];
	     hptr != NULL;
	     last = hptr, hptr = hptr->next) {
		if (val == hptr->target) {
			if (last == NULL)
				htab->entry->element[hval] = hptr->next;
			else
				last->next = hptr->next;
			free(hptr);
			htab->deletes++;
			htab->entries--;
			if (htab->entry->element[hval] == NULL)
				htab->nulls++;
			return;
		}
	}
}

/*
 * ---------------------------------------------------------------------------
 * * nhashflush: free all the entries in a hashtable.
 */

void nhashflush(htab, size)
NHSHTAB *htab;
int size;
{
	NHSHENT *hent, *thent;
	int i;

	for (i = 0; i < htab->hashsize; i++) {
		hent = htab->entry->element[i];
		while (hent != NULL) {
			thent = hent;
			hent = hent->next;
			free(thent);
		}
		htab->entry->element[i] = NULL;
	}

	/*
	 * Resize if needed.  Otherwise, just zero all the stats 
	 */

	if ((size > 0) && (size != htab->hashsize)) {
		free(htab->entry);
		nhashinit(htab, size);
	} else {
		htab->checks = 0;
		htab->scans = 0;
		htab->max_scan = 0;
		htab->hits = 0;
		htab->entries = 0;
		htab->deletes = 0;
		htab->nulls = htab->hashsize;
	}
}

/*
 * ---------------------------------------------------------------------------
 * * nhashrepl: replace the data part of a hash entry.
 */

int nhashrepl(val, hashdata, htab)
int val, *hashdata;
NHSHTAB *htab;
{
	NHSHENT *hptr;
	int hval;

	hval = (val & htab->mask);
	for (hptr = htab->entry->element[hval];
	     hptr != NULL;
	     hptr = hptr->next) {
		if (hptr->target == val) {
			hptr->data = hashdata;
			return 1;
		}
	}
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * search_nametab: Search a name table for a match and return the flag value.
 */

int search_nametab(player, ntab, flagname)
dbref player;
NAMETAB *ntab;
char *flagname;
{
	NAMETAB *nt;

	for (nt = ntab; nt->name; nt++) {
		if (minmatch(flagname, nt->name, nt->minlen)) {
			if (check_access(player, nt->perm)) {
				return nt->flag;
			} else
				return -2;
		}
	}
	return -1;
}

/*
 * ---------------------------------------------------------------------------
 * * find_nametab_ent: Search a name table for a match and return a pointer to it.
 */

NAMETAB *find_nametab_ent(player, ntab, flagname)
dbref player;
NAMETAB *ntab;
char *flagname;
{
	NAMETAB *nt;

	for (nt = ntab; nt->name; nt++) {
		if (minmatch(flagname, nt->name, nt->minlen)) {
			if (check_access(player, nt->perm)) {
				return nt;
			}
		}
	}
	return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * display_nametab: Print out the names of the entries in a name table.
 */

void display_nametab(player, ntab, prefix, list_if_none)
dbref player;
NAMETAB *ntab;
char *prefix;
int list_if_none;
{
	char *buf, *bp, *cp;
	NAMETAB *nt;
	int got_one;

	buf = alloc_lbuf("display_nametab");
	bp = buf;
	got_one = 0;
	for (cp = prefix; *cp; cp++)
		*bp++ = *cp;
	for (nt = ntab; nt->name; nt++) {
		if (God(player) || check_access(player, nt->perm)) {
			*bp++ = ' ';
			for (cp = nt->name; *cp; cp++)
				*bp++ = *cp;
			got_one = 1;
		}
	}
	*bp = '\0';
	if (got_one || list_if_none)
		notify(player, buf);
	free_lbuf(buf);
}



/*
 * ---------------------------------------------------------------------------
 * * interp_nametab: Print values for flags defined in name table.
 */

void interp_nametab(player, ntab, flagword, prefix, true_text, false_text)
dbref player;
NAMETAB *ntab;
int flagword;
char *prefix, *true_text, *false_text;
{
	char *buf, *bp, *cp;
	NAMETAB *nt;

	buf = alloc_lbuf("interp_nametab");
	bp = buf;
	for (cp = prefix; *cp; cp++)
		*bp++ = *cp;
	nt = ntab;
	while (nt->name) {
		if (God(player) || check_access(player, nt->perm)) {
			*bp++ = ' ';
			for (cp = nt->name; *cp; cp++)
				*bp++ = *cp;
			*bp++ = '.';
			*bp++ = '.';
			*bp++ = '.';
			if ((flagword & nt->flag) != 0)
				cp = true_text;
			else
				cp = false_text;
			while (*cp)
				*bp++ = *cp++;
			if ((++nt)->name)
				*bp++ = ';';
		}
	}
	*bp = '\0';
	notify(player, buf);
	free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * listset_nametab: Print values for flags defined in name table.
 */

void listset_nametab(player, ntab, flagword, prefix, list_if_none)
dbref player;
NAMETAB *ntab;
int flagword, list_if_none;
char *prefix;
{
	char *buf, *bp, *cp;
	NAMETAB *nt;
	int got_one;

	buf = bp = alloc_lbuf("listset_nametab");
	for (cp = prefix; *cp; cp++)
		*bp++ = *cp;
	nt = ntab;
	got_one = 0;
	while (nt->name) {
		if (((flagword & nt->flag) != 0) &&
		    (God(player) || check_access(player, nt->perm))) {
			*bp++ = ' ';
			for (cp = nt->name; *cp; cp++)
				*bp++ = *cp;
			got_one = 1;
		}
		nt++;
	}
	*bp = '\0';
	if (got_one || list_if_none)
		notify(player, buf);
	free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_ntab_access: Change the access on a nametab entry.
 */

extern void FDECL(cf_log_notfound, (dbref, char *, const char *, char *));

CF_HAND(cf_ntab_access)
{
	NAMETAB *np;
	char *ap;

	for (ap = str; *ap && !isspace(*ap); ap++) ;
	if (*ap)
		*ap++ = '\0';
	while (*ap && isspace(*ap))
		ap++;
	for (np = (NAMETAB *) vp; np->name; np++) {
		if (minmatch(str, np->name, np->minlen)) {
			return cf_modify_bits(&(np->perm), ap, extra,
					      player, cmd);
		}
	}
	cf_log_notfound(player, cmd, "Entry", str);
	return -1;
}

#endif STANDALONE