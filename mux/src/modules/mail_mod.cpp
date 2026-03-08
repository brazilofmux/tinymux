/*! \file mail_mod.cpp
 * \brief Mail Module — @mail system as a loadable module
 *
 * This module implements the MUX @mail system as a dynamically loaded
 * module.  It hooks into server events for player connect/disconnect
 * and provides mux_IMailControl for command dispatch.
 *
 * Core dependencies are accessed exclusively through COM interfaces:
 *   mux_INotify           — player notification
 *   mux_IObjectInfo       — object property queries
 *   mux_IAttributeAccess  — attribute read/write
 *   mux_IPermissions      — permission checks
 *   mux_ILog              — logging
 *
 * Mail data is loaded from and persisted to the game's SQLite database
 * via the module's own sqlite3 connection.
 */

#include "../copyright.h"
#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "mail_mod.h"

#include <cstring>
#include <cstdlib>

// Module bookkeeping.
//
static uint32_t g_cComponents  = 0;
static uint32_t g_cServerLocks = 0;

// Module entry points.
//
static MUX_CLASS_INFO mail_classes[] =
{
    { CID_MailMod }
};
#define NUM_CLASSES (sizeof(mail_classes)/sizeof(mail_classes[0]))

extern "C" MUX_RESULT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, mail_classes, nullptr);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, mail_classes);
}

extern "C" MUX_RESULT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_MailMod == cid)
    {
        CMailModFactory *pFactory = nullptr;
        try
        {
            pFactory = new CMailModFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    return mr;
}

// ---------------------------------------------------------------------------
// CMailMod — main module class.
// ---------------------------------------------------------------------------

CMailMod::CMailMod(void) : m_cRef(1),
    m_pILog(nullptr),
    m_pIServerEventsControl(nullptr),
    m_pINotify(nullptr),
    m_pIObjectInfo(nullptr),
    m_pIAttributeAccess(nullptr),
    m_pIPermissions(nullptr),
    m_db(nullptr),
    m_mail_expiration(14),
    m_mail_per_player(250),
    m_mail_list(nullptr),
    m_mail_db_top(0),
    m_mail_db_size(0),
    m_malias(nullptr),
    m_ma_size(0),
    m_ma_top(0),
    m_bLoading(false)
{
    g_cComponents++;
}

MUX_RESULT CMailMod::FinalConstruct(void)
{
    MUX_RESULT mr;

    // Acquire logging interface.
    //
    mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
                            IID_ILog,
                            reinterpret_cast<void **>(&m_pILog));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    // Register for server events.
    //
    mux_IServerEventsSink *pIServerEventsSink = nullptr;
    mr = QueryInterface(IID_IServerEventsSink,
                        reinterpret_cast<void **>(&pIServerEventsSink));
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_CreateInstance(CID_ServerEventsSource, nullptr,
                                UseSameProcess, IID_IServerEventsControl,
                                reinterpret_cast<void **>(&m_pIServerEventsControl));
        if (MUX_SUCCEEDED(mr))
        {
            m_pIServerEventsControl->Advise(pIServerEventsSink);
        }
        pIServerEventsSink->Release();
    }

    // Acquire core interfaces.
    //
    mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
                       IID_INotify,
                       reinterpret_cast<void **>(&m_pINotify));

    mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
                       IID_IObjectInfo,
                       reinterpret_cast<void **>(&m_pIObjectInfo));

    mux_CreateInstance(CID_AttributeAccess, nullptr, UseSameProcess,
                       IID_IAttributeAccess,
                       reinterpret_cast<void **>(&m_pIAttributeAccess));

    mux_CreateInstance(CID_Permissions, nullptr, UseSameProcess,
                       IID_IPermissions,
                       reinterpret_cast<void **>(&m_pIPermissions));

    // Log that we are alive.
    //
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr2 = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                             T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr2) && fStarted)
        {
            m_pILog->log_text(T("Mail module loaded."));
            m_pILog->end_log();
        }
    }

    return mr;
}

CMailMod::~CMailMod()
{
    // Close our SQLite connection.
    //
    CloseDatabase();

    // Free mail data.
    //
    ClearRuntimeData();

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module unloading."));
            m_pILog->end_log();
        }
        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIServerEventsControl)
    {
        m_pIServerEventsControl->Release();
        m_pIServerEventsControl = nullptr;
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->Release();
        m_pINotify = nullptr;
    }

    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->Release();
        m_pIObjectInfo = nullptr;
    }

    if (nullptr != m_pIAttributeAccess)
    {
        m_pIAttributeAccess->Release();
        m_pIAttributeAccess = nullptr;
    }

    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->Release();
        m_pIPermissions = nullptr;
    }

    g_cComponents--;
}

