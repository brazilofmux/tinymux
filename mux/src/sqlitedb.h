/*! \file sqlitedb.h
 * \brief SQLite storage backend for TinyMUX.
 *
 * This module provides a unified SQLite-backed store for object metadata
 * and attribute values, replacing both the db[] flatfile dump and CHashFile.
 *
 * Design: write-through (SQLite is always authoritative), with an in-process
 * read cache layered on top.
 */

#ifndef SQLITEDB_H
#define SQLITEDB_H

#include <sqlite3.h>
#include <cstddef>
#include <cstdint>
#include <functional>

// When building the standalone test harness, define TINYMUX_TYPES_DEFINED
// via compiler flags and provide these types. When building inside TinyMUX,
// these come from config.h which is included before this header.
//
#if defined(TINYMUX_TYPES_DEFINED)
typedef int           dbref;
typedef unsigned int  FLAG;
typedef int           POWER;
typedef unsigned char UTF8;
constexpr dbref NOTHING = -1;
#endif

class CSQLiteDB
{
public:
    CSQLiteDB();
    ~CSQLiteDB();

    // Database lifecycle
    //
    bool Open(const char *path);
    void Close();
    bool IsOpen() const { return nullptr != m_db; }

    // Object metadata operations.
    // These correspond to the db[] array and s_Location() etc.
    //
    struct ObjectRecord
    {
        dbref   dbref_val;
        dbref   location;
        dbref   contents;
        dbref   exits;
        dbref   next;
        dbref   link;
        dbref   owner;
        dbref   parent;
        dbref   zone;
        int     pennies;
        FLAG    flags1;
        FLAG    flags2;
        FLAG    flags3;
        POWER   powers1;
        POWER   powers2;
        // Name is stored as attribute A_NAME in the attributes table,
        // not in the objects table, matching TinyMUX convention.
    };

    bool InsertObject(const ObjectRecord &obj);
    bool DeleteObject(dbref obj);
    bool LoadObject(dbref obj, ObjectRecord &rec);

    // Individual field updates (write-through from s_Location etc.)
    //
    bool UpdateLocation(dbref obj, dbref val);
    bool UpdateContents(dbref obj, dbref val);
    bool UpdateExits(dbref obj, dbref val);
    bool UpdateNext(dbref obj, dbref val);
    bool UpdateLink(dbref obj, dbref val);
    bool UpdateOwner(dbref obj, dbref val);
    bool UpdateParent(dbref obj, dbref val);
    bool UpdateZone(dbref obj, dbref val);
    bool UpdatePennies(dbref obj, int val);
    bool UpdateFlags(dbref obj, FLAG f1, FLAG f2, FLAG f3);
    bool UpdatePowers(dbref obj, POWER p1, POWER p2);

    // Load all objects into a callback (for populating db[] at startup).
    //
    typedef std::function<void(const ObjectRecord &)> ObjectCallback;
    bool LoadAllObjects(ObjectCallback cb);

    // Attribute operations.
    // These correspond to cache_get / cache_put / cache_del.
    //
    bool GetAttribute(dbref obj, int attrnum, UTF8 *buf, size_t buflen, size_t *pLen);
    bool PutAttribute(dbref obj, int attrnum, const UTF8 *value, size_t len);
    bool DelAttribute(dbref obj, int attrnum);
    bool DelAllAttributes(dbref obj);

    // Bulk attribute read for preloading / @search.
    //
    typedef std::function<void(int attrnum, const UTF8 *value, size_t len)> AttrCallback;
    bool GetAllAttributes(dbref obj, AttrCallback cb);

    // Attribute name registry.
    // These correspond to vattr_define_LEN / vattr_alloc_LEN.
    //
    bool PutAttrName(int attrnum, const char *name, int flags);
    bool DelAttrName(int attrnum);

    typedef std::function<void(int attrnum, const char *name, int flags)> AttrNameCallback;
    bool LoadAllAttrNames(AttrNameCallback cb);

    // Metadata key-value store (attr_next, db_top, etc.)
    //
    bool PutMeta(const char *key, int value);
    bool GetMeta(const char *key, int *value);

    // Transaction support for batching related writes.
    //
    bool Begin();
    bool Commit();
    bool Rollback();

    // Comsys operations (bulk sync + write-through).
    //
    bool SyncChannel(const UTF8 *name, const UTF8 *header,
                     int type, int temp1, int temp2,
                     int charge, int charge_who, int amount_col,
                     int num_messages, int chan_obj);
    bool SyncChannelUser(const UTF8 *channel_name, int who,
                         bool is_on, bool comtitle_status,
                         bool gag_join_leave, const UTF8 *title);
    bool SyncPlayerChannel(int who, const UTF8 *alias,
                           const UTF8 *channel_name);
    bool DeleteChannel(const UTF8 *name);
    bool DeleteChannelUser(const UTF8 *channel_name, int who);
    bool DeletePlayerChannel(int who, const UTF8 *alias);
    bool DeleteAllPlayerChannels(int who);
    bool ClearComsysTables();

    typedef std::function<void(const UTF8 *name, const UTF8 *header,
        int type, int temp1, int temp2, int charge, int charge_who,
        int amount_col, int num_messages, int chan_obj)> ChannelCallback;
    bool LoadAllChannels(ChannelCallback cb);

    typedef std::function<void(const UTF8 *channel_name, int who,
        bool is_on, bool comtitle_status, bool gag_join_leave,
        const UTF8 *title)> ChannelUserCallback;
    bool LoadAllChannelUsers(ChannelUserCallback cb);

    typedef std::function<void(int who, const UTF8 *alias,
        const UTF8 *channel_name)> PlayerChannelCallback;
    bool LoadAllPlayerChannels(PlayerChannelCallback cb);

