// vattr.cpp -- Manages the user-defined attributes.
//
// $Id: vattr.cpp,v 1.11 2003-01-05 17:49:34 sdennis Exp $
//
// MUX 2.2
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "functions.h"
#include "vattr.h"

static char *store_string(char *);

// Allocate space for strings in lumps this big.
//
#define STRINGBLOCK 1000

// Current block we're putting stuff in
//
static char *stringblock = (char *)NULL;

// High water mark.
//
static int stringblock_hwm = 0;

ATTR *vattr_find_LEN(const char *pAttrName, int nAttrName)
{
    UINT32 nHash = HASH_ProcessBuffer(0, pAttrName, nAttrName);

    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        ATTR *va = (ATTR *)anum_table[anum];
        if (strcmp(pAttrName, va->name) == 0)
        {
            return va;
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
    return NULL;
}

ATTR *vattr_alloc_LEN(char *pName, int nName, int flags)
{
    int number = mudstate.attr_next++;
    anum_extend(number);
    return vattr_define_LEN(pName, nName, number, flags);
}

ATTR *vattr_define_LEN(char *pName, int nName, int number, int flags)
{
    ATTR *vp = vattr_find_LEN(pName, nName);
    if (vp)
    {
        return vp;
    }

    vp = (ATTR *)MEMALLOC(sizeof(ATTR));
    if (ISOUTOFMEMORY(vp))
    {
        return NULL;
    }

    // NOTE: By using store_string, the only way to release the
    // memory associated with a user attribute name is to @restart
    // the game.
    //
    vp->name = store_string(pName);
    vp->flags = flags;
    vp->number = number;

    // This entry cannot already be in the hash table because we've checked it
    // above with vattr_find_LEN.
    //
    UINT32 nHash = HASH_ProcessBuffer(0, pName, nName);
    mudstate.vattr_name_htab.Insert(sizeof(number), nHash, &number);

    anum_extend(vp->number);
    anum_set(vp->number, (ATTR *) vp);
    return vp;
}

#ifndef STANDALONE
extern int anum_alc_top;

// There are five data structures which must remain mutually consistent: The attr_name_htab, vattr_name_htab,
// the anum_table, the A_LIST for every object, and the attribute database.
//
void dbclean_CheckANHtoAT(dbref executor)
{
    notify(executor, "1. Checking (v)attr_name_htabs to anum_table mapping...");

    // This test traverses the attr_name_htab/vattr_name_htab and verifies that the corresponding anum_table
    // entry exists and is valid.
    //
    int nAttributes = 0;
    int nPredefined = 0;
    int nUserDefined = 0;
    int nOutOfBounds = 0;
    int nInvalid = 0;

    for (ATTR *pa = (ATTR *)hash_firstentry(&mudstate.attr_name_htab);
         pa;
         pa = (ATTR *)hash_nextentry(&mudstate.attr_name_htab))
    {
        nAttributes++;
        int iAttr = pa->number;
        if (iAttr <= 0 || iAttr > anum_alc_top)
        {
            nOutOfBounds++;
        }
        else
        {
            if (iAttr < A_USER_START)
            {
                nPredefined++;
            }
            else
            {
                nInvalid++;
            }

            ATTR *pb = (ATTR *) anum_get(iAttr);
            if (pb != pa)
            {
                nInvalid++;
            }
        }
    }

    for (ATTR *va = vattr_first(); va; va = vattr_next(va))
    {
        nAttributes++;
        int iAttr = va->number;
        if (iAttr <= 0 || iAttr > anum_alc_top)
        {
            nOutOfBounds++;
        }
        else
        {
            if (iAttr < A_USER_START)
            {
                nInvalid++;
            }
            else
            {
                nUserDefined++;
            }

            ATTR *vb = (ATTR *) anum_get(iAttr);
            if (vb != va)
            {
                nInvalid++;
            }
        }
    }

    notify(executor, tprintf("   Total Attributes: %d", nAttributes));
    notify(executor, tprintf("   Predefined: %d", nPredefined));
    notify(executor, tprintf("   User Defined: %d", nUserDefined));
    notify(executor, tprintf("   Index Out of Bounds: %d", nOutOfBounds));
    notify(executor, tprintf("   Inconsistent: %d", nInvalid));
    notify(executor, "   Done.");
}

void dbclean_CheckATtoANH(dbref executor)
{
    notify(executor, "2. Checking anum_table to vattr_name_htab mapping...");

    // This test traverses the anum_table and verifies that the corresponding attr_name_htab and
    // vattr_name_htab entries exist and are valid.
    //
    int nAttributes = 0;
    int nPredefined = 0;
    int nUserDefined = 0;
    int nInvalid = 0;
    int nEmpty = 0;
    for (int iAttr = 1; iAttr <= anum_alc_top; iAttr++)
    {
        if (iAttr < A_USER_START)
        {
            ATTR *pa = (ATTR *) anum_get(iAttr);
            if (pa)
            {
                nPredefined++;
                nAttributes++;

                // Convert name to upper case.
                //
                char Buffer[SBUF_SIZE];
                strcpy(Buffer, pa->name);
                mux_strupr(Buffer);

                // Fetch the attribute structure pointer -- which should match the one
                // from the corresponding table entry.
                //
                ATTR *pb = (ATTR *) hashfindLEN(Buffer, strlen(Buffer), &mudstate.attr_name_htab);
                if (pb != pa)
                {
                    nInvalid++;
                }
            }
            else
            {
                nEmpty++;
            }
        }
        else
        {
            ATTR *va = (ATTR *) anum_get(iAttr);
            if (va)
            {
                nUserDefined++;
                nAttributes++;
                ATTR *vb = vattr_find_LEN(va->name, strlen(va->name));
                if (vb != va)
                {
                    nInvalid++;
                }
            }
            else
            {
                nEmpty++;
            }
        }
    }

    notify(executor, tprintf("   Total Attributes: %d", nAttributes));
    notify(executor, tprintf("   Predefined: %d", nPredefined));
    notify(executor, tprintf("   User Defined: %d", nUserDefined));
    notify(executor, tprintf("   Empty: %d", nEmpty));
    notify(executor, tprintf("   Inconsistent: %d", nInvalid));
    notify(executor, "   Done.");
}

void dbclean_CheckALISTtoAT(dbref executor)
{
    notify(executor, "3. Checking ALIST to anum_table mapping...");

    // Traverse every attribute on every object and make sure that attribute is represented in the attribute table.
    //
    dbref iObject;
    int nInvalid = 0;
    int nDangle = 0;
    int nALIST = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr < A_USER_START)
            {
                ATTR *pa = (ATTR *) anum_get(iAttr);
                if (pa == NULL)
                {
                    nInvalid++;
                }
            }
            else if (iAttr <= anum_alc_top)
            {
                ATTR *va = (ATTR *) anum_get(iAttr);
                if (va == NULL)
                {
                    // We can try to fix this one.
                    //
                    const char *pRecord = atr_get_raw(iObject, iAttr);
                    if (pRecord)
                    {
                        // If the attribute exists in the DB, then the easiest thing to do
                        // is add a dummy attribute name. Note: The following attribute
                        // is already in Canonical form, otherwise, we would need to
                        // call MakeCanonicalAttributeName.
                        //
                        char *p = tprintf("DANGLINGATTR-%08d", iAttr);
                        vattr_define_LEN(p, strlen(p), iAttr, 0);
                        nDangle++;
                    }
                    else
                    {
                        // Otherwise, the easiest thing to do is remove it from the ALIST.
                        //
                        atr_clr(iObject, iAttr);
                        nALIST++;
                    }
                }
            }
            else
            {
                nInvalid++;
            }
        }
    }
    notify(executor, tprintf("   Invalid: %d", nInvalid));
    notify(executor, tprintf("   DANGLINGATTR-99999999 added: %d", nDangle));
    notify(executor, tprintf("   ALIST prunes: %d", nALIST));
    atr_pop();
}