void CMailMod::ClearRuntimeData(void)
{
    // Free all mail linked lists.
    //
    for (auto it = m_mail_htab.begin(); it != m_mail_htab.end(); ++it)
    {
        struct mail *mp = it->second;
        while (nullptr != mp)
        {
            struct mail *next = mp->next;
            if (nullptr != mp->time)
            {
                free(mp->time);
            }
            if (nullptr != mp->subject)
            {
                free(mp->subject);
            }
            if (nullptr != mp->tolist)
            {
                free(mp->tolist);
            }
            delete mp;
            mp = next;
        }
    }
    m_mail_htab.clear();

    // Free message bodies.
    //
    if (nullptr != m_mail_list)
    {
        for (int i = 0; i < m_mail_db_top; i++)
        {
            if (nullptr != m_mail_list[i].m_pMessage)
            {
                free(m_mail_list[i].m_pMessage);
                m_mail_list[i].m_pMessage = nullptr;
            }
        }
        free(m_mail_list);
        m_mail_list = nullptr;
    }
    m_mail_db_top = 0;
    m_mail_db_size = 0;

    // Free mail aliases.
    //
    if (nullptr != m_malias)
    {
        for (int i = 0; i < m_ma_top; i++)
        {
            if (nullptr != m_malias[i])
            {
                if (nullptr != m_malias[i]->name)
                {
                    free(m_malias[i]->name);
                }
                if (nullptr != m_malias[i]->desc)
                {
                    free(m_malias[i]->desc);
                }
                delete m_malias[i];
            }
        }
        delete[] m_malias;
        m_malias = nullptr;
    }
    m_ma_size = 0;
    m_ma_top = 0;
}

MUX_RESULT CMailMod::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IMailControl *>(this);
    }
    else if (IID_IMailControl == iid)
    {
        *ppv = static_cast<mux_IMailControl *>(this);
    }
    else if (IID_IServerEventsSink == iid)
    {
        *ppv = static_cast<mux_IServerEventsSink *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CMailMod::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CMailMod::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// ---------------------------------------------------------------------------
// SQLite database access — module's own connection.
// ---------------------------------------------------------------------------

bool CMailMod::OpenDatabase(const UTF8 *pPath)
{
    if (nullptr != m_db)
    {
        return true;
    }

    int rc = sqlite3_open(reinterpret_cast<const char *>(pPath), &m_db);
    if (SQLITE_OK != rc)
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("DB"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: sqlite3_open failed: "));
                m_pILog->log_text(reinterpret_cast<const UTF8 *>(
                    sqlite3_errmsg(m_db)));
                m_pILog->end_log();
            }
        }
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Match server pragmas for WAL mode.
    //
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

    return true;
}

