/*! \file vattr.cpp
 * \brief Manages the user-defined attributes.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "sqlite_backend.h"

using namespace std;

ATTR *vattr_find_LEN(const UTF8 *pAttrName, size_t nAttrName)
{
    string key(reinterpret_cast<const char *>(pAttrName), nAttrName);
    auto it = mudstate.vattr_name_map.find(key);
    if (it != mudstate.vattr_name_map.end())
    {
        return static_cast<ATTR *>(anum_table[it->second]);
    }
    return nullptr;
}

ATTR *vattr_alloc_LEN(const UTF8 *pName, size_t nName, int flags)
{
    int number = mudstate.attr_next++;
    anum_extend(number);
    ATTR *vp = vattr_define_LEN(pName, nName, number, flags);

    if (!mudstate.bSQLiteLoading)
    {
        g_pSQLiteBackend->GetDB().PutMeta("attr_next", mudstate.attr_next);
    }

    return vp;
}

ATTR *vattr_define_LEN(const UTF8 *pName, size_t nName, int number, int flags)
{
    ATTR *vp = vattr_find_LEN(pName, nName);
    if (vp)
    {
        return vp;
    }

    vp = static_cast<ATTR *>(MEMALLOC(sizeof(ATTR)));
    if (vp)
    {
        string key(reinterpret_cast<const char *>(pName), nName);
        auto [it, inserted] = mudstate.vattr_name_map.emplace(move(key), number);

        // ATTR::name points directly into the map key.  unordered_map
        // guarantees pointer stability for existing elements across
        // insertions, so this pointer remains valid until the element
        // is erased.
        //
        vp->name = reinterpret_cast<const UTF8 *>(it->first.c_str());
        vp->flags = flags;
        vp->number = number;

        anum_extend(vp->number);
        anum_set(vp->number, static_cast<ATTR *>(vp));

        if (  !mudstate.bSQLiteLoading
           && number >= A_USER_START)
        {
            g_pSQLiteBackend->GetDB().PutAttrName(number,
                reinterpret_cast<const char *>(pName), flags);
        }
    }
    else
    {
        ISOUTOFMEMORY(vp);
    }
    return vp;
}

void do_dbclean(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    notify(executor, T("@dbclean is not needed with SQLite storage."));
    notify(executor, T("Attribute numbers are indexed keys; gaps cost nothing."));
    return;
}

void vattr_delete_LEN(UTF8 *pName, size_t nName)
{
    string key(reinterpret_cast<const char *>(pName), nName);
    auto it = mudstate.vattr_name_map.find(key);
    if (it != mudstate.vattr_name_map.end())
    {
        int anum = it->second;
        ATTR *vp = static_cast<ATTR *>(anum_table[anum]);
        anum_set(anum, nullptr);
        mudstate.vattr_name_map.erase(it);
        if (  !mudstate.bSQLiteLoading
           && anum >= A_USER_START)
        {
            g_pSQLiteBackend->GetDB().DelAttrName(anum);
        }
        MEMFREE(vp);
    }
}

ATTR *vattr_rename_LEN(UTF8 *pOldName, size_t nOldName, UTF8 *pNewName, size_t nNewName)
{
    string oldkey(reinterpret_cast<const char *>(pOldName), nOldName);
    auto it = mudstate.vattr_name_map.find(oldkey);
    if (it == mudstate.vattr_name_map.end())
    {
        return nullptr;
    }

    int anum = it->second;
    ATTR *vp = static_cast<ATTR *>(anum_table[anum]);

    string newkey(reinterpret_cast<const char *>(pNewName), nNewName);
    auto existing = mudstate.vattr_name_map.find(newkey);
    if (  existing != mudstate.vattr_name_map.end()
       && existing->second != anum)
    {
        return nullptr;
    }

    if (newkey == oldkey)
    {
        return vp;
    }

    mudstate.vattr_name_map.erase(it);

    auto [newit, inserted] = mudstate.vattr_name_map.emplace(move(newkey), anum);
    if (!inserted)
    {
        mudstate.vattr_name_map.emplace(oldkey, anum);
        return nullptr;
    }
    vp->name = reinterpret_cast<const UTF8 *>(newit->first.c_str());

    if (  !mudstate.bSQLiteLoading
       && anum >= A_USER_START)
    {
        g_pSQLiteBackend->GetDB().PutAttrName(anum,
            reinterpret_cast<const char *>(pNewName), vp->flags);
    }
    return vp;
}

ATTR *vattr_first(void)
{
    int best = INT_MAX;
    for (const auto& entry : mudstate.vattr_name_map)
    {
        if (entry.second > 0 && entry.second < best)
        {
            best = entry.second;
        }
    }
    return (best == INT_MAX) ? nullptr : static_cast<ATTR *>(anum_table[best]);
}

ATTR *vattr_next(ATTR *vp)
{
    if (vp == nullptr)
    {
        return vattr_first();
    }

    int best = INT_MAX;
    for (const auto& entry : mudstate.vattr_name_map)
    {
        if (  entry.second > vp->number
           && entry.second < best)
        {
            best = entry.second;
        }
    }
    return (best == INT_MAX) ? nullptr : static_cast<ATTR *>(anum_table[best]);
}