void dbclean_CheckALISTtoDB(dbref executor)
{
    notify(executor, "4. Checking ALIST against attribute DB on disk...");

    // Traverse every attribute on every object and make sure that attribute is represented attribute database.
    //
    dbref iObject;
    int nInvalid = 0;
    int nMissing = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr <= anum_alc_top)
            {
                const char *pRecord = atr_get_raw(iObject, iAttr);
                if (!pRecord)
                {
                    // The contents are gone. The easiest thing to do is remove it from the ALIST.
                    //
                    atr_clr(iObject, iAttr);
                    nMissing++;
                }
            }
            else
            {
                nInvalid++;
            }
        }
    }
    notify(executor, tprintf("   Invalid: %d", nInvalid));
    notify(executor, tprintf("   DB prunes: %d", nMissing));
    atr_pop();
}

void dbclean_IntegrityChecking(dbref executor)
{
    dbclean_CheckANHtoAT(executor);
    dbclean_CheckATtoANH(executor);
    dbclean_CheckALISTtoAT(executor);
    dbclean_CheckALISTtoDB(executor);
}

int dbclean_RemoveStaleAttributeNames(void)
{
    ATTR *va;

    // Clear every valid attribute's AF_ISUSED flag
    //
    extern int anum_alc_top;
    int iAttr;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        va = (ATTR *) anum_get(iAttr);
        if (va != NULL)
        {
            va->flags &= ~AF_ISUSED;
        }
    }

    // Traverse every attribute on every object and mark it's attribute as AF_ISUSED.
    //
    dbref iObject;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int attr = atr_head(iObject, &as); attr; attr = atr_next(&as))
        {
            if (attr >= A_USER_START)
            {
                va = (ATTR *) anum_get(attr);
                if (va != NULL)
                {
                    va->flags |= AF_ISUSED;
                }
            }
        }
    }
    atr_pop();

    // Traverse the attribute table again and remove the ones that aren't AF_ISUSED,
    // and count how many vattributes -are- used.
    //
    int cVAttributes = 0;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        va = (ATTR *) anum_get(iAttr);
        if (va != NULL)
        {
            if ((AF_ISUSED & (va->flags)) != AF_ISUSED)
            {
                anum_set(iAttr, NULL);

                // Delete from hashtable.
                //
                UINT32 nHash = HASH_ProcessBuffer(0, va->name, strlen(va->name));
                CHashTable *pht = &mudstate.vattr_name_htab;
                HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
                while (iDir != HF_FIND_END)
                {
                    HP_HEAPLENGTH nRecord;
                    int anum;
                    pht->Copy(iDir, &nRecord, &anum);
                    if (iAttr == anum)
                    {
                        pht->Remove(iDir);
                    }
                    iDir = pht->FindNextKey(iDir, nHash);
                }

                MEMFREE(va);
                va = NULL;
            }
            else
            {
                cVAttributes++;
                va->flags &= ~AF_ISUSED;
            }
        }
    }
    return cVAttributes;
}

