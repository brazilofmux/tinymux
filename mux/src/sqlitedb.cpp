/*! \file sqlitedb.cpp
 * \brief SQLite storage backend for TinyMUX.
 *
 */

#if !defined(TINYMUX_TYPES_DEFINED)
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#endif

#include "sqlitedb.h"
#include <cstring>
#include <cstdio>
#include <string>

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
      m_stmtAttrGetBuiltin(nullptr),
      m_stmtAttrNamePut(nullptr),
      m_stmtAttrNameDel(nullptr),
      m_stmtAttrNameLoadAll(nullptr),
      m_stmtMetaPut(nullptr),
      m_stmtMetaGet(nullptr),
      m_stmtChannelSync(nullptr),
      m_stmtChannelUserSync(nullptr),
      m_stmtPlayerChannelSync(nullptr),
      m_stmtChannelLoadAll(nullptr),
      m_stmtChannelUserLoadAll(nullptr),
      m_stmtPlayerChannelLoadAll(nullptr),
      m_stmtChannelDelete(nullptr),
      m_stmtChannelUserDelete(nullptr),
      m_stmtPlayerChannelDelete(nullptr),
      m_stmtPlayerChannelDeleteAll(nullptr),
      m_stmtMailHeaderSync(nullptr),
      m_stmtMailHeaderInsertRet(nullptr),
      m_stmtMailHeaderUpdateFlags(nullptr),
      m_stmtMailHeaderDelete(nullptr),
      m_stmtMailHeaderDeleteAll(nullptr),
      m_stmtMailBodySync(nullptr),
      m_stmtMailBodyDelete(nullptr),
      m_stmtMailAliasSync(nullptr),
      m_stmtMailHeaderLoadAll(nullptr),
      m_stmtMailBodyLoadAll(nullptr),
      m_stmtMailAliasLoadAll(nullptr),
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

    if (!MigrateSchema())
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
        "PRAGMA busy_timeout=5000",
        "PRAGMA mmap_size=268435456",
        "PRAGMA page_size=4096",
        "PRAGMA cache_size=-65536",
        "PRAGMA foreign_keys=ON",
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
        // Core tables (no foreign key dependencies).
        //
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
        "    owner       INTEGER NOT NULL DEFAULT -1,"
        "    flags       INTEGER NOT NULL DEFAULT 0,"
        "    PRIMARY KEY (object, attrnum)"
        ") WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS attrnames ("
        "    attrnum     INTEGER PRIMARY KEY,"
        "    name        TEXT NOT NULL,"
        "    flags       INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS metadata ("
        "    key         TEXT PRIMARY KEY,"
        "    value       INTEGER NOT NULL"
        ") WITHOUT ROWID;"

        // Comsys: channels first (referenced), then dependents with FKs.
        //
        "CREATE TABLE IF NOT EXISTS channels ("
        "    name        TEXT PRIMARY KEY,"
        "    header      TEXT NOT NULL DEFAULT '',"
        "    type        INTEGER NOT NULL DEFAULT 127,"
        "    temp1       INTEGER NOT NULL DEFAULT 0,"
        "    temp2       INTEGER NOT NULL DEFAULT 0,"
        "    charge      INTEGER NOT NULL DEFAULT 0,"
        "    charge_who  INTEGER NOT NULL DEFAULT -1,"
        "    amount_col  INTEGER NOT NULL DEFAULT 0,"
        "    num_messages INTEGER NOT NULL DEFAULT 0,"
        "    chan_obj    INTEGER NOT NULL DEFAULT -1"
        ") WITHOUT ROWID;"

        "CREATE TABLE IF NOT EXISTS channel_users ("
        "    channel_name TEXT NOT NULL"
        "        REFERENCES channels(name) ON DELETE CASCADE ON UPDATE CASCADE,"
        "    who         INTEGER NOT NULL,"
        "    is_on       INTEGER NOT NULL DEFAULT 0,"
        "    comtitle_status INTEGER NOT NULL DEFAULT 0,"
        "    gag_join_leave INTEGER NOT NULL DEFAULT 0,"
        "    title       TEXT NOT NULL DEFAULT '',"
        "    PRIMARY KEY (channel_name, who)"
        ") WITHOUT ROWID;"

        "CREATE TABLE IF NOT EXISTS player_channels ("
        "    who         INTEGER NOT NULL,"
        "    alias       TEXT NOT NULL,"
        "    channel_name TEXT NOT NULL"
        "        REFERENCES channels(name) ON DELETE CASCADE ON UPDATE CASCADE,"
        "    PRIMARY KEY (who, alias)"
        ") WITHOUT ROWID;"

        // Mail: bodies first (referenced), then headers with FK.
        //
        "CREATE TABLE IF NOT EXISTS mail_bodies ("
        "    number      INTEGER PRIMARY KEY,"
        "    message     TEXT NOT NULL DEFAULT ''"
        ");"

        "CREATE TABLE IF NOT EXISTS mail_headers ("
        "    rowid       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    to_player   INTEGER NOT NULL,"
        "    from_player INTEGER NOT NULL,"
        "    body_number INTEGER NOT NULL REFERENCES mail_bodies(number),"
        "    tolist      TEXT NOT NULL DEFAULT '',"
        "    time_str    TEXT NOT NULL DEFAULT '',"
        "    subject     TEXT NOT NULL DEFAULT '',"
        "    read_flags  INTEGER NOT NULL DEFAULT 0"
        ");"

        "CREATE TABLE IF NOT EXISTS mail_aliases ("
        "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    owner       INTEGER NOT NULL,"
        "    name        TEXT NOT NULL,"
        "    description TEXT NOT NULL DEFAULT '',"
        "    desc_width  INTEGER NOT NULL DEFAULT 0,"
        "    members     TEXT NOT NULL DEFAULT ''"
        ");"

        // Secondary indexes for common query patterns.
        //
        "CREATE INDEX IF NOT EXISTS idx_objects_owner ON objects(owner);"
        "CREATE INDEX IF NOT EXISTS idx_objects_location ON objects(location);"
        "CREATE INDEX IF NOT EXISTS idx_objects_zone ON objects(zone);"
        "CREATE INDEX IF NOT EXISTS idx_objects_parent ON objects(parent);"
        "CREATE INDEX IF NOT EXISTS idx_mail_headers_to ON mail_headers(to_player);"
        "CREATE INDEX IF NOT EXISTS idx_channel_users_who ON channel_users(who);"
        "CREATE INDEX IF NOT EXISTS idx_player_channels_channel"
        "    ON player_channels(channel_name);";

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
// Schema migration
// ---------------------------------------------------------------------------

