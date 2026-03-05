/*! \file sqlitedb.cpp
 * \brief SQLite storage backend for TinyMUX.
 *
 */

#include "sqlitedb.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

CSQLiteDB::CSQLiteDB()
    : m_db(nullptr),
      m_stmtObjInsert(nullptr),
      m_stmtObjDelete(nullptr),
      m_stmtObjLoad(nullptr),
      m_stmtObjLoadAll(nullptr),
      m_stmtUpdateLocation(nullptr),
      m_stmtUpdateContents(nullptr),
      m_stmtUpdateExits(nullptr),
      m_stmtUpdateNext(nullptr),
      m_stmtUpdateLink(nullptr),
      m_stmtUpdateOwner(nullptr),
      m_stmtUpdateParent(nullptr),
      m_stmtUpdateZone(nullptr),
      m_stmtUpdatePennies(nullptr),
      m_stmtUpdateFlags(nullptr),
      m_stmtUpdatePowers(nullptr),
      m_stmtAttrGet(nullptr),
      m_stmtAttrPut(nullptr),
      m_stmtAttrDel(nullptr),
      m_stmtAttrDelObj(nullptr),
      m_stmtAttrGetObj(nullptr),
      m_stats{}
{
}

CSQLiteDB::~CSQLiteDB()
{
    Close();
}

// ---------------------------------------------------------------------------
// Database lifecycle
// ---------------------------------------------------------------------------