void dbclean_RenumberAttributes(int cVAttributes)
{
    ATTR *va;

    // Now that all the stale attribute entries have been removed, we can
    // begin the interesting task of renumbering the attributes that remain.

    // The range [A_USER_START, A_USER_START+cVAttributes] will be left
    // alone. The range (A_USER_START+cVAttribute, anum_alc_top] can be
    // reallocated from the first range. To create this mapping from old
    // attribute numbers to new ones, we need the following table:
    //
    int iMapStart = A_USER_START+cVAttributes+1;
    int iMapEnd = anum_alc_top;
    int nMap = iMapEnd - iMapStart + 1;
    int *aMap = (int *)MEMALLOC(sizeof(int) * nMap);
    if (ISOUTOFMEMORY(aMap))
    {
        return;
    }

    int iSweep = A_USER_START;
    memset(aMap, 0, sizeof(int) * nMap);
    for (int i = nMap - 1; i >= 0 && iSweep < iMapStart; i--)
    {
        int iAttr = iMapStart + i;
        va = (ATTR *) anum_get(iAttr);
        if (va != NULL)
        {
            while (anum_get(iSweep))
            {
                iSweep++;
            }
            int iAllocated = iSweep++;
            aMap[i] = iAllocated;


            // Change vattr_name_htab mapping as well to point to
            // iAllocated instead of iAttr.
            //
            UINT32 nHash = HASH_ProcessBuffer(0, va->name, strlen(va->name));
            CHashTable *pht = &mudstate.vattr_name_htab;
            HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
            while (iDir != HF_FIND_END)
            {
                HP_HEAPLENGTH nRecord;
                int anum;
                pht->Copy(iDir, &nRecord, &anum);
                if (anum == iAttr)
                {
                    pht->Update(iDir, sizeof(int), &iAllocated);
                    break;
                }
                iDir = pht->FindNextKey(iDir, nHash);
            }

            va->number = iAllocated;
            anum_set(iAllocated, (ATTR *)va);
            anum_set(iAttr, NULL);
            mudstate.attr_next = iAttr;
        }
    }

    // aMap contains a unique map from old, high-numbered attribute
    // entries to new, low-numbered, empty attribute entries. We can
    // traverse all the attributes on all the objects again and look for
    // attributes numbers in the range [iMapStart, iMapEnd]. FETCHing
    // them out of the database using the old attribute number, STOREing
    // them in the database using the new attribute number, and
    // TM_DELETEing them under the old attributes number.
    //
    atr_push();
    dbref iObject;
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for ( int iAttr = atr_head(iObject, &as);
              iAttr;
              iAttr = atr_next(&as)
            )
        {
            if (iMapStart <= iAttr && iAttr <= iMapEnd)
            {
                int iNew = aMap[iAttr-iMapStart];
                if (iNew)
                {
                    dbref iOwner;
                    int   iFlag;
                    char *pRecord = atr_get(iObject, iAttr, &iOwner, &iFlag);
                    atr_add_raw(iObject, iNew, pRecord);
                    free_lbuf(pRecord);
                    atr_add_raw(iObject, iAttr, NULL);
                }
            }
        }
    }
    
    // Traverse entire @addcommand data structure.
    //
    int nKeyLength;
    char *pKeyName;
    CMDENT *old;
    for (old = (CMDENT *)hash_firstkey(&mudstate.command_htab, &nKeyLength, &pKeyName);
         old != NULL;
         old = (CMDENT *)hash_nextkey(&mudstate.command_htab, &nKeyLength, &pKeyName))
    {
        if (old && (old->callseq & CS_ADDED))
        {
            pKeyName[nKeyLength] = '\0';
            ADDENT *nextp;
            for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next)
            {
                if (strcmp(pKeyName, nextp->name) != 0)
                {
                    continue;
                }
                int iAttr = nextp->atr;
                if (iMapStart <= iAttr && iAttr <= iMapEnd)
                {
                    int iNew = aMap[iAttr-iMapStart];
                    if (iNew)
                    {
                        nextp->atr = iNew;
                    }
                }
            }
        }
    }

    // Traverse entire @function data structure.
    //
    UFUN *ufp2;
    extern UFUN *ufun_head;
    for (ufp2 = ufun_head; ufp2; ufp2 = ufp2->next)
    {
        int iAttr = ufp2->atr;
        if (iMapStart <= iAttr && iAttr <= iMapEnd)
        {
            int iNew = aMap[iAttr-iMapStart];
            if (iNew)
            {
                ufp2->atr = iNew;
            }
        }
    }
    atr_pop();

    MEMFREE(aMap);
    aMap = NULL;
}