// Helper: run a migration SQL block with FK checks disabled.
//
static bool RunMigration(sqlite3 *db, const char *sql, int target_version)
{
    sqlite3_exec(db, "PRAGMA foreign_keys=OFF", nullptr, nullptr, nullptr);

    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (SQLITE_OK != rc)
    {
        fprintf(stderr, "CSQLiteDB::MigrateSchema v%d: %s\n",
            target_version, errmsg ? errmsg : "unknown error");
        sqlite3_free(errmsg);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_exec(db, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    fprintf(stderr, "CSQLiteDB::MigrateSchema: upgraded to schema version %d.\n",
        target_version);
    return true;
}

bool CSQLiteDB::MigrateSchema()
{
    static const int CURRENT_SCHEMA_VERSION = 5;

    int version = 0;
    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK == sqlite3_prepare_v2(m_db,
        "SELECT value FROM metadata WHERE key='schema_version'",
        -1, &stmt, nullptr))
    {
        if (SQLITE_ROW == sqlite3_step(stmt))
        {
            version = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (version >= CURRENT_SCHEMA_VERSION)
    {
        return true;
    }

    // ---------------------------------------------------------------
    // v0/v1 -> v2: add foreign keys and secondary indexes.
    // ---------------------------------------------------------------
    //
    if (version < 2)
    {
        const char *migration_v2 =
            "BEGIN;"

            "CREATE TABLE new_channel_users ("
            "    channel_name TEXT NOT NULL"
            "        REFERENCES channels(name) ON DELETE CASCADE ON UPDATE CASCADE,"
            "    who         INTEGER NOT NULL,"
            "    is_on       INTEGER NOT NULL DEFAULT 0,"
            "    comtitle_status INTEGER NOT NULL DEFAULT 0,"
            "    gag_join_leave INTEGER NOT NULL DEFAULT 0,"
            "    title       TEXT NOT NULL DEFAULT '',"
            "    PRIMARY KEY (channel_name, who)"
            ") WITHOUT ROWID;"
            "INSERT INTO new_channel_users"
            "    SELECT * FROM channel_users;"
            "DROP TABLE channel_users;"
            "ALTER TABLE new_channel_users RENAME TO channel_users;"

            "CREATE TABLE new_player_channels ("
            "    who         INTEGER NOT NULL,"
            "    alias       TEXT NOT NULL,"
            "    channel_name TEXT NOT NULL"
            "        REFERENCES channels(name) ON DELETE CASCADE ON UPDATE CASCADE,"
            "    PRIMARY KEY (who, alias)"
            ") WITHOUT ROWID;"
            "INSERT INTO new_player_channels"
            "    SELECT * FROM player_channels;"
            "DROP TABLE player_channels;"
            "ALTER TABLE new_player_channels RENAME TO player_channels;"

            "CREATE TABLE new_mail_headers ("
            "    rowid       INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    to_player   INTEGER NOT NULL,"
            "    from_player INTEGER NOT NULL,"
            "    body_number INTEGER NOT NULL REFERENCES mail_bodies(number),"
            "    tolist      TEXT NOT NULL DEFAULT '',"
            "    time_str    TEXT NOT NULL DEFAULT '',"
            "    subject     TEXT NOT NULL DEFAULT '',"
            "    read_flags  INTEGER NOT NULL DEFAULT 0"
            ");"
            "INSERT INTO new_mail_headers"
            "    (rowid, to_player, from_player, body_number,"
            "     tolist, time_str, subject, read_flags)"
            "    SELECT rowid, to_player, from_player, body_number,"
            "           tolist, time_str, subject, read_flags"
            "    FROM mail_headers;"
            "DROP TABLE mail_headers;"
            "ALTER TABLE new_mail_headers RENAME TO mail_headers;"

            "CREATE INDEX IF NOT EXISTS idx_objects_owner ON objects(owner);"
            "CREATE INDEX IF NOT EXISTS idx_objects_location ON objects(location);"
            "CREATE INDEX IF NOT EXISTS idx_objects_zone ON objects(zone);"
            "CREATE INDEX IF NOT EXISTS idx_mail_headers_to ON mail_headers(to_player);"
            "CREATE INDEX IF NOT EXISTS idx_channel_users_who ON channel_users(who);"
            "CREATE INDEX IF NOT EXISTS idx_player_channels_channel"
            "    ON player_channels(channel_name);"

            "INSERT OR REPLACE INTO metadata(key, value)"
            "    VALUES('schema_version', 2);"

            "COMMIT;";

        if (!RunMigration(m_db, migration_v2, 2))
        {
            return false;
        }
        version = 2;
    }

    // ---------------------------------------------------------------
    // v2 -> v3: add owner/flags columns to attributes table.
    // Recreate the table with new columns.  Existing blobs may contain
    // packed \x01owner:flags:text prefixes which we leave as-is here;
    // the runtime atr_add_raw_LEN() decodes and re-stores them on
    // first write.  For the cold-start path, all attributes get
    // decoded and re-stored with proper columns automatically.
    // ---------------------------------------------------------------
    //
    if (version < 3)
    {
        const char *migration_v3 =
            "BEGIN;"

            "CREATE TABLE new_attributes ("
            "    object      INTEGER NOT NULL,"
            "    attrnum     INTEGER NOT NULL,"
            "    value       BLOB NOT NULL,"
            "    owner       INTEGER NOT NULL DEFAULT -1,"
            "    flags       INTEGER NOT NULL DEFAULT 0,"
            "    PRIMARY KEY (object, attrnum)"
            ") WITHOUT ROWID;"
            "INSERT INTO new_attributes (object, attrnum, value)"
            "    SELECT object, attrnum, value FROM attributes;"
            "DROP TABLE attributes;"
            "ALTER TABLE new_attributes RENAME TO attributes;"

            "INSERT OR REPLACE INTO metadata(key, value)"
            "    VALUES('schema_version', 3);"

            "COMMIT;";

        if (!RunMigration(m_db, migration_v3, 3))
        {
            return false;
        }
        version = 3;
    }

    // ---------------------------------------------------------------
    // v3 -> v4: add parent index for @search optimization.
    // ---------------------------------------------------------------
    //
    if (version < 4)
    {
        const char *migration_v4 =
            "BEGIN;"
            "CREATE INDEX IF NOT EXISTS idx_objects_parent ON objects(parent);"
            "INSERT OR REPLACE INTO metadata(key, value)"
            "    VALUES('schema_version', 4);"
            "COMMIT;";

        if (!RunMigration(m_db, migration_v4, 4))
        {
            return false;
        }
        version = 4;
    }

    // ---------------------------------------------------------------
    // v4 -> v5: remove legacy packed attr-253 rows.
    // Attribute membership is derived relationally by (object, attrnum),
    // so attr 253 is redundant and should not be persisted.
    // ---------------------------------------------------------------
    //
    if (version < 5)
    {
        const char *migration_v5 =
            "BEGIN;"
            "DELETE FROM attributes WHERE attrnum=253;"
            "INSERT OR REPLACE INTO metadata(key, value)"
            "    VALUES('schema_version', 5);"
            "COMMIT;";

        if (!RunMigration(m_db, migration_v5, 5))
        {
            return false;
        }
        version = 5;
    }

    // Log any FK violations (informational, not fatal).
    //
    if (SQLITE_OK == sqlite3_prepare_v2(m_db,
        "PRAGMA foreign_key_check", -1, &stmt, nullptr))
    {
        bool first = true;
        while (SQLITE_ROW == sqlite3_step(stmt))
        {
            if (first)
            {
                fprintf(stderr, "CSQLiteDB::MigrateSchema: foreign key violations detected:\n");
                first = false;
            }
            fprintf(stderr, "  table=%s rowid=%lld parent=%s fkid=%d\n",
                sqlite3_column_text(stmt, 0),
                sqlite3_column_int64(stmt, 1),
                sqlite3_column_text(stmt, 2),
                sqlite3_column_int(stmt, 3));
        }
        sqlite3_finalize(stmt);
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
        "SELECT value, owner, flags FROM attributes WHERE object=? AND attrnum=?",
        &m_stmtAttrGet))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO attributes (object, attrnum, value, owner, flags) VALUES (?,?,?,?,?)",
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
        "SELECT attrnum, value, owner, flags FROM attributes WHERE object=? ORDER BY attrnum",
        &m_stmtAttrGetObj))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT attrnum, value, owner, flags FROM attributes WHERE object=? AND attrnum < 256 ORDER BY attrnum",
        &m_stmtAttrGetBuiltin))
    {
        return false;
    }

    // Attribute name registry.
    //
    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO attrnames (attrnum, name, flags) VALUES (?,?,?)",
        &m_stmtAttrNamePut))
    {
        return false;
    }

    if (!Prepare(m_db,
        "DELETE FROM attrnames WHERE attrnum=?",
        &m_stmtAttrNameDel))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT attrnum, name, flags FROM attrnames ORDER BY attrnum",
        &m_stmtAttrNameLoadAll))
    {
        return false;
    }

    // Metadata key-value store.
    //
    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO metadata (key, value) VALUES (?,?)",
        &m_stmtMetaPut))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT value FROM metadata WHERE key=?",
        &m_stmtMetaGet))
    {
        return false;
    }

    // Comsys statements.
    //
    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO channels "
        "(name, header, type, temp1, temp2, charge, charge_who, amount_col, num_messages, chan_obj) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)",
        &m_stmtChannelSync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO channel_users "
        "(channel_name, who, is_on, comtitle_status, gag_join_leave, title) "
        "VALUES (?,?,?,?,?,?)",
        &m_stmtChannelUserSync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO player_channels (who, alias, channel_name) "
        "VALUES (?,?,?)",
        &m_stmtPlayerChannelSync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT name, header, type, temp1, temp2, charge, charge_who, "
        "amount_col, num_messages, chan_obj FROM channels ORDER BY name",
        &m_stmtChannelLoadAll))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT channel_name, who, is_on, comtitle_status, gag_join_leave, title "
        "FROM channel_users ORDER BY channel_name, who",
        &m_stmtChannelUserLoadAll))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT who, alias, channel_name FROM player_channels ORDER BY who, alias",
        &m_stmtPlayerChannelLoadAll))
    {
        return false;
    }

    // Comsys write-through delete statements.
    //
    if (!Prepare(m_db, "DELETE FROM channels WHERE name=?", &m_stmtChannelDelete)) return false;
    if (!Prepare(m_db, "DELETE FROM channel_users WHERE channel_name=? AND who=?", &m_stmtChannelUserDelete)) return false;
    if (!Prepare(m_db, "DELETE FROM player_channels WHERE who=? AND alias=?", &m_stmtPlayerChannelDelete)) return false;
    if (!Prepare(m_db, "DELETE FROM player_channels WHERE who=?", &m_stmtPlayerChannelDeleteAll)) return false;

    // Mail statements.
    //
    if (!Prepare(m_db,
        "INSERT INTO mail_headers "
        "(to_player, from_player, body_number, tolist, time_str, subject, read_flags) "
        "VALUES (?,?,?,?,?,?,?)",
        &m_stmtMailHeaderSync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT OR REPLACE INTO mail_bodies (number, message) VALUES (?,?)",
        &m_stmtMailBodySync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "INSERT INTO mail_headers "
        "(to_player, from_player, body_number, tolist, time_str, subject, read_flags) "
        "VALUES (?,?,?,?,?,?,?)",
        &m_stmtMailHeaderInsertRet))
    {
        return false;
    }

    if (!Prepare(m_db, "UPDATE mail_headers SET read_flags=? WHERE rowid=?", &m_stmtMailHeaderUpdateFlags)) return false;
    if (!Prepare(m_db, "DELETE FROM mail_headers WHERE rowid=?", &m_stmtMailHeaderDelete)) return false;
    if (!Prepare(m_db, "DELETE FROM mail_headers WHERE to_player=?", &m_stmtMailHeaderDeleteAll)) return false;
    if (!Prepare(m_db, "DELETE FROM mail_bodies WHERE number=?", &m_stmtMailBodyDelete)) return false;

    if (!Prepare(m_db,
        "INSERT INTO mail_aliases (owner, name, description, desc_width, members) "
        "VALUES (?,?,?,?,?)",
        &m_stmtMailAliasSync))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT rowid, to_player, from_player, body_number, tolist, time_str, subject, read_flags "
        "FROM mail_headers ORDER BY rowid",
        &m_stmtMailHeaderLoadAll))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT number, message FROM mail_bodies ORDER BY number",
        &m_stmtMailBodyLoadAll))
    {
        return false;
    }

    if (!Prepare(m_db,
        "SELECT owner, name, description, desc_width, members "
        "FROM mail_aliases ORDER BY id",
        &m_stmtMailAliasLoadAll))
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
    Finalize(&m_stmtAttrGetBuiltin);
    Finalize(&m_stmtAttrNamePut);
    Finalize(&m_stmtAttrNameDel);
    Finalize(&m_stmtAttrNameLoadAll);
    Finalize(&m_stmtMetaPut);
    Finalize(&m_stmtMetaGet);
    Finalize(&m_stmtChannelSync);
    Finalize(&m_stmtChannelUserSync);
    Finalize(&m_stmtPlayerChannelSync);
    Finalize(&m_stmtChannelLoadAll);
    Finalize(&m_stmtChannelUserLoadAll);
    Finalize(&m_stmtPlayerChannelLoadAll);
    Finalize(&m_stmtChannelDelete);
    Finalize(&m_stmtChannelUserDelete);
    Finalize(&m_stmtPlayerChannelDelete);
    Finalize(&m_stmtPlayerChannelDeleteAll);
    Finalize(&m_stmtMailHeaderSync);
    Finalize(&m_stmtMailHeaderInsertRet);
    Finalize(&m_stmtMailHeaderUpdateFlags);
    Finalize(&m_stmtMailHeaderDelete);
    Finalize(&m_stmtMailHeaderDeleteAll);
    Finalize(&m_stmtMailBodySync);
    Finalize(&m_stmtMailBodyDelete);
    Finalize(&m_stmtMailAliasSync);
    Finalize(&m_stmtMailHeaderLoadAll);
    Finalize(&m_stmtMailBodyLoadAll);
    Finalize(&m_stmtMailAliasLoadAll);
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
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtObjLoadAll);
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
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllObjects: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
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