bool CSQLiteDB::Open(const char *path)
{
    if (m_db)
    {
        return false;
    }

    int rc = sqlite3_open(path, &m_db);
    if (SQLITE_OK != rc)
    {
        fprintf(stderr, "CSQLiteDB::Open: %s\n", sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    if (!ConfigurePragmas())
    {
        Close();
        return false;
    }

    if (!CreateSchema())
    {
        Close();
        return false;
    }

    if (!PrepareStatements())
    {
        Close();
        return false;
    }

    return true;
}

void CSQLiteDB::Close()
{
    if (m_db)
    {
        FinalizeStatements();
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool CSQLiteDB::ConfigurePragmas()
{
    const char *pragmas[] =
    {
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA mmap_size=268435456",
        "PRAGMA page_size=4096",
        "PRAGMA cache_size=-65536",
        "PRAGMA foreign_keys=OFF",
        nullptr
    };

    for (int i = 0; pragmas[i]; i++)
    {
        char *errmsg = nullptr;
        int rc = sqlite3_exec(m_db, pragmas[i], nullptr, nullptr, &errmsg);
        if (SQLITE_OK != rc)
        {
            fprintf(stderr, "CSQLiteDB::ConfigurePragmas: %s: %s\n",
                pragmas[i], errmsg ? errmsg : "unknown error");
            sqlite3_free(errmsg);
            return false;
        }
    }
    return true;
}

bool CSQLiteDB::CreateSchema()
{
    const char *schema =
        "CREATE TABLE IF NOT EXISTS objects ("
        "    dbref       INTEGER PRIMARY KEY,"
        "    location    INTEGER NOT NULL DEFAULT -1,"
        "    contents    INTEGER NOT NULL DEFAULT -1,"
        "    exits       INTEGER NOT NULL DEFAULT -1,"
        "    next        INTEGER NOT NULL DEFAULT -1,"
        "    link        INTEGER NOT NULL DEFAULT -1,"
        "    owner       INTEGER NOT NULL DEFAULT -1,"
        "    parent      INTEGER NOT NULL DEFAULT -1,"
        "    zone        INTEGER NOT NULL DEFAULT -1,"
        "    pennies     INTEGER NOT NULL DEFAULT 0,"
        "    flags1      INTEGER NOT NULL DEFAULT 0,"
        "    flags2      INTEGER NOT NULL DEFAULT 0,"
        "    flags3      INTEGER NOT NULL DEFAULT 0,"
        "    powers1     INTEGER NOT NULL DEFAULT 0,"
        "    powers2     INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS attributes ("
        "    object      INTEGER NOT NULL,"
        "    attrnum     INTEGER NOT NULL,"
        "    value       BLOB NOT NULL,"
        "    PRIMARY KEY (object, attrnum)"
        ") WITHOUT ROWID;";

    char *errmsg = nullptr;
    int rc = sqlite3_exec(m_db, schema, nullptr, nullptr, &errmsg);
    if (SQLITE_OK != rc)
    {
        fprintf(stderr, "CSQLiteDB::CreateSchema: %s\n",
            errmsg ? errmsg : "unknown error");
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Prepared statement management
// ---------------------------------------------------------------------------

static bool Prepare(sqlite3 *db, const char *sql, sqlite3_stmt **ppStmt)
{
    int rc = sqlite3_prepare_v2(db, sql, -1, ppStmt, nullptr);
    if (SQLITE_OK != rc)
    {
        fprintf(stderr, "CSQLiteDB::Prepare: %s\n  SQL: %s\n",
            sqlite3_errmsg(db), sql);
        return false;
    }
    return true;
}

bool CSQLiteDB::PrepareStatements()
{
    // Object metadata.
    //
    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO objects "
        "(dbref, location, contents, exits, next, link, owner, parent, zone, "
        "pennies, flags1, flags2, flags3, powers1, powers2) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        &m_stmtObjInsert))
    {
        return false;
    }

    if (!Prepare(m_db,
        "DELETE FROM objects WHERE dbref=?",
        &m_stmtObjDelete))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT location, contents, exits, next, link, owner, parent, zone, "
        "pennies, flags1, flags2, flags3, powers1, powers2 "
        "FROM objects WHERE dbref=?",
        &m_stmtObjLoad))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT dbref, location, contents, exits, next, link, owner, parent, zone, "
        "pennies, flags1, flags2, flags3, powers1, powers2 "
        "FROM objects ORDER BY dbref",
        &m_stmtObjLoadAll))
    {
        return false;
    }

    // Individual field updates.
    //
    if (!Prepare(m_db, "UPDATE objects SET location=? WHERE dbref=?", &m_stmtUpdateLocation)) return false;
    if (!Prepare(m_db, "UPDATE objects SET contents=? WHERE dbref=?", &m_stmtUpdateContents)) return false;
    if (!Prepare(m_db, "UPDATE objects SET exits=? WHERE dbref=?", &m_stmtUpdateExits)) return false;
    if (!Prepare(m_db, "UPDATE objects SET next=? WHERE dbref=?", &m_stmtUpdateNext)) return false;
    if (!Prepare(m_db, "UPDATE objects SET link=? WHERE dbref=?", &m_stmtUpdateLink)) return false;
    if (!Prepare(m_db, "UPDATE objects SET owner=? WHERE dbref=?", &m_stmtUpdateOwner)) return false;
    if (!Prepare(m_db, "UPDATE objects SET parent=? WHERE dbref=?", &m_stmtUpdateParent)) return false;
    if (!Prepare(m_db, "UPDATE objects SET zone=? WHERE dbref=?", &m_stmtUpdateZone)) return false;
    if (!Prepare(m_db, "UPDATE objects SET pennies=? WHERE dbref=?", &m_stmtUpdatePennies)) return false;
    if (!Prepare(m_db, "UPDATE objects SET flags1=?, flags2=?, flags3=? WHERE dbref=?", &m_stmtUpdateFlags)) return false;
    if (!Prepare(m_db, "UPDATE objects SET powers1=?, powers2=? WHERE dbref=?", &m_stmtUpdatePowers)) return false;

    // Attribute operations.
    //
    if (!Prepare(m_db,
        "SELECT value FROM attributes WHERE object=? AND attrnum=?",
        &m_stmtAttrGet))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO attributes (object, attrnum, value) VALUES (?,?,?)",
        &m_stmtAttrPut))
    {
        return false;
    }

    if (!Prepare(m_db,
        "DELETE FROM attributes WHERE object=? AND attrnum=?",
        &m_stmtAttrDel))
    {
        return false;
    }

    if (!Prepare(m_db,
        "DELETE FROM attributes WHERE object=?",
        &m_stmtAttrDelObj))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT attrnum, value FROM attributes WHERE object=?",
        &m_stmtAttrGetObj))
    {
        return false;
    }

    return true;
}

static void Finalize(sqlite3_stmt **ppStmt)
{
    if (*ppStmt)
    {
        sqlite3_finalize(*ppStmt);
        *ppStmt = nullptr;
    }
}