void do_dbclean(dbref executor, dbref caller, dbref enactor, int key)
{
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(executor, "Dumping in progress. Try again later.");
        return;
    }
#endif // !WIN32
#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    notify(executor, "Checking Integrity of the attribute data structures...");
    dbclean_IntegrityChecking(executor);
    notify(executor, "Removing stale attributes names...");
    int cVAttributes = dbclean_RemoveStaleAttributeNames();
    notify(executor, "Renumbering and compacting attribute numbers...");
    dbclean_RenumberAttributes(cVAttributes);
    notify(executor, tprintf("Next Attribute number to allocate: %d", mudstate.attr_next));
    notify(executor, "Checking Integrity of the attribute data structures...");
    dbclean_IntegrityChecking(executor);
    notify(executor, "@dbclean completed..");
}
#endif // !STANDALONE

void vattr_delete_LEN(char *pName, int nName)
{
    // Delete from hashtable.
    //
    UINT32 nHash = HASH_ProcessBuffer(0, pName, nName);
    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        if (strcmp(pName, anum_table[anum]->name) == 0)
        {
            ATTR *vp = (ATTR *)anum_table[anum];
            anum_set(anum, NULL);
            pht->Remove(iDir);
            MEMFREE(vp);
            vp = NULL;
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
}

ATTR *vattr_rename_LEN(char *pOldName, int nOldName, char *pNewName, int nNewName)
{
    // Find and Delete old name from hashtable.
    //
    UINT32 nHash = HASH_ProcessBuffer(0, pOldName, nOldName);
    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        ATTR *vp = (ATTR *)anum_table[anum];
        if (strcmp(pOldName, vp->name) == 0)
        {
            pht->Remove(iDir);

            // Add in new name. After the Insert call, iDir is no longer
            // valid, so don't write code that uses it.
            //
            vp->name = store_string(pNewName);
            nHash = HASH_ProcessBuffer(0, pNewName, nNewName);
            pht->Insert(sizeof(int), nHash, &anum);
            return (ATTR *)anum_table[anum];
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
    return NULL;
}

ATTR *vattr_first(void)
{
    HP_HEAPLENGTH nRecord;
    int anum;
    HP_DIRINDEX iDir = mudstate.vattr_name_htab.FindFirst(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (ATTR *)anum_table[anum];
    }
    return NULL;

}

ATTR *vattr_next(ATTR *vp)
{
    if (vp == NULL)
        return vattr_first();

    HP_HEAPLENGTH nRecord;
    int anum;
    HP_DIRINDEX iDir = mudstate.vattr_name_htab.FindNext(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (ATTR *)anum_table[anum];
    }
    return NULL;
}

// Some goop for efficiently storing strings we expect to keep forever. There
// is no freeing mechanism.
//
static char *store_string(char *str)
{
    int nSize = strlen(str) + 1;

    // If we have no block, or there's not enough room left in the
    // current one, get a new one.
    //
    if (!stringblock || (STRINGBLOCK - stringblock_hwm) < nSize)
    {
        // NOTE: These allocations are -never- freed, and this is
        // intentional.
        //
        stringblock = (char *)MEMALLOC(STRINGBLOCK);
        if (ISOUTOFMEMORY(stringblock))
        {
            return NULL;
        }
        stringblock_hwm = 0;
    }
    char *ret = stringblock + stringblock_hwm;
    memcpy(ret, str, nSize);
    stringblock_hwm += nSize;
    return ret;
}