bool CSQLiteDB::GetAttribute(dbref obj, int attrnum, UTF8 *buf, size_t buflen,
                             size_t *pLen, dbref *owner, int *flags)
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
        *owner = static_cast<dbref>(sqlite3_column_int(m_stmtAttrGet, 1));
        *flags = sqlite3_column_int(m_stmtAttrGet, 2);
        sqlite3_reset(m_stmtAttrGet);
        m_stats.attr_gets++;
        return true;
    }

    sqlite3_reset(m_stmtAttrGet);
    *pLen = 0;
    *owner = NOTHING;
    *flags = 0;
    return false;
}

bool CSQLiteDB::PutAttribute(dbref obj, int attrnum, const UTF8 *value, size_t len,
                             dbref owner, int flags)
{
    sqlite3_bind_int(m_stmtAttrPut, 1, obj);
    sqlite3_bind_int(m_stmtAttrPut, 2, attrnum);
    sqlite3_bind_blob(m_stmtAttrPut, 3, value, static_cast<int>(len), SQLITE_STATIC);
    sqlite3_bind_int(m_stmtAttrPut, 4, owner);
    sqlite3_bind_int(m_stmtAttrPut, 5, flags);

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

    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtAttrGetObj);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int attrnum = sqlite3_column_int(m_stmtAttrGetObj, 0);
        const UTF8 *value = static_cast<const UTF8 *>(sqlite3_column_blob(m_stmtAttrGetObj, 1));
        size_t len = static_cast<size_t>(sqlite3_column_bytes(m_stmtAttrGetObj, 1));
        dbref owner = static_cast<dbref>(sqlite3_column_int(m_stmtAttrGetObj, 2));
        int flags = sqlite3_column_int(m_stmtAttrGetObj, 3);

        cb(attrnum, value, len, owner, flags);
    }

    sqlite3_reset(m_stmtAttrGetObj);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::GetAllAttributes(#%d): %s\n", obj, sqlite3_errmsg(m_db));
        return false;
    }
    m_stats.attr_bulk_loads++;
    return true;
}

