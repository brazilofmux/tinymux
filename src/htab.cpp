// htab.cpp - table hashing routines 
//
// $Id: htab.cpp,v 1.2 2001-02-01 23:51:16 sdennis Exp $
//
// MUX 2.0
// Portions are derived from MUX 1.6. Portions are original work.
//
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "htab.h"
#include "alloc.h"

#include "mudconf.h"
#include "svdhash.h"

/*
 * ---------------------------------------------------------------------------
 * hashreset: Reset hash table stats.
 */
void hashreset(CHashTable *htab)
{
    htab->ResetStats();
}

#pragma pack(1)
static struct
{
    int *hashdata;
    char aTarget[LBUF_SIZE+125];
} htab_rec;
#pragma pack()

/*
 * ---------------------------------------------------------------------------
 * * hashfindLEN: Look up an entry in a hash table and return a pointer to its
 * * hash data.
 */

int *hashfindLEN(void *str, int nStr, CHashTable *htab)
{
    if (str == NULL || nStr <= 0) return NULL;

    unsigned long nHash = CRC32_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = HF_FIND_FIRST;
    iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (nTarget == nStr && memcmp(str, htab_rec.aTarget, nStr) == 0)
        {
            return htab_rec.hashdata;
        }
        iDir = htab->FindNextKey(iDir, nHash);
    }
    return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * hashaddLEN: Add a new entry to a hash table.
 */

int hashaddLEN(void *str, int nStr, int *hashdata, CHashTable *htab)
{
    // Make sure that the entry isn't already in the hash table.  If it
    // is, exit with an error.
    //
    if (str == NULL || nStr <= 0) return -1;

    unsigned long nHash = CRC32_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (nTarget == nStr && memcmp(str, htab_rec.aTarget, nStr) == 0)
        {
            return -1;
        }
        iDir = htab->FindNextKey(iDir, nHash);
    }

    // Otherwise, add it.
    //
    htab_rec.hashdata = hashdata;
    memcpy(htab_rec.aTarget, str, nStr);
    unsigned int nRecord = nStr + sizeof(int *);
    htab->Insert(nRecord, nHash, &htab_rec);
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * hashdelete: Remove an entry from a hash table.
 */

void hashdeleteLEN(void *str, int nStr, CHashTable *htab)
{
    if (str == NULL || nStr <= 0) return;

    unsigned long nHash = CRC32_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (nTarget == nStr && memcmp(str, htab_rec.aTarget, nStr) == 0)
        {
            htab->Remove(iDir);
        }
        iDir = htab->FindNextKey(iDir, nHash);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * hashflush: free all the entries in a hashtable.
 */

void hashflush(CHashTable *htab)
{
    htab->Reset();
}

/*
 * ---------------------------------------------------------------------------
 * * hashreplLEN: replace the data part of a hash entry.
 */

int hashreplLEN(void *str, int nStr, int *hashdata, CHashTable *htab)
{
    if (str == NULL || nStr <= 0) return 0;

    unsigned long nHash = CRC32_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (nTarget == nStr && memcmp(str, htab_rec.aTarget, nStr) == 0)
        {
            htab_rec.hashdata = hashdata;
            htab->Update(iDir, nRecord, &htab_rec);
            return 1;
        }
        iDir = htab->FindNextKey(iDir, nHash);
    }
    return 0;
}

void hashreplall(int *old, int *new0, CHashTable *htab)
{
    HP_HEAPLENGTH nRecord;
    for (HP_DIRINDEX iDir = htab->FindFirst(&nRecord, &htab_rec); iDir != HF_FIND_END; iDir = htab->FindNext(&nRecord, &htab_rec))
    {
        if (htab_rec.hashdata == old)
        {
            htab_rec.hashdata = new0;
            htab->Update(iDir, nRecord, &htab_rec);
        }
    }
}

/*
 * Returns the key for the first hash entry in 'htab'. 
 */

int *hash_firstentry(CHashTable *htab)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindFirst(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        return htab_rec.hashdata;
    }
    return NULL;
}

int *hash_nextentry(CHashTable *htab)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindNext(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        return htab_rec.hashdata;
    }
    return NULL;
}

char *hash_firstkey(CHashTable *htab, int *nKeyLength)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindFirst(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        *nKeyLength = nRecord-sizeof(int *);
        return htab_rec.aTarget;
    }
    *nKeyLength = 0;
    return NULL;
}

char *hash_nextkey(CHashTable *htab, int *nKeyLength)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindNext(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        *nKeyLength = nRecord-sizeof(int *);
        return htab_rec.aTarget;
    }
    *nKeyLength = 0;
    return NULL;
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * search_nametab: Search a name table for a match and return the flag value.
 */

int search_nametab(dbref player, NAMETAB *ntab, char *flagname)
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

NAMETAB *find_nametab_ent(dbref player, NAMETAB *ntab, char *flagname)
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

void display_nametab(dbref player, NAMETAB *ntab, char *prefix, int list_if_none)
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

void interp_nametab(dbref player, NAMETAB *ntab, int flagword, char *prefix, char *true_text, char *false_text)
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

void listset_nametab(dbref player, NAMETAB *ntab, int flagword, char *prefix, int list_if_none)
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

    for (ap = str; *ap && !Tiny_IsSpace[(unsigned char)*ap]; ap++) ;
    if (*ap)
        *ap++ = '\0';

    while (Tiny_IsSpace[(unsigned char)*ap])
        ap++;

    for (np = (NAMETAB *) vp; np->name; np++)
    {
        if (minmatch(str, np->name, np->minlen))
        {
            return cf_modify_bits(&(np->perm), ap, extra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, "Entry", str);
    return -1;
}

#endif STANDALONE