void CSQLiteDB::FinalizeStatements()
{
    Finalize(&m_stmtObjInsert);
    Finalize(&m_stmtObjDelete);
    Finalize(&m_stmtObjLoad);
    Finalize(&m_stmtObjLoadAll);
    Finalize(&m_stmtUpdateLocation);
    Finalize(&m_stmtUpdateContents);
    Finalize(&m_stmtUpdateExits);
    Finalize(&m_stmtUpdateNext);
    Finalize(&m_stmtUpdateLink);
    Finalize(&m_stmtUpdateOwner);
    Finalize(&m_stmtUpdateParent);
    Finalize(&m_stmtUpdateZone);
    Finalize(&m_stmtUpdatePennies);
    Finalize(&m_stmtUpdateFlags);
    Finalize(&m_stmtUpdatePowers);
    Finalize(&m_stmtAttrGet);
    Finalize(&m_stmtAttrPut);
    Finalize(&m_stmtAttrDel);
    Finalize(&m_stmtAttrDelObj);
    Finalize(&m_stmtAttrGetObj);
}

// ---------------------------------------------------------------------------
// Object metadata operations
// ---------------------------------------------------------------------------

bool CSQLiteDB::InsertObject(const ObjectRecord &obj)
{
    sqlite3_bind_int(m_stmtObjInsert, 1, obj.dbref_val);
    sqlite3_bind_int(m_stmtObjInsert, 2, obj.location);
    sqlite3_bind_int(m_stmtObjInsert, 3, obj.contents);
    sqlite3_bind_int(m_stmtObjInsert, 4, obj.exits);
    sqlite3_bind_int(m_stmtObjInsert, 5, obj.next);
    sqlite3_bind_int(m_stmtObjInsert, 6, obj.link);
    sqlite3_bind_int(m_stmtObjInsert, 7, obj.owner);
    sqlite3_bind_int(m_stmtObjInsert, 8, obj.parent);
    sqlite3_bind_int(m_stmtObjInsert, 9, obj.zone);
    sqlite3_bind_int(m_stmtObjInsert, 10, obj.pennies);
    sqlite3_bind_int(m_stmtObjInsert, 11, static_cast<int>(obj.flags1));
    sqlite3_bind_int(m_stmtObjInsert, 12, static_cast<int>(obj.flags2));
    sqlite3_bind_int(m_stmtObjInsert, 13, static_cast<int>(obj.flags3));
    sqlite3_bind_int(m_stmtObjInsert, 14, obj.powers1);
    sqlite3_bind_int(m_stmtObjInsert, 15, obj.powers2);

    int rc = sqlite3_step(m_stmtObjInsert);
    sqlite3_reset(m_stmtObjInsert);

    if (SQLITE_DONE == rc)
    {
        m_stats.obj_inserts++;
        return true;
    }
    fprintf(stderr, "CSQLiteDB::InsertObject(#%d): %s\n",
        obj.dbref_val, sqlite3_errmsg(m_db));
    return false;
}

bool CSQLiteDB::DeleteObject(dbref obj)
{
    sqlite3_bind_int(m_stmtObjDelete, 1, obj);
    int rc = sqlite3_step(m_stmtObjDelete);
    sqlite3_reset(m_stmtObjDelete);

    if (SQLITE_DONE == rc)
    {
        m_stats.obj_deletes++;
        return true;
    }
    return false;
}

bool CSQLiteDB::LoadObject(dbref obj, ObjectRecord &rec)
{
    sqlite3_bind_int(m_stmtObjLoad, 1, obj);
    int rc = sqlite3_step(m_stmtObjLoad);

    if (SQLITE_ROW == rc)
    {
        rec.dbref_val = obj;
        rec.location  = sqlite3_column_int(m_stmtObjLoad, 0);
        rec.contents  = sqlite3_column_int(m_stmtObjLoad, 1);
        rec.exits     = sqlite3_column_int(m_stmtObjLoad, 2);
        rec.next      = sqlite3_column_int(m_stmtObjLoad, 3);
        rec.link      = sqlite3_column_int(m_stmtObjLoad, 4);
        rec.owner     = sqlite3_column_int(m_stmtObjLoad, 5);
        rec.parent    = sqlite3_column_int(m_stmtObjLoad, 6);
        rec.zone      = sqlite3_column_int(m_stmtObjLoad, 7);
        rec.pennies   = sqlite3_column_int(m_stmtObjLoad, 8);
        rec.flags1    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoad, 9));
        rec.flags2    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoad, 10));
        rec.flags3    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoad, 11));
        rec.powers1   = sqlite3_column_int(m_stmtObjLoad, 12);
        rec.powers2   = sqlite3_column_int(m_stmtObjLoad, 13);
        sqlite3_reset(m_stmtObjLoad);
        m_stats.obj_loads++;
        return true;
    }

    sqlite3_reset(m_stmtObjLoad);
    return false;
}

