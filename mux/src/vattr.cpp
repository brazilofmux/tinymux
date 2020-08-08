/*! \file vattr.cpp
 * \brief Manages the user-defined attributes.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "functions.h"
#include "vattr.h"

static UTF8 *store_string(const UTF8 *);

// Allocate space for strings in lumps this big.
//
#define STRINGBLOCK 1000

// Current block we're putting stuff in
//
static UTF8 *stringblock = nullptr;

// High water mark.
//
static size_t stringblock_hwm = 0;

ATTR *vattr_find_LEN(const UTF8 *pAttrName, size_t nAttrName)
{
    UINT32 nHash = HASH_ProcessBuffer(0, pAttrName, nAttrName);

    CHashTable *pht = &mudstate.vattr_name_htab;
    UINT32 iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        ATTR *va = (ATTR *)anum_table[anum];
        if (strcmp((char *)pAttrName, (char *)va->name) == 0)
        {
            return va;
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
    return nullptr;
}

ATTR *vattr_alloc_LEN(const UTF8 *pName, size_t nName, int flags)
{
    int number = mudstate.attr_next++;
    anum_extend(number);
    return vattr_define_LEN(pName, nName, number, flags);
}

ATTR *vattr_define_LEN(const UTF8 *pName, size_t nName, int number, int flags)
{
    ATTR *vp = vattr_find_LEN(pName, nName);
    if (vp)
    {
        return vp;
    }

    vp = (ATTR *)MEMALLOC(sizeof(ATTR));
    if (vp)
    {
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
    }
    else
    {
        ISOUTOFMEMORY(vp);
    }
    return vp;
}

// There are five data structures which must remain mutually consistent: The
// attr_name_htab, vattr_name_htab, the anum_table, the A_LIST for every
// object, and the attribute database.
//
static void dbclean_CheckANHtoAT(dbref executor)
{
    notify(executor, T("1. Checking (v)attr_name_htabs to anum_table mapping..."));

    // This test traverses the attr_name_htab/vattr_name_htab and verifies
    // that the corresponding anum_table entry exists and is valid.
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

    notify(executor, tprintf(T("   Total Attributes: %d"), nAttributes));
    notify(executor, tprintf(T("   Predefined: %d"), nPredefined));
    notify(executor, tprintf(T("   User Defined: %d"), nUserDefined));
    notify(executor, tprintf(T("   Index Out of Bounds: %d"), nOutOfBounds));
    notify(executor, tprintf(T("   Inconsistent: %d"), nInvalid));
    notify(executor, T("   Done."));
}

static void dbclean_CheckATtoANH(dbref executor)
{
    notify(executor, T("2. Checking anum_table to vattr_name_htab mapping..."));

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
                size_t nCased;
                UTF8 *pCased = mux_strupr(pa->name, nCased);

                // Fetch the attribute structure pointer -- which should match the one
                // from the corresponding table entry.
                //
                ATTR *pb = (ATTR *) hashfindLEN(pCased, nCased, &mudstate.attr_name_htab);
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
                ATTR *vb = vattr_find_LEN(va->name, strlen((char *)va->name));
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

    notify(executor, tprintf(T("   Total Attributes: %d"), nAttributes));
    notify(executor, tprintf(T("   Predefined: %d"), nPredefined));
    notify(executor, tprintf(T("   User Defined: %d"), nUserDefined));
    notify(executor, tprintf(T("   Empty: %d"), nEmpty));
    notify(executor, tprintf(T("   Inconsistent: %d"), nInvalid));
    notify(executor, T("   Done."));
}

static void dbclean_CheckALISTtoAT(dbref executor)
{
    notify(executor, T("3. Checking ALIST to anum_table mapping..."));

    // Traverse every attribute on every object and make sure that attribute is
    // represented in the attribute table.
    //
    dbref iObject;
    int nInvalid = 0;
    int nDangle = 0;
    int nALIST = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        unsigned char *as;
        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr < A_USER_START)
            {
                ATTR *pa = (ATTR *) anum_get(iAttr);
                if (pa == nullptr)
                {
                    nInvalid++;
                }
            }
            else if (iAttr <= anum_alc_top)
            {
                ATTR *va = (ATTR *) anum_get(iAttr);
                if (va == nullptr)
                {
                    // We can try to fix this one.
                    //
                    const UTF8 *pRecord = atr_get_raw(iObject, iAttr);
                    if (pRecord)
                    {
                        // If the attribute exists in the DB, then the easiest thing to do
                        // is add a dummy attribute name. Note: The following attribute
                        // is already in Canonical form, otherwise, we would need to
                        // call MakeCanonicalAttributeName.
                        //
                        UTF8 *p = tprintf(T("DANGLINGATTR-%08d"), iAttr);
                        vattr_define_LEN(p, strlen((char *)p), iAttr, 0);
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
    notify(executor, tprintf(T("   Invalid: %d"), nInvalid));
    notify(executor, tprintf(T("   DANGLINGATTR-99999999 added: %d"), nDangle));
    notify(executor, tprintf(T("   ALIST prunes: %d"), nALIST));
    atr_pop();
}

static void dbclean_CheckALISTtoDB(dbref executor)
{
    notify(executor, T("4. Checking ALIST against attribute DB on disk..."));

    // Traverse every attribute on every object and make sure that attribute is
    // represented attribute database.
    //
    dbref iObject;
    int nInvalid = 0;
    int nMissing = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        unsigned char *as;
        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr <= anum_alc_top)
            {
                const UTF8 *pRecord = atr_get_raw(iObject, iAttr);
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
    notify(executor, tprintf(T("   Invalid: %d"), nInvalid));
    notify(executor, tprintf(T("   DB prunes: %d"), nMissing));
    atr_pop();
}

static void dbclean_IntegrityChecking(dbref executor)
{
    dbclean_CheckANHtoAT(executor);
    dbclean_CheckATtoANH(executor);
    dbclean_CheckALISTtoAT(executor);
    dbclean_CheckALISTtoDB(executor);
}

static int dbclean_RemoveStaleAttributeNames(void)
{
    ATTR *va;

    // Clear every valid attribute's AF_ISUSED flag
    //
    int iAttr;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        va = (ATTR *) anum_get(iAttr);
        if (va != nullptr)
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
        unsigned char *as;
        for (int atr = atr_head(iObject, &as); atr; atr = atr_next(&as))
        {
            if (atr >= A_USER_START)
            {
                va = (ATTR *) anum_get(atr);
                if (va != nullptr)
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
        if (va != nullptr)
        {
            if ((AF_ISUSED & (va->flags)) != AF_ISUSED)
            {
                anum_set(iAttr, nullptr);

                // Delete from hashtable.
                //
                UINT32 nHash = HASH_ProcessBuffer(0, va->name, strlen((char *)va->name));
                CHashTable *pht = &mudstate.vattr_name_htab;
                UINT32 iDir = pht->FindFirstKey(nHash);
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
                va = nullptr;
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

static void dbclean_RenumberAttributes(int cVAttributes)
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
    ISOUTOFMEMORY(aMap);

    int iSweep = A_USER_START;
    memset(aMap, 0, sizeof(int) * nMap);
    for (int i = nMap - 1; i >= 0 && iSweep < iMapStart; i--)
    {
        int iAttr = iMapStart + i;
        va = (ATTR *) anum_get(iAttr);
        if (va != nullptr)
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
            UINT32 nHash = HASH_ProcessBuffer(0, va->name, strlen((char *)va->name));
            CHashTable *pht = &mudstate.vattr_name_htab;
            UINT32 iDir = pht->FindFirstKey(nHash);
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
            anum_set(iAttr, nullptr);
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
    UTF8 *tbuff = alloc_lbuf("dbclean_RenumberAttributes.534");
    DO_WHOLE_DB(iObject)
    {
        unsigned char *as;
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
                    // Copy value from old attribute number to new attribute
                    // number. Raw access does not support using returned
                    // pointer for any other database access, so value must be
                    // copied to a temporary buffer.  Encoded attribute flags
                    // and encoded attribute owner are copied in encoded form.
                    //
                    size_t n;
                    const UTF8 *p = atr_get_raw_LEN(iObject, iAttr, &n);
                    if (nullptr != p)
                    {
                        memcpy(tbuff, p, n);
                        atr_add_raw_LEN(iObject, iNew, tbuff, n);
                    }

                    // Delete value at old attribute number.
                    //
                    atr_add_raw_LEN(iObject, iAttr, nullptr, 0);
                }
            }
        }
    }
    free_lbuf(tbuff);
    tbuff = nullptr;

    // Traverse entire @addcommand data structure.
    //
    int nKeyLength;
    UTF8 *pKeyName;
    CMDENT *old;
    for (old = (CMDENT *)hash_firstkey(&mudstate.command_htab, &nKeyLength, &pKeyName);
         old != nullptr;
         old = (CMDENT *)hash_nextkey(&mudstate.command_htab, &nKeyLength, &pKeyName))
    {
        if (old && (old->callseq & CS_ADDED))
        {
            pKeyName[nKeyLength] = '\0';
            ADDENT *nextp;
            for (nextp = old->addent; nextp != nullptr; nextp = nextp->next)
            {
                if (strcmp((char *)pKeyName, (char *)nextp->name) != 0)
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
    aMap = nullptr;
}

void do_dbclean(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

#if defined(HAVE_WORKING_FORK)
    if (mudstate.dumping)
    {
        notify(executor, T("Dumping in progress. Try again later."));
        return;
    }
#endif // HAVE_WORKING_FORK
#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED
    pcache_sync();

    notify(executor, T("Checking Integrity of the attribute data structures..."));
    dbclean_IntegrityChecking(executor);
    notify(executor, T("Removing stale attributes names..."));
    int cVAttributes = dbclean_RemoveStaleAttributeNames();
    notify(executor, T("Renumbering and compacting attribute numbers..."));
    dbclean_RenumberAttributes(cVAttributes);
    notify(executor, tprintf(T("Next Attribute number to allocate: %d"), mudstate.attr_next));
    notify(executor, T("Checking Integrity of the attribute data structures..."));
    dbclean_IntegrityChecking(executor);
    notify(executor, T("@dbclean completed.."));
}

void vattr_delete_LEN(UTF8 *pName, size_t nName)
{
    // Delete from hashtable.
    //
    UINT32 nHash = HASH_ProcessBuffer(0, pName, nName);
    CHashTable *pht = &mudstate.vattr_name_htab;
    UINT32 iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        if (strcmp((char *)pName, (char *)anum_table[anum]->name) == 0)
        {
            ATTR *vp = (ATTR *)anum_table[anum];
            anum_set(anum, nullptr);
            pht->Remove(iDir);
            MEMFREE(vp);
            vp = nullptr;
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
}

ATTR *vattr_rename_LEN(UTF8 *pOldName, size_t nOldName, UTF8 *pNewName, size_t nNewName)
{
    // Find and Delete old name from hashtable.
    //
    UINT32 nHash = HASH_ProcessBuffer(0, pOldName, nOldName);
    CHashTable *pht = &mudstate.vattr_name_htab;
    UINT32 iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        ATTR *vp = (ATTR *)anum_table[anum];
        if (strcmp((char *)pOldName, (char *)vp->name) == 0)
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
    return nullptr;
}

ATTR *vattr_first(void)
{
    HP_HEAPLENGTH nRecord;
    int anum;
    UINT32 iDir = mudstate.vattr_name_htab.FindFirst(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (ATTR *)anum_table[anum];
    }
    return nullptr;

}

ATTR *vattr_next(ATTR *vp)
{
    if (vp == nullptr)
        return vattr_first();

    HP_HEAPLENGTH nRecord;
    int anum;
    UINT32 iDir = mudstate.vattr_name_htab.FindNext(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (ATTR *)anum_table[anum];
    }
    return nullptr;
}

// Some goop for efficiently storing strings we expect to keep forever. There
// is no freeing mechanism.
//
static UTF8 *store_string(const UTF8 *str)
{
    size_t nSize = strlen((char *)str) + 1;

    // If we have no block, or there's not enough room left in the
    // current one, get a new one.
    //
    if (  !stringblock
       || (STRINGBLOCK - stringblock_hwm) < nSize)
    {
        // NOTE: These allocations are -never- freed, and this is
        // intentional.
        //
        stringblock = (UTF8 *)MEMALLOC(STRINGBLOCK);
        ISOUTOFMEMORY(stringblock);
        stringblock_hwm = 0;
    }
    UTF8 *ret = stringblock + stringblock_hwm;
    memcpy(ret, str, nSize);
    stringblock_hwm += nSize;
    return ret;
}