    // Mail operations (bulk sync + write-through).
    //
    bool SyncMailHeader(int to_player, int from_player, int body_number,
                        const UTF8 *tolist, const UTF8 *time_str,
                        const UTF8 *subject, int read_flags);
    int64_t InsertMailHeaderReturningId(int to_player, int from_player,
                        int body_number, const UTF8 *tolist,
                        const UTF8 *time_str, const UTF8 *subject,
                        int read_flags);
    bool UpdateMailReadFlags(int64_t rowid, int read_flags);
    bool DeleteMailHeader(int64_t rowid);
    bool DeleteAllMailHeaders(int to_player);
    bool SyncMailBody(int number, const UTF8 *message);
    bool DeleteMailBody(int number);
    bool SyncMailAlias(int owner, const UTF8 *name, const UTF8 *desc,
                       int desc_width, const UTF8 *members);
    bool ClearMailAliases();
    bool ClearMailTables();

    typedef std::function<void(int64_t rowid, int to_player, int from_player,
        int body_number, const UTF8 *tolist, const UTF8 *time_str,
        const UTF8 *subject, int read_flags)> MailHeaderCallback;
    bool LoadAllMailHeaders(MailHeaderCallback cb);

    typedef std::function<void(int number, const UTF8 *message)> MailBodyCallback;
    bool LoadAllMailBodies(MailBodyCallback cb);

    typedef std::function<void(int owner, const UTF8 *name,
        const UTF8 *desc, int desc_width, const UTF8 *members)> MailAliasCallback;
    bool LoadAllMailAliases(MailAliasCallback cb);

    // Maintenance
    //
    bool Checkpoint();
    bool Optimize();

    // Statistics
    //
    struct Stats
    {
        uint64_t    obj_inserts;
        uint64_t    obj_deletes;
        uint64_t    obj_updates;
        uint64_t    obj_loads;
        uint64_t    attr_gets;
        uint64_t    attr_puts;
        uint64_t    attr_dels;
        uint64_t    attr_bulk_loads;
    };
    Stats GetStats() const { return m_stats; }
    void ResetStats() { m_stats = {}; }

private:
    sqlite3 *m_db;

    // Object metadata statements.
    //
    sqlite3_stmt *m_stmtObjInsert;
    sqlite3_stmt *m_stmtObjDelete;
    sqlite3_stmt *m_stmtObjLoad;
    sqlite3_stmt *m_stmtObjLoadAll;
    sqlite3_stmt *m_stmtUpdateLocation;
    sqlite3_stmt *m_stmtUpdateContents;
    sqlite3_stmt *m_stmtUpdateExits;
    sqlite3_stmt *m_stmtUpdateNext;
    sqlite3_stmt *m_stmtUpdateLink;
    sqlite3_stmt *m_stmtUpdateOwner;
    sqlite3_stmt *m_stmtUpdateParent;
    sqlite3_stmt *m_stmtUpdateZone;
    sqlite3_stmt *m_stmtUpdatePennies;
    sqlite3_stmt *m_stmtUpdateFlags;
    sqlite3_stmt *m_stmtUpdatePowers;

    // Attribute statements.
    //
    sqlite3_stmt *m_stmtAttrGet;
    sqlite3_stmt *m_stmtAttrPut;
    sqlite3_stmt *m_stmtAttrDel;
    sqlite3_stmt *m_stmtAttrDelObj;
    sqlite3_stmt *m_stmtAttrGetObj;

    // Attribute name registry statements.
    //
    sqlite3_stmt *m_stmtAttrNamePut;
    sqlite3_stmt *m_stmtAttrNameDel;
    sqlite3_stmt *m_stmtAttrNameLoadAll;

    // Metadata statements.
    //
    sqlite3_stmt *m_stmtMetaPut;
    sqlite3_stmt *m_stmtMetaGet;

    // Comsys statements.
    //
    sqlite3_stmt *m_stmtChannelSync;
    sqlite3_stmt *m_stmtChannelUserSync;
    sqlite3_stmt *m_stmtPlayerChannelSync;
    sqlite3_stmt *m_stmtChannelLoadAll;
    sqlite3_stmt *m_stmtChannelUserLoadAll;
    sqlite3_stmt *m_stmtPlayerChannelLoadAll;
    sqlite3_stmt *m_stmtChannelDelete;
    sqlite3_stmt *m_stmtChannelUserDelete;
    sqlite3_stmt *m_stmtPlayerChannelDelete;
    sqlite3_stmt *m_stmtPlayerChannelDeleteAll;

    // Mail statements.
    //
    sqlite3_stmt *m_stmtMailHeaderSync;
    sqlite3_stmt *m_stmtMailHeaderInsertRet;
    sqlite3_stmt *m_stmtMailHeaderUpdateFlags;
    sqlite3_stmt *m_stmtMailHeaderDelete;
    sqlite3_stmt *m_stmtMailHeaderDeleteAll;
    sqlite3_stmt *m_stmtMailBodySync;
    sqlite3_stmt *m_stmtMailBodyDelete;
    sqlite3_stmt *m_stmtMailAliasSync;
    sqlite3_stmt *m_stmtMailHeaderLoadAll;
    sqlite3_stmt *m_stmtMailBodyLoadAll;
    sqlite3_stmt *m_stmtMailAliasLoadAll;

    Stats m_stats;

    bool CreateSchema();
    bool PrepareStatements();
    void FinalizeStatements();
    bool ConfigurePragmas();

    // Helper: execute a single-field UPDATE on the objects table.
    //
    bool UpdateSingleField(sqlite3_stmt *stmt, dbref obj, int val);
};

#endif // !SQLITEDB_H