bool CSQLiteDB::LoadAllObjects(ObjectCallback cb)
{
    for (;;)
    {
        int rc = sqlite3_step(m_stmtObjLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        ObjectRecord rec;
        rec.dbref_val = sqlite3_column_int(m_stmtObjLoadAll, 0);
        rec.location  = sqlite3_column_int(m_stmtObjLoadAll, 1);
        rec.contents  = sqlite3_column_int(m_stmtObjLoadAll, 2);
        rec.exits     = sqlite3_column_int(m_stmtObjLoadAll, 3);
        rec.next      = sqlite3_column_int(m_stmtObjLoadAll, 4);
        rec.link      = sqlite3_column_int(m_stmtObjLoadAll, 5);
        rec.owner     = sqlite3_column_int(m_stmtObjLoadAll, 6);
        rec.parent    = sqlite3_column_int(m_stmtObjLoadAll, 7);
        rec.zone      = sqlite3_column_int(m_stmtObjLoadAll, 8);
        rec.pennies   = sqlite3_column_int(m_stmtObjLoadAll, 9);
        rec.flags1    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoadAll, 10));
        rec.flags2    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoadAll, 11));
        rec.flags3    = static_cast<FLAG>(sqlite3_column_int(m_stmtObjLoadAll, 12));
        rec.powers1   = sqlite3_column_int(m_stmtObjLoadAll, 13);
        rec.powers2   = sqlite3_column_int(m_stmtObjLoadAll, 14);

        m_stats.obj_loads++;
        cb(rec);
    }

    sqlite3_reset(m_stmtObjLoadAll);
    return true;
}

// ---------------------------------------------------------------------------
// Individual field updates
// ---------------------------------------------------------------------------

bool CSQLiteDB::UpdateSingleField(sqlite3_stmt *stmt, dbref obj, int val)
{
    sqlite3_bind_int(stmt, 1, val);
    sqlite3_bind_int(stmt, 2, obj);
    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);

    if (SQLITE_DONE == rc)
    {
        m_stats.obj_updates++;
        return true;
    }
    return false;
}

bool CSQLiteDB::UpdateLocation(dbref obj, dbref val) { return UpdateSingleField(m_stmtUpdateLocation, obj, val); }
bool CSQLiteDB::UpdateContents(dbref obj, dbref val) { return UpdateSingleField(m_stmtUpdateContents, obj, val); }
bool CSQLiteDB::UpdateExits(dbref obj, dbref val)    { return UpdateSingleField(m_stmtUpdateExits, obj, val); }
bool CSQLiteDB::UpdateNext(dbref obj, dbref val)     { return UpdateSingleField(m_stmtUpdateNext, obj, val); }
bool CSQLiteDB::UpdateLink(dbref obj, dbref val)     { return UpdateSingleField(m_stmtUpdateLink, obj, val); }
bool CSQLiteDB::UpdateOwner(dbref obj, dbref val)    { return UpdateSingleField(m_stmtUpdateOwner, obj, val); }
bool CSQLiteDB::UpdateParent(dbref obj, dbref val)   { return UpdateSingleField(m_stmtUpdateParent, obj, val); }
bool CSQLiteDB::UpdateZone(dbref obj, dbref val)     { return UpdateSingleField(m_stmtUpdateZone, obj, val); }
bool CSQLiteDB::UpdatePennies(dbref obj, int val)    { return UpdateSingleField(m_stmtUpdatePennies, obj, val); }

bool CSQLiteDB::UpdateFlags(dbref obj, FLAG f1, FLAG f2, FLAG f3)
{
    sqlite3_bind_int(m_stmtUpdateFlags, 1, static_cast<int>(f1));
    sqlite3_bind_int(m_stmtUpdateFlags, 2, static_cast<int>(f2));
    sqlite3_bind_int(m_stmtUpdateFlags, 3, static_cast<int>(f3));
    sqlite3_bind_int(m_stmtUpdateFlags, 4, obj);
    int rc = sqlite3_step(m_stmtUpdateFlags);
    sqlite3_reset(m_stmtUpdateFlags);

    if (SQLITE_DONE == rc)
    {
        m_stats.obj_updates++;
        return true;
    }
    return false;
}

