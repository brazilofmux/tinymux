// htab.cpp -- Table hashing routines.
//
// $Id: htab.cpp,v 1.4 2003-02-04 00:07:28 sdennis Exp $
//
// MUX 2.3
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

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

int *hashfindLEN(const void *str, int nStr, CHashTable *htab)
{
    if (  str == NULL
       || nStr <= 0)
    {
        return NULL;
    }

    UINT32 nHash = HASH_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = HF_FIND_FIRST;
    iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (  nTarget == nStr
           && memcmp(str, htab_rec.aTarget, nStr) == 0)
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

int hashaddLEN(const void *str, int nStr, int *hashdata, CHashTable *htab)
{
    // Make sure that the entry isn't already in the hash table.  If it
    // is, exit with an error.
    //
    if (  str == NULL
       || nStr <= 0)
    {
        return -1;
    }

    UINT32 nHash = HASH_ProcessBuffer(0, str, nStr);

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

void hashdeleteLEN(const void *str, int nStr, CHashTable *htab)
{
    if (  str == NULL
       || nStr <= 0)
    {
        return;
    }

    UINT32 nHash = HASH_ProcessBuffer(0, str, nStr);

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

BOOL hashreplLEN(const void *str, int nStr, int *hashdata, CHashTable *htab)
{
    if (  str == NULL
       || nStr <= 0)
    {
        return FALSE;
    }

    UINT32 nHash = HASH_ProcessBuffer(0, str, nStr);

    HP_DIRINDEX iDir = htab->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        htab->Copy(iDir, &nRecord, &htab_rec);
        int nTarget = nRecord - sizeof(int *);

        if (  nTarget == nStr
           && memcmp(str, htab_rec.aTarget, nStr) == 0)
        {
            htab_rec.hashdata = hashdata;
            htab->Update(iDir, nRecord, &htab_rec);
            return TRUE;
        }
        iDir = htab->FindNextKey(iDir, nHash);
    }
    return FALSE;
}

void hashreplall(int *old, int *new0, CHashTable *htab)
{
    HP_HEAPLENGTH nRecord;
    for (  HP_DIRINDEX iDir = htab->FindFirst(&nRecord, &htab_rec);
           iDir != HF_FIND_END;
           iDir = htab->FindNext(&nRecord, &htab_rec))
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

int *hash_firstkey(CHashTable *htab, int *nKeyLength, char **pKey)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindFirst(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        *nKeyLength = nRecord-sizeof(int *);
        *pKey = htab_rec.aTarget;
        return htab_rec.hashdata;
    }
    *nKeyLength = 0;
    *pKey = NULL;
    return NULL;
}

int *hash_nextkey(CHashTable *htab, int *nKeyLength, char **pKey)
{
    HP_HEAPLENGTH nRecord;
    HP_DIRINDEX iDir = htab->FindNext(&nRecord, &htab_rec);
    if (iDir != HF_FIND_END)
    {
        *nKeyLength = nRecord-sizeof(int *);
        *pKey = htab_rec.aTarget;
        return htab_rec.hashdata;
    }
    *nKeyLength = 0;
    *pKey = NULL;
    return NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * search_nametab: Search a name table for a match and return the flag value.
 */

int search_nametab(dbref player, NAMETAB *ntab, char *flagname)
{
    NAMETAB *nt;

    for (nt = ntab; nt->name; nt++)
    {
        if (minmatch(flagname, nt->name, nt->minlen))
        {
            if (check_access(player, nt->perm))
            {
                return nt->flag;
            }
            else
            {
                return -2;
            }
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

    for (nt = ntab; nt->name; nt++)
    {
        if (minmatch(flagname, nt->name, nt->minlen))
        {
            if (check_access(player, nt->perm))
            {
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

void display_nametab(dbref player, NAMETAB *ntab, char *prefix, BOOL list_if_none)
{
    char *buf, *bp, *cp;
    NAMETAB *nt;
    BOOL got_one = FALSE;
    bp = buf = alloc_lbuf("display_nametab");

    for (cp = prefix; *cp; cp++)
        *bp++ = *cp;
    for (nt = ntab; nt->name; nt++)
    {
        if (God(player) || check_access(player, nt->perm))
        {
            *bp++ = ' ';
            for (cp = nt->name; *cp; cp++)
                *bp++ = *cp;
            got_one = TRUE;
        }
    }
    *bp = '\0';
    if (got_one || list_if_none)
    {
        notify(player, buf);
    }
    free_lbuf(buf);
}

/* ---------------------------------------------------------------------------
 * interp_nametab: Print values for flags defined in name table.
 */

void interp_nametab(dbref player, NAMETAB *ntab, int flagword, const char *prefix, const char *true_text, const char *false_text)
{
    char *buf = alloc_lbuf("interp_nametab");
    char *bp = buf;
    const char *cp;

    for (cp = prefix; *cp; cp++)
        *bp++ = *cp;
    NAMETAB *nt = ntab;
    while (nt->name)
    {
        if (  God(player)
           || check_access(player, nt->perm))
        {
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

/* ---------------------------------------------------------------------------
 * listset_nametab: Print values for flags defined in name table.
 */

void listset_nametab(dbref player, NAMETAB *ntab, int flagword, char *prefix, BOOL list_if_none)
{
    char *buf, *bp, *cp;
    buf = bp = alloc_lbuf("listset_nametab");
    for (cp = prefix; *cp; cp++)
        *bp++ = *cp;

    NAMETAB *nt = ntab;
    BOOL got_one = FALSE;
    while (nt->name)
    {
        if (  ((flagword & nt->flag) != 0)
           && (  God(player)
              || check_access(player, nt->perm)))
        {
            *bp++ = ' ';
            for (cp = nt->name; *cp; cp++)
                *bp++ = *cp;
            got_one = TRUE;
        }
        nt++;
    }
    *bp = '\0';
    if (got_one || list_if_none)
    {
        notify(player, buf);
    }
    free_lbuf(buf);
}

/* ---------------------------------------------------------------------------
 * cf_ntab_access: Change the access on a nametab entry.
 */

CF_HAND(cf_ntab_access)
{
    NAMETAB *np;
    char *ap;

    for (ap = str; *ap && !mux_isspace[(unsigned char)*ap]; ap++)
    {
        ; // Nothing.
    }
    if (*ap)
    {
        *ap++ = '\0';
    }

    while (mux_isspace[(unsigned char)*ap])
    {
        ap++;
    }

    for (np = (NAMETAB *) vp; np->name; np++)
    {
        if (minmatch(str, np->name, np->minlen))
        {
            return cf_modify_bits(&(np->perm), ap, pExtra, nExtra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, "Entry", str);
    return -1;
}