bool CSQLiteDB::GetBuiltinAttributes(dbref obj, AttrCallback cb)
{
    sqlite3_bind_int(m_stmtAttrGetBuiltin, 1, obj);

    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtAttrGetBuiltin);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int attrnum = sqlite3_column_int(m_stmtAttrGetBuiltin, 0);
        const UTF8 *value = static_cast<const UTF8 *>(sqlite3_column_blob(m_stmtAttrGetBuiltin, 1));
        size_t len = static_cast<size_t>(sqlite3_column_bytes(m_stmtAttrGetBuiltin, 1));
        dbref owner = static_cast<dbref>(sqlite3_column_int(m_stmtAttrGetBuiltin, 2));
        int flags = sqlite3_column_int(m_stmtAttrGetBuiltin, 3);

        cb(attrnum, value, len, owner, flags);
    }

    sqlite3_reset(m_stmtAttrGetBuiltin);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::GetBuiltinAttributes(#%d): %s\n", obj, sqlite3_errmsg(m_db));
        return false;
    }
    m_stats.attr_bulk_loads++;
    return true;
}

// ---------------------------------------------------------------------------
// Attribute name registry
// ---------------------------------------------------------------------------

bool CSQLiteDB::PutAttrName(int attrnum, const char *name, int flags)
{
    sqlite3_bind_int(m_stmtAttrNamePut, 1, attrnum);
    sqlite3_bind_text(m_stmtAttrNamePut, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtAttrNamePut, 3, flags);

    int rc = sqlite3_step(m_stmtAttrNamePut);
    sqlite3_reset(m_stmtAttrNamePut);

    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DelAttrName(int attrnum)
{
    sqlite3_bind_int(m_stmtAttrNameDel, 1, attrnum);

    int rc = sqlite3_step(m_stmtAttrNameDel);
    sqlite3_reset(m_stmtAttrNameDel);

    return SQLITE_DONE == rc;
}

bool CSQLiteDB::LoadAllAttrNames(AttrNameCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtAttrNameLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int attrnum = sqlite3_column_int(m_stmtAttrNameLoadAll, 0);
        const char *name = reinterpret_cast<const char *>(
            sqlite3_column_text(m_stmtAttrNameLoadAll, 1));
        int flags = sqlite3_column_int(m_stmtAttrNameLoadAll, 2);

        cb(attrnum, name, flags);
    }

    sqlite3_reset(m_stmtAttrNameLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllAttrNames: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Metadata key-value store
// ---------------------------------------------------------------------------

bool CSQLiteDB::PutMeta(const char *key, int value)
{
    sqlite3_bind_text(m_stmtMetaPut, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtMetaPut, 2, value);

    int rc = sqlite3_step(m_stmtMetaPut);
    sqlite3_reset(m_stmtMetaPut);

    return SQLITE_DONE == rc;
}

CSQLiteDB::MetaGetResult CSQLiteDB::GetMetaEx(const char *key, int *value)
{
    sqlite3_bind_text(m_stmtMetaGet, 1, key, -1, SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtMetaGet);
    if (SQLITE_ROW == rc)
    {
        *value = sqlite3_column_int(m_stmtMetaGet, 0);
        sqlite3_reset(m_stmtMetaGet);
        return MetaGetResult::Found;
    }

    sqlite3_reset(m_stmtMetaGet);
    *value = 0;
    if (SQLITE_DONE == rc)
    {
        return MetaGetResult::NotFound;
    }
    fprintf(stderr, "CSQLiteDB::GetMeta(%s): %s\n", key, sqlite3_errmsg(m_db));
    return MetaGetResult::Error;
}

bool CSQLiteDB::GetMeta(const char *key, int *value)
{
    return MetaGetResult::Found == GetMetaEx(key, value);
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
// Search queries
// ---------------------------------------------------------------------------

// Helper: prepare an ad-hoc search query, bind params, iterate results,
// call back with each matching dbref, then finalize.  Search queries are
// infrequent (once per @search command), so the prepare/finalize overhead
// is negligible.
//
static bool RunSearch(sqlite3 *db, const char *sql,
                      const std::function<void(sqlite3_stmt *)> &bind,
                      CSQLiteDB::SearchCallback cb)
{
    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr))
    {
        return false;
    }

    bind(stmt);

    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(stmt);
        if (SQLITE_ROW != rc)
        {
            break;
        }
        cb(static_cast<dbref>(sqlite3_column_int(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::SearchByOwner(dbref owner, int type, dbref low, dbref high,
                              SearchCallback cb)
{
    if (type != -1)
    {
        // Owner + type filter.
        //
        return RunSearch(m_db,
            "SELECT dbref FROM objects"
            " WHERE owner=? AND (flags1 & 7)=? AND (flags1 & 16384)=0"
            " AND dbref BETWEEN ? AND ?"
            " ORDER BY dbref",
            [owner, type, low, high](sqlite3_stmt *s)
            {
                sqlite3_bind_int(s, 1, owner);
                sqlite3_bind_int(s, 2, type);
                sqlite3_bind_int(s, 3, low);
                sqlite3_bind_int(s, 4, high);
            }, cb);
    }

    return RunSearch(m_db,
        "SELECT dbref FROM objects"
        " WHERE owner=? AND (flags1 & 16384)=0"
        " AND dbref BETWEEN ? AND ?"
        " ORDER BY dbref",
        [owner, low, high](sqlite3_stmt *s)
        {
            sqlite3_bind_int(s, 1, owner);
            sqlite3_bind_int(s, 2, low);
            sqlite3_bind_int(s, 3, high);
        }, cb);
}

bool CSQLiteDB::SearchByType(int type, dbref low, dbref high, SearchCallback cb)
{
    return RunSearch(m_db,
        "SELECT dbref FROM objects"
        " WHERE (flags1 & 7)=? AND (flags1 & 16384)=0"
        " AND dbref BETWEEN ? AND ?"
        " ORDER BY dbref",
        [type, low, high](sqlite3_stmt *s)
        {
            sqlite3_bind_int(s, 1, type);
            sqlite3_bind_int(s, 2, low);
            sqlite3_bind_int(s, 3, high);
        }, cb);
}

bool CSQLiteDB::SearchByZone(dbref zone, dbref low, dbref high, SearchCallback cb)
{
    return RunSearch(m_db,
        "SELECT dbref FROM objects"
        " WHERE zone=? AND (flags1 & 16384)=0"
        " AND dbref BETWEEN ? AND ?"
        " ORDER BY dbref",
        [zone, low, high](sqlite3_stmt *s)
        {
            sqlite3_bind_int(s, 1, zone);
            sqlite3_bind_int(s, 2, low);
            sqlite3_bind_int(s, 3, high);
        }, cb);
}

bool CSQLiteDB::SearchByParent(dbref parent, dbref low, dbref high, SearchCallback cb)
{
    return RunSearch(m_db,
        "SELECT dbref FROM objects"
        " WHERE parent=? AND (flags1 & 16384)=0"
        " AND dbref BETWEEN ? AND ?"
        " ORDER BY dbref",
        [parent, low, high](sqlite3_stmt *s)
        {
            sqlite3_bind_int(s, 1, parent);
            sqlite3_bind_int(s, 2, low);
            sqlite3_bind_int(s, 3, high);
        }, cb);
}

bool CSQLiteDB::SearchByFlags(FLAG f1, FLAG f2, FLAG f3,
                              dbref low, dbref high, SearchCallback cb)
{
    // Build the query dynamically based on which flag words are non-zero.
    // Always exclude GOING.  The flags check is (flags & mask) == mask.
    //
    std::string sql = "SELECT dbref FROM objects WHERE (flags1 & 16384)=0";

    if (f1)
    {
        sql += " AND (flags1 & ?) = ?";
    }
    if (f2)
    {
        sql += " AND (flags2 & ?) = ?";
    }
    if (f3)
    {
        sql += " AND (flags3 & ?) = ?";
    }
    sql += " AND dbref BETWEEN ? AND ? ORDER BY dbref";

    return RunSearch(m_db, sql.c_str(),
        [f1, f2, f3, low, high](sqlite3_stmt *s)
        {
            int p = 1;
            if (f1)
            {
                sqlite3_bind_int(s, p++, static_cast<int>(f1));
                sqlite3_bind_int(s, p++, static_cast<int>(f1));
            }
            if (f2)
            {
                sqlite3_bind_int(s, p++, static_cast<int>(f2));
                sqlite3_bind_int(s, p++, static_cast<int>(f2));
            }
            if (f3)
            {
                sqlite3_bind_int(s, p++, static_cast<int>(f3));
                sqlite3_bind_int(s, p++, static_cast<int>(f3));
            }
            sqlite3_bind_int(s, p++, low);
            sqlite3_bind_int(s, p++, high);
        }, cb);
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

// ---------------------------------------------------------------------------
// Comsys bulk operations
// ---------------------------------------------------------------------------

bool CSQLiteDB::ClearComsysTables()
{
    return SQLITE_OK == sqlite3_exec(m_db,
        "DELETE FROM channels; DELETE FROM channel_users; DELETE FROM player_channels;",
        nullptr, nullptr, nullptr);
}

bool CSQLiteDB::SyncChannel(const UTF8 *name, const UTF8 *header,
    int type, int temp1, int temp2,
    int charge, int charge_who, int amount_col,
    int num_messages, int chan_obj)
{
    sqlite3_bind_text(m_stmtChannelSync, 1, reinterpret_cast<const char *>(name), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtChannelSync, 2, reinterpret_cast<const char *>(header), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtChannelSync, 3, type);
    sqlite3_bind_int(m_stmtChannelSync, 4, temp1);
    sqlite3_bind_int(m_stmtChannelSync, 5, temp2);
    sqlite3_bind_int(m_stmtChannelSync, 6, charge);
    sqlite3_bind_int(m_stmtChannelSync, 7, charge_who);
    sqlite3_bind_int(m_stmtChannelSync, 8, amount_col);
    sqlite3_bind_int(m_stmtChannelSync, 9, num_messages);
    sqlite3_bind_int(m_stmtChannelSync, 10, chan_obj);

    int rc = sqlite3_step(m_stmtChannelSync);
    sqlite3_reset(m_stmtChannelSync);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::SyncChannelUser(const UTF8 *channel_name, int who,
    bool is_on, bool comtitle_status,
    bool gag_join_leave, const UTF8 *title)
{
    sqlite3_bind_text(m_stmtChannelUserSync, 1, reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtChannelUserSync, 2, who);
    sqlite3_bind_int(m_stmtChannelUserSync, 3, is_on ? 1 : 0);
    sqlite3_bind_int(m_stmtChannelUserSync, 4, comtitle_status ? 1 : 0);
    sqlite3_bind_int(m_stmtChannelUserSync, 5, gag_join_leave ? 1 : 0);
    sqlite3_bind_text(m_stmtChannelUserSync, 6, reinterpret_cast<const char *>(title), -1, SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtChannelUserSync);
    sqlite3_reset(m_stmtChannelUserSync);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::SyncPlayerChannel(int who, const UTF8 *alias,
    const UTF8 *channel_name)
{
    sqlite3_bind_int(m_stmtPlayerChannelSync, 1, who);
    sqlite3_bind_text(m_stmtPlayerChannelSync, 2, reinterpret_cast<const char *>(alias), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtPlayerChannelSync, 3, reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtPlayerChannelSync);
    sqlite3_reset(m_stmtPlayerChannelSync);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteChannel(const UTF8 *name)
{
    sqlite3_bind_text(m_stmtChannelDelete, 1, reinterpret_cast<const char *>(name), -1, SQLITE_STATIC);
    int rc = sqlite3_step(m_stmtChannelDelete);
    sqlite3_reset(m_stmtChannelDelete);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteChannelUser(const UTF8 *channel_name, int who)
{
    sqlite3_bind_text(m_stmtChannelUserDelete, 1, reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtChannelUserDelete, 2, who);
    int rc = sqlite3_step(m_stmtChannelUserDelete);
    sqlite3_reset(m_stmtChannelUserDelete);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeletePlayerChannel(int who, const UTF8 *alias)
{
    sqlite3_bind_int(m_stmtPlayerChannelDelete, 1, who);
    sqlite3_bind_text(m_stmtPlayerChannelDelete, 2, reinterpret_cast<const char *>(alias), -1, SQLITE_STATIC);
    int rc = sqlite3_step(m_stmtPlayerChannelDelete);
    sqlite3_reset(m_stmtPlayerChannelDelete);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteAllPlayerChannels(int who)
{
    sqlite3_bind_int(m_stmtPlayerChannelDeleteAll, 1, who);
    int rc = sqlite3_step(m_stmtPlayerChannelDeleteAll);
    sqlite3_reset(m_stmtPlayerChannelDeleteAll);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::LoadAllChannels(ChannelCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtChannelLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        const UTF8 *name    = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtChannelLoadAll, 0));
        const UTF8 *header  = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtChannelLoadAll, 1));
        int type            = sqlite3_column_int(m_stmtChannelLoadAll, 2);
        int temp1           = sqlite3_column_int(m_stmtChannelLoadAll, 3);
        int temp2           = sqlite3_column_int(m_stmtChannelLoadAll, 4);
        int charge          = sqlite3_column_int(m_stmtChannelLoadAll, 5);
        int charge_who      = sqlite3_column_int(m_stmtChannelLoadAll, 6);
        int amount_col      = sqlite3_column_int(m_stmtChannelLoadAll, 7);
        int num_messages    = sqlite3_column_int(m_stmtChannelLoadAll, 8);
        int chan_obj        = sqlite3_column_int(m_stmtChannelLoadAll, 9);

        cb(name ? name : reinterpret_cast<const UTF8 *>(""),
           header ? header : reinterpret_cast<const UTF8 *>(""),
           type, temp1, temp2, charge, charge_who, amount_col, num_messages, chan_obj);
    }

    sqlite3_reset(m_stmtChannelLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllChannels: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool CSQLiteDB::LoadAllChannelUsers(ChannelUserCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtChannelUserLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        const UTF8 *channel_name = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtChannelUserLoadAll, 0));
        int who             = sqlite3_column_int(m_stmtChannelUserLoadAll, 1);
        bool is_on          = sqlite3_column_int(m_stmtChannelUserLoadAll, 2) != 0;
        bool comtitle       = sqlite3_column_int(m_stmtChannelUserLoadAll, 3) != 0;
        bool gag            = sqlite3_column_int(m_stmtChannelUserLoadAll, 4) != 0;
        const UTF8 *title   = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtChannelUserLoadAll, 5));

        cb(channel_name ? channel_name : reinterpret_cast<const UTF8 *>(""),
           who, is_on, comtitle, gag,
           title ? title : reinterpret_cast<const UTF8 *>(""));
    }

    sqlite3_reset(m_stmtChannelUserLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllChannelUsers: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool CSQLiteDB::LoadAllPlayerChannels(PlayerChannelCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtPlayerChannelLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int who                  = sqlite3_column_int(m_stmtPlayerChannelLoadAll, 0);
        const UTF8 *alias        = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtPlayerChannelLoadAll, 1));
        const UTF8 *channel_name = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtPlayerChannelLoadAll, 2));

        cb(who,
           alias ? alias : reinterpret_cast<const UTF8 *>(""),
           channel_name ? channel_name : reinterpret_cast<const UTF8 *>(""));
    }

    sqlite3_reset(m_stmtPlayerChannelLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllPlayerChannels: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Mail bulk operations
// ---------------------------------------------------------------------------

bool CSQLiteDB::ClearMailAliases()
{
    return SQLITE_OK == sqlite3_exec(m_db,
        "DELETE FROM mail_aliases;",
        nullptr, nullptr, nullptr);
}

bool CSQLiteDB::ClearMailTables()
{
    return SQLITE_OK == sqlite3_exec(m_db,
        "DELETE FROM mail_headers; DELETE FROM mail_bodies; DELETE FROM mail_aliases;",
        nullptr, nullptr, nullptr);
}

bool CSQLiteDB::SyncMailHeader(int to_player, int from_player, int body_number,
    const UTF8 *tolist, const UTF8 *time_str,
    const UTF8 *subject, int read_flags)
{
    sqlite3_bind_int(m_stmtMailHeaderSync, 1, to_player);
    sqlite3_bind_int(m_stmtMailHeaderSync, 2, from_player);
    sqlite3_bind_int(m_stmtMailHeaderSync, 3, body_number);
    sqlite3_bind_text(m_stmtMailHeaderSync, 4, reinterpret_cast<const char *>(tolist), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtMailHeaderSync, 5, reinterpret_cast<const char *>(time_str), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtMailHeaderSync, 6, reinterpret_cast<const char *>(subject), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtMailHeaderSync, 7, read_flags);

    int rc = sqlite3_step(m_stmtMailHeaderSync);
    sqlite3_reset(m_stmtMailHeaderSync);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::SyncMailBody(int number, const UTF8 *message)
{
    sqlite3_bind_int(m_stmtMailBodySync, 1, number);
    sqlite3_bind_text(m_stmtMailBodySync, 2, reinterpret_cast<const char *>(message), -1, SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtMailBodySync);
    sqlite3_reset(m_stmtMailBodySync);
    return SQLITE_DONE == rc;
}

int64_t CSQLiteDB::InsertMailHeaderReturningId(int to_player, int from_player,
    int body_number, const UTF8 *tolist, const UTF8 *time_str,
    const UTF8 *subject, int read_flags)
{
    sqlite3_bind_int(m_stmtMailHeaderInsertRet, 1, to_player);
    sqlite3_bind_int(m_stmtMailHeaderInsertRet, 2, from_player);
    sqlite3_bind_int(m_stmtMailHeaderInsertRet, 3, body_number);
    sqlite3_bind_text(m_stmtMailHeaderInsertRet, 4, reinterpret_cast<const char *>(tolist), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtMailHeaderInsertRet, 5, reinterpret_cast<const char *>(time_str), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtMailHeaderInsertRet, 6, reinterpret_cast<const char *>(subject), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtMailHeaderInsertRet, 7, read_flags);

    int rc = sqlite3_step(m_stmtMailHeaderInsertRet);
    sqlite3_reset(m_stmtMailHeaderInsertRet);

    if (SQLITE_DONE == rc)
    {
        return sqlite3_last_insert_rowid(m_db);
    }
    return -1;
}

bool CSQLiteDB::UpdateMailReadFlags(int64_t rowid, int read_flags)
{
    sqlite3_bind_int(m_stmtMailHeaderUpdateFlags, 1, read_flags);
    sqlite3_bind_int64(m_stmtMailHeaderUpdateFlags, 2, rowid);
    int rc = sqlite3_step(m_stmtMailHeaderUpdateFlags);
    sqlite3_reset(m_stmtMailHeaderUpdateFlags);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteMailHeader(int64_t rowid)
{
    sqlite3_bind_int64(m_stmtMailHeaderDelete, 1, rowid);
    int rc = sqlite3_step(m_stmtMailHeaderDelete);
    sqlite3_reset(m_stmtMailHeaderDelete);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteAllMailHeaders(int to_player)
{
    sqlite3_bind_int(m_stmtMailHeaderDeleteAll, 1, to_player);
    int rc = sqlite3_step(m_stmtMailHeaderDeleteAll);
    sqlite3_reset(m_stmtMailHeaderDeleteAll);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::DeleteMailBody(int number)
{
    sqlite3_bind_int(m_stmtMailBodyDelete, 1, number);
    int rc = sqlite3_step(m_stmtMailBodyDelete);
    sqlite3_reset(m_stmtMailBodyDelete);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::SyncMailAlias(int owner, const UTF8 *name, const UTF8 *desc,
    int desc_width, const UTF8 *members)
{
    sqlite3_bind_int(m_stmtMailAliasSync, 1, owner);
    sqlite3_bind_text(m_stmtMailAliasSync, 2, reinterpret_cast<const char *>(name), -1, SQLITE_STATIC);
    sqlite3_bind_text(m_stmtMailAliasSync, 3, reinterpret_cast<const char *>(desc), -1, SQLITE_STATIC);
    sqlite3_bind_int(m_stmtMailAliasSync, 4, desc_width);
    sqlite3_bind_text(m_stmtMailAliasSync, 5, reinterpret_cast<const char *>(members), -1, SQLITE_STATIC);

    int rc = sqlite3_step(m_stmtMailAliasSync);
    sqlite3_reset(m_stmtMailAliasSync);
    return SQLITE_DONE == rc;
}

bool CSQLiteDB::LoadAllMailHeaders(MailHeaderCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtMailHeaderLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int64_t rowid   = sqlite3_column_int64(m_stmtMailHeaderLoadAll, 0);
        int to_player   = sqlite3_column_int(m_stmtMailHeaderLoadAll, 1);
        int from_player = sqlite3_column_int(m_stmtMailHeaderLoadAll, 2);
        int body_number = sqlite3_column_int(m_stmtMailHeaderLoadAll, 3);
        const UTF8 *tolist  = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailHeaderLoadAll, 4));
        const UTF8 *time_str = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailHeaderLoadAll, 5));
        const UTF8 *subject = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailHeaderLoadAll, 6));
        int read_flags  = sqlite3_column_int(m_stmtMailHeaderLoadAll, 7);

        cb(rowid, to_player, from_player, body_number,
           tolist ? tolist : reinterpret_cast<const UTF8 *>(""),
           time_str ? time_str : reinterpret_cast<const UTF8 *>(""),
           subject ? subject : reinterpret_cast<const UTF8 *>(""),
           read_flags);
    }

    sqlite3_reset(m_stmtMailHeaderLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllMailHeaders: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool CSQLiteDB::LoadAllMailBodies(MailBodyCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtMailBodyLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int number          = sqlite3_column_int(m_stmtMailBodyLoadAll, 0);
        const UTF8 *message = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailBodyLoadAll, 1));

        cb(number, message ? message : reinterpret_cast<const UTF8 *>(""));
    }

    sqlite3_reset(m_stmtMailBodyLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllMailBodies: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool CSQLiteDB::LoadAllMailAliases(MailAliasCallback cb)
{
    int rc = SQLITE_DONE;
    for (;;)
    {
        rc = sqlite3_step(m_stmtMailAliasLoadAll);
        if (SQLITE_ROW != rc)
        {
            break;
        }

        int owner           = sqlite3_column_int(m_stmtMailAliasLoadAll, 0);
        const UTF8 *name    = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailAliasLoadAll, 1));
        const UTF8 *desc    = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailAliasLoadAll, 2));
        int desc_width      = sqlite3_column_int(m_stmtMailAliasLoadAll, 3);
        const UTF8 *members = reinterpret_cast<const UTF8 *>(sqlite3_column_text(m_stmtMailAliasLoadAll, 4));

        cb(owner,
           name ? name : reinterpret_cast<const UTF8 *>(""),
           desc ? desc : reinterpret_cast<const UTF8 *>(""),
           desc_width,
           members ? members : reinterpret_cast<const UTF8 *>(""));
    }

    sqlite3_reset(m_stmtMailAliasLoadAll);
    if (SQLITE_DONE != rc)
    {
        fprintf(stderr, "CSQLiteDB::LoadAllMailAliases: %s\n", sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}