bool CSQLiteDB::UpdatePowers(dbref obj, POWER p1, POWER p2)
{
    sqlite3_bind_int(m_stmtUpdatePowers, 1, p1);
    sqlite3_bind_int(m_stmtUpdatePowers, 2, p2);
    sqlite3_bind_int(m_stmtUpdatePowers, 3, obj);
    int rc = sqlite3_step(m_stmtUpdatePowers);
    sqlite3_reset(m_stmtUpdatePowers);

    if (SQLITE_DONE == rc)
    {
        m_stats.obj_updates++;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Attribute operations
// ---------------------------------------------------------------------------

bool CSQLiteDB::GetAttribute(dbref obj, int attrnum, UTF8 *buf, size_t buflen, size_t *pLen)
{
    sqlite3_bind_int(m_stmtAttrGet, 1, obj);
    sqlite3_bind_int(m_stmtAttrGet, 2, attrnum);

    int rc = sqlite3_step(m_stmtAttrGet);
    if (SQLITE_ROW == rc)
    {
        const void *blob = sqlite3_column_blob(m_stmtAttrGet, 0);
        size_t blobLen = static_cast<size_t>(sqlite3_column_bytes(m_stmtAttrGet, 0));

        if (blobLen > buflen)
        {
            blobLen = buflen;
        }
        memcpy(buf, blob, blobLen);
        *pLen = blobLen;
        sqlite3_reset(m_stmtAttrGet);
        m_stats.attr_gets++;
        return true;
    }

    sqlite3_reset(m_stmtAttrGet);
    *pLen = 0;
    return false;
}

bool CSQLiteDB::PutAttribute(dbref obj, int attrnum, const UTF8 *value, size_t len)
{
    sqlite3_bind_int(m_stmtAttrPut, 1, obj);
    sqlite3_bind_int(m_stmtAttrPut, 2, attrnum);
    sqlite3_bind_blob(m_stmtAttrPut, 3, value, static_cast<int>(len), SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtAttrPut);
    sqlite3_reset(m_stmtAttrPut);

    if (SQLITE_DONE == rc)
    {
        m_stats.attr_puts++;
        return true;
    }
    fprintf(stderr, "CSQLiteDB::PutAttribute(#%d/%d): %s\n",
        obj, attrnum, sqlite3_errmsg(m_db));
    return false;
}

bool CSQLiteDB::DelAttribute(dbref obj, int attrnum)
{
    sqlite3_bind_int(m_stmtAttrDel, 1, obj);
    sqlite3_bind_int(m_stmtAttrDel, 2, attrnum);

    int rc = sqlite3_step(m_stmtAttrDel);
    sqlite3_reset(m_stmtAttrDel);

    if (SQLITE_DONE == rc)
    {
        m_stats.attr_dels++;
        return true;
    }
    return false;
}

bool CSQLiteDB::DelAllAttributes(dbref obj)
{
    sqlite3_bind_int(m_stmtAttrDelObj, 1, obj);

    int rc = sqlite3_step(m_stmtAttrDelObj);
    sqlite3_reset(m_stmtAttrDelObj);

    if (SQLITE_DONE == rc)
    {
        m_stats.attr_dels++;
        return true;
    }
    return false;
}

bool CSQLiteDB::GetAllAttributes(dbref obj, AttrCallback cb)
{
    sqlite3_bind_int(m_stmtAttrGetObj, 1, obj);

    for (;;)
    {
        int rc = sqlite3_step(m_stmtAttrGetObj);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int attrnum = sqlite3_column_int(m_stmtAttrGetObj, 0);
        const UTF8 *value = static_cast<const UTF8 *>(sqlite3_column_blob(m_stmtAttrGetObj, 1));
        size_t len = static_cast<size_t>(sqlite3_column_bytes(m_stmtAttrGetObj, 1));

        cb(attrnum, value, len);
    }

    sqlite3_reset(m_stmtAttrGetObj);
    m_stats.attr_bulk_loads++;
    return true;
}

// ---------------------------------------------------------------------------
// Transaction support
// ---------------------------------------------------------------------------

bool CSQLiteDB::Begin()
{
    return SQLITE_OK == sqlite3_exec(m_db, "BEGIN", nullptr, nullptr, nullptr);
}

bool CSQLiteDB::Commit()
{
    return SQLITE_OK == sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
}

bool CSQLiteDB::Rollback()
{
    return SQLITE_OK == sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

bool CSQLiteDB::Checkpoint()
{
    int rc = sqlite3_wal_checkpoint_v2(m_db, nullptr,
        SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    return SQLITE_OK == rc;
}

bool CSQLiteDB::Optimize()
{
    return SQLITE_OK == sqlite3_exec(m_db, "PRAGMA optimize", nullptr, nullptr, nullptr);
}