void CMailMod::CloseDatabase(void)
{
    if (nullptr != m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Message body management.
// ---------------------------------------------------------------------------

void CMailMod::mail_db_grow(int newtop)
{
    if (newtop <= m_mail_db_top)
    {
        return;
    }

    if (m_mail_db_size <= newtop)
    {
        int newsize = m_mail_db_size + 100;
        if (newtop > newsize)
        {
            newsize = newtop;
        }

        struct mail_body *newdb = static_cast<struct mail_body *>(
            calloc(newsize, sizeof(struct mail_body)));
        if (nullptr == newdb)
        {
            return;
        }

        if (nullptr != m_mail_list)
        {
            memcpy(newdb, m_mail_list,
                   m_mail_db_top * sizeof(struct mail_body));
            free(m_mail_list);
        }
        m_mail_list = newdb;
        m_mail_db_size = newsize;
    }

    // Initialize new slots.
    //
    for (int i = m_mail_db_top; i < newtop; i++)
    {
        m_mail_list[i].m_nRefs = 0;
        m_mail_list[i].m_nMessage = 0;
        m_mail_list[i].m_pMessage = nullptr;
    }
    m_mail_db_top = newtop;
}

int CMailMod::new_mail_message(const UTF8 *message, int number)
{
    bool bTruncated = false;
    size_t nLen = strlen(reinterpret_cast<const char *>(message));
    if (nLen > MOD_LBUF_SIZE - 1)
    {
        bTruncated = true;
        nLen = MOD_LBUF_SIZE - 1;
    }

    mail_db_grow(number + 1);

    struct mail_body *pm = &m_mail_list[number];
    pm->m_nMessage = nLen;
    pm->m_pMessage = static_cast<UTF8 *>(malloc(nLen + 1));
    if (nullptr != pm->m_pMessage)
    {
        memcpy(pm->m_pMessage, message, nLen);
        pm->m_pMessage[nLen] = '\0';
    }

    if (bTruncated && nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_BUGS, T("BUG"), T("MAIL"));
        if (fStarted)
        {
            UTF8 buf[128];
            snprintf(reinterpret_cast<char *>(buf), sizeof(buf),
                     "new_mail_message: Mail message %d truncated.", number);
            m_pILog->log_text(buf);
            m_pILog->end_log();
        }
    }
    return number;
}

const UTF8 *CMailMod::MessageFetch(int number)
{
    if (number < 0 || number >= m_mail_db_top)
    {
        return T("MAIL: This mail message does not exist in the database. Please alert your admin.");
    }
    if (nullptr != m_mail_list[number].m_pMessage)
    {
        return m_mail_list[number].m_pMessage;
    }
    return T("MAIL: This mail message does not exist in the database. Please alert your admin.");
}

size_t CMailMod::MessageFetchSize(int number)
{
    if (number < 0 || number >= m_mail_db_top)
    {
        return 0;
    }
    if (nullptr != m_mail_list[number].m_pMessage)
    {
        return m_mail_list[number].m_nMessage;
    }
    return 0;
}

void CMailMod::MessageReferenceInc(int number)
{
    if (0 <= number && number < m_mail_db_top)
    {
        m_mail_list[number].m_nRefs++;
    }
}

void CMailMod::MessageReferenceDec(int number)
{
    if (number < 0 || number >= m_mail_db_top)
    {
        return;
    }

    m_mail_list[number].m_nRefs--;
    if (m_mail_list[number].m_nRefs <= 0)
    {
        sqlite_wt_delete_mail_body(number);
        if (nullptr != m_mail_list[number].m_pMessage)
        {
            free(m_mail_list[number].m_pMessage);
            m_mail_list[number].m_pMessage = nullptr;
            m_mail_list[number].m_nMessage = 0;
        }
        m_mail_list[number].m_nRefs = 0;
    }
}

// ---------------------------------------------------------------------------
// Mail list management — per-player doubly-linked lists.
// ---------------------------------------------------------------------------

struct mail *CMailMod::MailListFirst(dbref player)
{
    auto it = m_mail_htab.find(player);
    if (it != m_mail_htab.end())
    {
        return it->second;
    }
    return nullptr;
}

struct mail *CMailMod::MailListNext(struct mail *mp, dbref player)
{
    if (nullptr == mp)
    {
        return nullptr;
    }
    struct mail *next = mp->next;
    if (nullptr != next && next->to == player)
    {
        return next;
    }
    return nullptr;
}

void CMailMod::MailListAppend(dbref player, struct mail *mp)
{
    mp->next = nullptr;
    mp->prev = nullptr;

    auto it = m_mail_htab.find(player);
    if (it == m_mail_htab.end())
    {
        m_mail_htab[player] = mp;
        return;
    }

    // Find the tail.
    //
    struct mail *tail = it->second;
    while (nullptr != tail->next)
    {
        tail = tail->next;
    }
    tail->next = mp;
    mp->prev = tail;
}

void CMailMod::MailListRemove(dbref player, struct mail *mp)
{
    if (nullptr == mp)
    {
        return;
    }

    if (nullptr != mp->prev)
    {
        mp->prev->next = mp->next;
    }
    else
    {
        // mp was the head.
        //
        if (nullptr != mp->next)
        {
            m_mail_htab[player] = mp->next;
        }
        else
        {
            m_mail_htab.erase(player);
        }
    }

    if (nullptr != mp->next)
    {
        mp->next->prev = mp->prev;
    }

    MessageReferenceDec(mp->number);
    if (nullptr != mp->time) free(mp->time);
    if (nullptr != mp->subject) free(mp->subject);
    if (nullptr != mp->tolist) free(mp->tolist);
    delete mp;
}

void CMailMod::MailListRemoveAll(dbref player)
{
    auto it = m_mail_htab.find(player);
    if (it == m_mail_htab.end())
    {
        return;
    }

    struct mail *mp = it->second;
    while (nullptr != mp)
    {
        struct mail *next = mp->next;
        MessageReferenceDec(mp->number);
        if (nullptr != mp->time) free(mp->time);
        if (nullptr != mp->subject) free(mp->subject);
        if (nullptr != mp->tolist) free(mp->tolist);
        delete mp;
        mp = next;
    }
    m_mail_htab.erase(it);
}

// ---------------------------------------------------------------------------
// SQLite write-through helpers.
// ---------------------------------------------------------------------------

void CMailMod::sqlite_wt_insert_mail(struct mail *mp)
{
    if (m_bLoading || nullptr == m_db) return;

    const char *sql =
        "INSERT INTO mail_headers (to_player, from_player, body_number, "
        "tolist, time_str, subject, read_flags) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, mp->to);
    sqlite3_bind_int(stmt, 2, mp->from);
    sqlite3_bind_int(stmt, 3, mp->number);
    sqlite3_bind_text(stmt, 4, reinterpret_cast<const char *>(mp->tolist), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, reinterpret_cast<const char *>(mp->time), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, reinterpret_cast<const char *>(mp->subject), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, mp->read);

    if (SQLITE_DONE == sqlite3_step(stmt))
    {
        mp->sqlite_id = sqlite3_last_insert_rowid(m_db);
    }
    else
    {
        mp->sqlite_id = -1;
    }

    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_update_mail_flags(struct mail *mp)
{
    if (m_bLoading || nullptr == m_db || mp->sqlite_id < 0) return;

    const char *sql = "UPDATE mail_headers SET read_flags = ? WHERE rowid = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, mp->read);
    sqlite3_bind_int64(stmt, 2, mp->sqlite_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_delete_mail(struct mail *mp)
{
    if (m_bLoading || nullptr == m_db || mp->sqlite_id < 0) return;

    const char *sql = "DELETE FROM mail_headers WHERE rowid = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int64(stmt, 1, mp->sqlite_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_delete_all_mail(int to_player)
{
    if (m_bLoading || nullptr == m_db) return;

    const char *sql = "DELETE FROM mail_headers WHERE to_player = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, to_player);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_mail_body(int number, const UTF8 *message)
{
    if (m_bLoading || nullptr == m_db) return;

    const char *sql =
        "INSERT OR REPLACE INTO mail_bodies (number, message) VALUES (?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, number);
    sqlite3_bind_text(stmt, 2, reinterpret_cast<const char *>(message),
                      -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_delete_mail_body(int number)
{
    if (m_bLoading || nullptr == m_db) return;

    const char *sql = "DELETE FROM mail_bodies WHERE number = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, number);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CMailMod::sqlite_wt_sync_all_aliases(void)
{
    if (m_bLoading || nullptr == m_db) return;

    // Clear existing aliases.
    //
    sqlite3_exec(m_db, "DELETE FROM mail_aliases;", nullptr, nullptr, nullptr);

    const char *sql =
        "INSERT INTO mail_aliases (owner, name, description, desc_width, members) "
        "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    for (int i = 0; i < m_ma_top; i++)
    {
        malias_t *m = m_malias[i];
        if (nullptr == m) continue;

        // Build space-separated member list.
        //
        char members_buf[MOD_LBUF_SIZE];
        char *bp = members_buf;
        for (int j = 0; j < m->numrecep; j++)
        {
            if (j > 0)
            {
                *bp++ = ' ';
            }
            bp += snprintf(bp, sizeof(members_buf) - (bp - members_buf),
                           "%d", m->list[j]);
        }
        *bp = '\0';

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, m->owner);
        sqlite3_bind_text(stmt, 2, reinterpret_cast<const char *>(m->name),
                          -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, reinterpret_cast<const char *>(m->desc),
                          -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, static_cast<int>(m->desc_width));
        sqlite3_bind_text(stmt, 5, members_buf, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

// ---------------------------------------------------------------------------
// Data loading from SQLite.
// ---------------------------------------------------------------------------

bool CMailMod::LoadMailBodies(void)
{
    if (nullptr == m_db) return false;

    const char *sql = "SELECT number, message FROM mail_bodies;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return false;
    }

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        int number = sqlite3_column_int(stmt, 0);
        const UTF8 *message = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 1));
        if (nullptr != message)
        {
            new_mail_message(message, number);
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

bool CMailMod::LoadMailHeaders(void)
{
    if (nullptr == m_db) return false;

    const char *sql =
        "SELECT rowid, to_player, from_player, body_number, "
        "tolist, time_str, subject, read_flags FROM mail_headers;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return false;
    }

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        struct mail *mp = nullptr;
        try
        {
            mp = new struct mail;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == mp)
        {
            break;
        }

        mp->sqlite_id = sqlite3_column_int64(stmt, 0);
        mp->to        = sqlite3_column_int(stmt, 1);
        mp->from      = sqlite3_column_int(stmt, 2);
        mp->number    = sqlite3_column_int(stmt, 3);

        MessageReferenceInc(mp->number);

        const char *pTolist  = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        const char *pTime    = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
        const char *pSubject = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

        mp->tolist  = pTolist  ? reinterpret_cast<UTF8 *>(strdup(pTolist))  : reinterpret_cast<UTF8 *>(strdup(""));
        mp->time    = pTime    ? reinterpret_cast<UTF8 *>(strdup(pTime))    : reinterpret_cast<UTF8 *>(strdup(""));
        mp->subject = pSubject ? reinterpret_cast<UTF8 *>(strdup(pSubject)) : reinterpret_cast<UTF8 *>(strdup(""));
        mp->read    = sqlite3_column_int(stmt, 7);

        mp->next = nullptr;
        mp->prev = nullptr;

        MailListAppend(mp->to, mp);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool CMailMod::LoadMailAliases(void)
{
    if (nullptr == m_db) return false;

    const char *sql =
        "SELECT owner, name, description, desc_width, members "
        "FROM mail_aliases;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return false;
    }

    std::vector<malias_t *> alias_vec;

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        malias_t *m = nullptr;
        try
        {
            m = new malias_t;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == m)
        {
            break;
        }

        m->owner = sqlite3_column_int(stmt, 0);

        const char *pName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *pDesc = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        m->name = pName ? reinterpret_cast<UTF8 *>(strdup(pName)) : reinterpret_cast<UTF8 *>(strdup(""));
        m->desc = pDesc ? reinterpret_cast<UTF8 *>(strdup(pDesc)) : reinterpret_cast<UTF8 *>(strdup(""));
        m->desc_width = sqlite3_column_int(stmt, 3);

        // Parse space-separated member list.
        //
        m->numrecep = 0;
        const char *pMembers = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        if (nullptr != pMembers && pMembers[0] != '\0')
        {
            char buf[MOD_LBUF_SIZE];
            strncpy(buf, pMembers, MOD_LBUF_SIZE - 1);
            buf[MOD_LBUF_SIZE - 1] = '\0';
            char *p = buf;
            while (*p && m->numrecep < MAX_MALIAS_MEMBERSHIP)
            {
                while (*p == ' ') p++;
                if (*p == '\0') break;
                m->list[m->numrecep++] = atoi(p);
                while (*p && *p != ' ') p++;
            }
        }

        alias_vec.push_back(m);
    }

    sqlite3_finalize(stmt);

    if (!alias_vec.empty())
    {
        int alias_count = static_cast<int>(alias_vec.size());
        m_malias = nullptr;
        try
        {
            m_malias = new malias_t *[alias_count];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == m_malias)
        {
            for (auto *a : alias_vec)
            {
                if (a->name) free(a->name);
                if (a->desc) free(a->desc);
                delete a;
            }
            return false;
        }

        m_ma_top = m_ma_size = alias_count;
        for (int i = 0; i < m_ma_top; i++)
        {
            m_malias[i] = alias_vec[i];
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// mux_IMailControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CMailMod::Initialize(const UTF8 *pDatabasePath,
    int mail_expiration, int mail_per_player)
{
    if (nullptr == pDatabasePath)
    {
        return MUX_E_INVALIDARG;
    }

    m_mail_expiration = mail_expiration;
    m_mail_per_player = mail_per_player;

    if (!OpenDatabase(pDatabasePath))
    {
        return MUX_E_FAIL;
    }

    // Check for mail_db_top metadata to know if mail data exists.
    //
    const char *sql = "SELECT value FROM metadata WHERE key = 'mail_db_top';";
    sqlite3_stmt *stmt = nullptr;
    int mail_top = 0;

    if (SQLITE_OK == sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        if (SQLITE_ROW == sqlite3_step(stmt))
        {
            mail_top = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    m_bLoading = true;

    ClearRuntimeData();
    mail_db_grow(mail_top + 1);

    if (!LoadMailBodies())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailBodies failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadMailHeaders())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailHeaders failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadMailAliases())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Mail module: LoadMailAliases failed."));
                m_pILog->end_log();
            }
        }
    }

    m_bLoading = false;

    if (nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("MAIL"), T("LOAD"));
        if (fStarted)
        {
            m_pILog->log_text(T("Mail module: data loaded from SQLite."));
            m_pILog->end_log();
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CMailMod::PlayerConnect(dbref player)
{
    // TODO: Check mail on connect and notify player.
    UNUSED_PARAMETER(player);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::PlayerNuke(dbref player)
{
    // TODO: Enable when module fully owns mail data.
    UNUSED_PARAMETER(player);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::MailCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    // TODO: Implement mail command dispatch.
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(pArg1);
    UNUSED_PARAMETER(pArg2);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::MaliasCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    // TODO: Implement malias command dispatch.
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(pArg1);
    UNUSED_PARAMETER(pArg2);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::FolderCommand(dbref executor, int key, int nargs,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    // TODO: Implement folder command dispatch.
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(pArg1);
    UNUSED_PARAMETER(pArg2);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::CheckMail(dbref player, int folder, bool silent)
{
    // TODO: Implement check mail (needs get_folder_name, urgent_mail).
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(folder);
    UNUSED_PARAMETER(silent);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::ExpireMail(void)
{
    // TODO: Implement mail expiration (needs DO_WHOLE_DB, No_Mail_Expire, time classes).
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::CountMail(dbref player, int folder,
    int *pRead, int *pUnread, int *pCleared)
{
    // TODO: Enable when module fully owns mail data.
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(folder);
    UNUSED_PARAMETER(pRead);
    UNUSED_PARAMETER(pUnread);
    UNUSED_PARAMETER(pCleared);
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::DestroyPlayerMail(dbref player)
{
    // TODO: Enable when module fully owns mail data.
    UNUSED_PARAMETER(player);
    return MUX_E_NOTIMPLEMENTED;
}

// ---------------------------------------------------------------------------
// mux_IServerEventsSink implementation.
// ---------------------------------------------------------------------------

void CMailMod::startup(void)
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("MAIL"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module: startup event."));
            m_pILog->end_log();
        }
    }
}

void CMailMod::presync_database(void)
{
    // Nothing needed — SQLite write-through handles persistence.
}

void CMailMod::presync_database_sigsegv(void)
{
    // Nothing.
}

void CMailMod::dump_database(int dump_type)
{
    UNUSED_PARAMETER(dump_type);
    // Nothing needed — SQLite WAL checkpoint handles durability.
}

void CMailMod::dump_complete_signal(void)
{
    // Nothing.
}

void CMailMod::shutdown(void)
{
    CloseDatabase();

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("MAIL"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Mail module: shutdown event."));
            m_pILog->end_log();
        }
    }
}

void CMailMod::dbck(void)
{
    // TODO: Mail consistency checks.
}

void CMailMod::connect(dbref player, int isnew, int num)
{
    UNUSED_PARAMETER(isnew);
    UNUSED_PARAMETER(num);
    PlayerConnect(player);
}

void CMailMod::disconnect(dbref player, int num)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(num);
}

void CMailMod::data_create(dbref object)
{
    UNUSED_PARAMETER(object);
}

void CMailMod::data_clone(dbref clone, dbref source)
{
    UNUSED_PARAMETER(clone);
    UNUSED_PARAMETER(source);
}

void CMailMod::data_free(dbref object)
{
    // TODO: Enable when module fully owns mail data.
    UNUSED_PARAMETER(object);
}

// ---------------------------------------------------------------------------
// CMailModFactory — boilerplate.
// ---------------------------------------------------------------------------

CMailModFactory::CMailModFactory(void) : m_cRef(1)
{
}

CMailModFactory::~CMailModFactory()
{
}

MUX_RESULT CMailModFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CMailModFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CMailModFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CMailModFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CMailMod *pMailMod = nullptr;
    try
    {
        pMailMod = new CMailMod;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pMailMod)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pMailMod->FinalConstruct();
    if (MUX_FAILED(mr))
    {
        pMailMod->Release();
        return mr;
    }

    mr = pMailMod->QueryInterface(iid, ppv);
    pMailMod->Release();
    return mr;
}

MUX_RESULT CMailModFactory::LockServer(bool bLock)
{
    if (bLock)
    {
        g_cServerLocks++;
    }
    else
    {
        g_cServerLocks--;
    }
    return MUX_S_OK;
}
