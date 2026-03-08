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
#include <cctype>

// En-dash line for mail display borders.
//
static const UTF8 *MOD_DASH_LINE =
    T("\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93");

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
    m_pIMailDelivery(nullptr),
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

    mux_CreateInstance(CID_MailDelivery, nullptr, UseSameProcess,
                       IID_IMailDelivery,
                       reinterpret_cast<void **>(&m_pIMailDelivery));

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

    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->Release();
        m_pIMailDelivery = nullptr;
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

// ---------------------------------------------------------------------------
// Selector parsing and matching — ported from mail.cpp.
// ---------------------------------------------------------------------------

static const UTF8 *mailmsg[] =
{
    T("MAIL: Invalid message range"),
    T("MAIL: Invalid message number"),
    T("MAIL: Invalid age"),
    T("MAIL: Invalid dbref #"),
    T("MAIL: Invalid player"),
    T("MAIL: Invalid message specification"),
    T("MAIL: Invalid player or trying to send @mail to a @malias without a subject"),
};

#define MAIL_INVALID_RANGE  0
#define MAIL_INVALID_NUMBER 1
#define MAIL_INVALID_AGE    2
#define MAIL_INVALID_DBREF  3
#define MAIL_INVALID_PLAYER 4
#define MAIL_INVALID_SPEC   5
#define MAIL_INVALID_PLAYER_OR_USING_MALIAS 6

bool CMailMod::parse_msglist(const UTF8 *msglist, struct mail_selector *ms,
    dbref player)
{
    ms->low = 0;
    ms->high = 0;
    ms->flags = 0x0FFF | M_MSUNREAD;
    ms->player = 0;
    ms->days = -1;
    ms->day_comp = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        return true;
    }

    const UTF8 *p = msglist;
    while (isspace(*p))
    {
        p++;
    }

    if ('\0' == *p)
    {
        return true;
    }

    if (isdigit(*p))
    {
        const char *q = strchr(reinterpret_cast<const char *>(p), '-');
        if (q)
        {
            q++;
            ms->low = atol(reinterpret_cast<const char *>(p));
            if (ms->low <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            if ('\0' == *q)
            {
                ms->high = 0;
            }
            else
            {
                ms->high = atol(q);
                if (ms->low > ms->high)
                {
                    m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                    return false;
                }
            }
        }
        else
        {
            ms->low = ms->high = atol(reinterpret_cast<const char *>(p));
            if (ms->low <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_NUMBER]);
                return false;
            }
        }
    }
    else
    {
        switch (*p)
        {
        case '-':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            ms->high = atol(reinterpret_cast<const char *>(p));
            if (ms->high <= 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            break;

        case '~':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 0;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '<':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = -1;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '>':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 1;
            ms->days = atol(reinterpret_cast<const char *>(p));
            if (ms->days < 0)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '#':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_DBREF]);
                return false;
            }
            ms->player = atol(reinterpret_cast<const char *>(p));
            break;

        case '*':
            // From player name — use IObjectInfo::MatchThing.
            //
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_PLAYER]);
                return false;
            }
            {
                UTF8 namebuf[MOD_LBUF_SIZE];
                namebuf[0] = '*';
                strncpy(reinterpret_cast<char *>(namebuf + 1),
                        reinterpret_cast<const char *>(p),
                        MOD_LBUF_SIZE - 2);
                namebuf[MOD_LBUF_SIZE - 1] = '\0';
                dbref result = NOTHING;
                if (nullptr != m_pIObjectInfo)
                {
                    m_pIObjectInfo->MatchThing(player, namebuf, &result);
                }
                if (NOTHING == result)
                {
                    m_pINotify->RawNotify(player,
                        mailmsg[MAIL_INVALID_PLAYER_OR_USING_MALIAS]);
                    return false;
                }
                ms->player = result;
            }
            break;

        case 'a':
        case 'A':
            p++;
            if ('\0' == *p || ((*p != 'l' && *p != 'L')))
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            p++;
            if ('\0' == *p || ((*p != 'l' && *p != 'L')))
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            p++;
            if ('\0' == *p)
            {
                ms->flags = M_ALL;
            }
            else
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'u':
        case 'U':
            p++;
            if ('\0' == *p)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: U is ambiguous (urgent or unread?)"));
                return false;
            }
            if ('r' == *p || 'R' == *p)
            {
                ms->flags = M_URGENT;
            }
            else if ('n' == *p || 'N' == *p)
            {
                ms->flags = M_MSUNREAD;
            }
            else
            {
                m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'r':
        case 'R':
            ms->flags = M_ISREAD;
            break;

        case 'c':
        case 'C':
            ms->flags = M_CLEARED;
            break;

        case 't':
        case 'T':
            ms->flags = M_TAG;
            break;

        case 'm':
        case 'M':
            ms->flags = M_MASS;
            break;

        case 'f':
        case 'F':
            ms->flags = M_FORWARD;
            break;

        default:
            m_pINotify->RawNotify(player, mailmsg[MAIL_INVALID_SPEC]);
            return false;
        }
    }
    return true;
}

static int sign(int x)
{
    if (x == 0) return 0;
    return (x < 0) ? -1 : 1;
}

bool CMailMod::mail_match(struct mail *mp, struct mail_selector &ms, int num)
{
    if (ms.low && num < ms.low)
    {
        return false;
    }
    if (ms.high && ms.high < num)
    {
        return false;
    }
    if (ms.player && mp->from != ms.player)
    {
        return false;
    }

    mail_flag mpflag = Read(mp)
        ? (mp->read | M_ALL)
        : (mp->read | M_ALL | M_MSUNREAD);

    if ((ms.flags & mpflag) == 0)
    {
        return false;
    }

    if (ms.days == -1)
    {
        return true;
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    CLinearTimeAbsolute ltaMail;
    if (ltaMail.SetString(mp->time))
    {
        CLinearTimeDelta ltd(ltaMail, ltaNow);
        int iDiffDays = ltd.ReturnDays();
        if (sign(iDiffDays - ms.days) == ms.day_comp)
        {
            return true;
        }
    }
    return false;
}

int CMailMod::player_folder(dbref player)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return 0;
    }

    UTF8 buf[64];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailcurf"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return 0;
    }
    buf[nLen] = '\0';
    return atoi(reinterpret_cast<const char *>(buf));
}

void CMailMod::set_player_folder(dbref player, int fnum)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return;
    }
    UTF8 buf[16];
    snprintf(reinterpret_cast<char *>(buf), sizeof(buf), "%d", fnum);
    m_pIAttributeAccess->SetAttribute(1, player, T("Mailcurf"), buf);
}

const UTF8 *CMailMod::get_folder_name(dbref player, int fld)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return T("unnamed");
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr) || 0 == nFolders)
    {
        return T("unnamed");
    }
    aFolders[nFolders] = '\0';

    // Build pattern "N:" where N is the folder number.
    //
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "%d:", fld);
    size_t nPattern = strlen(pattern);

    const char *found = strstr(reinterpret_cast<const char *>(aFolders), pattern);
    if (nullptr == found)
    {
        return T("unnamed");
    }

    static UTF8 result[FOLDER_NAME_LEN + 1];
    const char *p = found + nPattern;
    char *q = reinterpret_cast<char *>(result);
    while (*p && *p != ':' && q < reinterpret_cast<char *>(result) + FOLDER_NAME_LEN)
    {
        *q++ = *p++;
    }
    *q = '\0';
    return result;
}

int CMailMod::get_folder_number(dbref player, const UTF8 *name)
{
    if (nullptr == m_pIAttributeAccess || nullptr == name || '\0' == *name)
    {
        return -1;
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr) || 0 == nFolders)
    {
        return -1;
    }
    aFolders[nFolders] = '\0';

    // Build upper-case pattern ":NAME:".
    //
    char pattern[FOLDER_NAME_LEN + 4];
    pattern[0] = ':';
    size_t i = 1;
    const char *s = reinterpret_cast<const char *>(name);
    while (*s && i < sizeof(pattern) - 2)
    {
        pattern[i++] = toupper(*s);
        s++;
    }
    pattern[i++] = ':';
    pattern[i] = '\0';

    // Need upper-case version of aFolders for case-insensitive match.
    //
    char upper[MOD_LBUF_SIZE];
    for (size_t k = 0; k <= nFolders; k++)
    {
        upper[k] = toupper(aFolders[k]);
    }

    const char *found = strstr(upper, pattern);
    if (nullptr == found)
    {
        return -1;
    }

    // The folder number follows the closing ':'.
    //
    const char *p = found + strlen(pattern);
    while (*p && isspace(*p))
    {
        p++;
    }
    if ('#' == *p)
    {
        p++;
    }
    return atoi(p);
}

int CMailMod::parse_folder(dbref player, const UTF8 *folder_string)
{
    if (nullptr == folder_string || '\0' == *folder_string)
    {
        return -1;
    }
    if (isdigit(*folder_string))
    {
        int fnum = atoi(reinterpret_cast<const char *>(folder_string));
        if (fnum < 0 || fnum > MAX_FOLDERS)
        {
            return -1;
        }
        return fnum;
    }
    return get_folder_number(player, folder_string);
}

void CMailMod::add_folder_name(dbref player, int fld, const UTF8 *name)
{
    if (nullptr == m_pIAttributeAccess)
    {
        return;
    }

    UTF8 aFolders[MOD_LBUF_SIZE];
    size_t nFolders = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Mailfolders"), aFolders, sizeof(aFolders) - 1, &nFolders);
    if (MUX_FAILED(mr))
    {
        nFolders = 0;
    }
    aFolders[nFolders] = '\0';

    // Build the new record "N:NAME:N" upper-cased.
    //
    char record[FOLDER_NAME_LEN + 16];
    snprintf(record, sizeof(record), "%d:", fld);
    size_t rlen = strlen(record);
    const char *s = reinterpret_cast<const char *>(name);
    while (*s && rlen < sizeof(record) - 8)
    {
        record[rlen++] = toupper(*s);
        s++;
    }
    snprintf(record + rlen, sizeof(record) - rlen, ":%d", fld);

    if (0 == nFolders)
    {
        // No existing folders — just set the new record.
        //
        m_pIAttributeAccess->SetAttribute(1, player,
            T("Mailfolders"),
            reinterpret_cast<const UTF8 *>(record));
        return;
    }

    // Build pattern "N:" to find existing entry.
    //
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "%d:", fld);

    char *found = strstr(reinterpret_cast<char *>(aFolders), pattern);
    if (nullptr != found)
    {
        // Replace existing entry: skip to end of old record.
        //
        UTF8 aNew[MOD_LBUF_SIZE];
        size_t before = found - reinterpret_cast<char *>(aFolders);
        memcpy(aNew, aFolders, before);
        size_t nNew = before;

        // Skip old record (format "N:name:N").
        //
        char *end = found + strlen(pattern);
        while (*end && *end != ' ')
        {
            end++;
        }
        if (*end == ' ')
        {
            end++;
        }

        // Append new record.
        //
        size_t rsize = strlen(record);
        memcpy(aNew + nNew, record, rsize);
        nNew += rsize;

        // Append remainder.
        //
        size_t remain = nFolders - (end - reinterpret_cast<char *>(aFolders));
        if (remain > 0)
        {
            aNew[nNew++] = ' ';
            memcpy(aNew + nNew, end, remain);
            nNew += remain;
        }
        aNew[nNew] = '\0';

        m_pIAttributeAccess->SetAttribute(1, player, T("Mailfolders"), aNew);
    }
    else
    {
        // Append new record.
        //
        size_t nOld = strlen(reinterpret_cast<const char *>(aFolders));
        size_t nRec = strlen(record);
        if (nOld + 1 + nRec < MOD_LBUF_SIZE)
        {
            UTF8 aNew[MOD_LBUF_SIZE];
            memcpy(aNew, aFolders, nOld);
            aNew[nOld] = ' ';
            memcpy(aNew + nOld + 1, record, nRec + 1);
            m_pIAttributeAccess->SetAttribute(1, player,
                T("Mailfolders"), aNew);
        }
    }
}

bool CMailMod::mail_to_player(dbref player, struct mail *mp)
{
    if (mp->to != player)
    {
        return false;
    }
    // Check that the mail postdates the player's creation (recycled dbref guard).
    //
    if (nullptr == m_pIAttributeAccess)
    {
        return true;
    }
    UTF8 buf[128];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Created"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return false;
    }
    buf[nLen] = '\0';
    CLinearTimeAbsolute ltaCreated, ltaMail;
    if (  ltaCreated.SetString(buf)
       && ltaMail.SetString(mp->time))
    {
        return ltaCreated <= ltaMail;
    }
    return false;
}

bool CMailMod::mail_from_player(dbref player, struct mail *mp)
{
    if (mp->from != player)
    {
        return false;
    }
    if (nullptr == m_pIAttributeAccess)
    {
        return true;
    }
    UTF8 buf[128];
    size_t nLen = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
        T("Created"), buf, sizeof(buf) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen)
    {
        return false;
    }
    buf[nLen] = '\0';
    CLinearTimeAbsolute ltaCreated, ltaMail;
    if (  ltaCreated.SetString(buf)
       && ltaMail.SetString(mp->time))
    {
        return ltaCreated <= ltaMail;
    }
    return false;
}

void CMailMod::count_mail_internal(dbref player, int folder,
    int *rcount, int *ucount, int *ccount)
{
    int rc = 0, uc = 0, cc = 0;

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (  Folder(mp) == folder
           && mail_to_player(player, mp))
        {
            if (Read(mp))
            {
                rc++;
            }
            else
            {
                uc++;
            }
            if (Cleared(mp))
            {
                cc++;
            }
        }
        mp = MailListNext(mp, player);
    }
    *rcount = rc;
    *ucount = uc;
    *ccount = cc;
}

void CMailMod::urgent_mail_internal(dbref player, int folder, int *ucount)
{
    int uc = 0;
    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (  Folder(mp) == folder
           && mail_to_player(player, mp))
        {
            if (Unread(mp) && Urgent(mp))
            {
                uc++;
            }
        }
        mp = MailListNext(mp, player);
    }
    *ucount = uc;
}

// ---------------------------------------------------------------------------
// Folder commands.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_change_folder(dbref player, const UTF8 *fld,
    const UTF8 *newname)
{
    if (nullptr == fld || '\0' == *fld)
    {
        DoListMailBrief(player);
        return;
    }

    int pfld = parse_folder(player, fld);
    if (pfld < 0)
    {
        m_pINotify->RawNotify(player, T("MAIL: What folder is that?"));
        return;
    }

    if (nullptr != newname && '\0' != *newname)
    {
        // Rename folder.
        //
        if (strlen(reinterpret_cast<const char *>(newname)) > FOLDER_NAME_LEN)
        {
            m_pINotify->RawNotify(player, T("MAIL: Folder name too long"));
            return;
        }
        const UTF8 *p;
        for (p = newname; isalnum(*p); p++)
            ;
        if (*p != '\0')
        {
            m_pINotify->RawNotify(player, T("MAIL: Illegal folder name"));
            return;
        }

        add_folder_name(player, pfld, newname);

        UTF8 msg[128];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Folder %d now named \xE2\x80\x98%s\xE2\x80\x99",
                 pfld, reinterpret_cast<const char *>(newname));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        // Switch to folder.
        //
        set_player_folder(player, pfld);

        UTF8 msg[128];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Current folder set to %d [%s].",
                 pfld,
                 reinterpret_cast<const char *>(get_folder_name(player, pfld)));
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::DoListMailBrief(dbref player)
{
    for (int folder = 0; folder < MAX_FOLDERS; folder++)
    {
        int rc, uc, cc, gc;
        count_mail_internal(player, folder, &rc, &uc, &cc);
        urgent_mail_internal(player, folder, &gc);

        if (rc + uc > 0)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).",
                     rc + uc, folder,
                     reinterpret_cast<const char *>(get_folder_name(player, folder)),
                     uc, cc);
            m_pINotify->RawNotify(player, msg);
        }
        else
        {
            const UTF8 *fname = get_folder_name(player, folder);
            if (0 != strcmp(reinterpret_cast<const char *>(fname), "unnamed"))
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "MAIL: 0 messages in folder %d [%s].",
                         folder, reinterpret_cast<const char *>(fname));
                m_pINotify->RawNotify(player, msg);
            }
        }

        if (gc > 0)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "URGENT MAIL: You have %d urgent messages in folder %d [%s].",
                     gc, folder,
                     reinterpret_cast<const char *>(get_folder_name(player, folder)));
            m_pINotify->RawNotify(player, msg);
        }
    }

    int current_folder = player_folder(player);

    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: Current folder is %d [%s].",
             current_folder,
             reinterpret_cast<const char *>(get_folder_name(player, current_folder)));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::ListMailInFolderNumber(dbref player, int folder_num,
    const UTF8 *msglist)
{
    int original_folder = player_folder(player);
    set_player_folder(player, folder_num);

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        set_player_folder(player, original_folder);
        return;
    }

    UTF8 hdr[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "   MAIL: Folder %d   "
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93",
             folder_num);
    m_pINotify->RawNotify(player, hdr);

    int i = 0;
    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (Folder(mp) == folder_num)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                UTF8 *time = mail_list_time(mp->time);
                size_t nSize = MessageFetchSize(mp->number);

                UTF8 szFromName[64];
                get_player_name(mp->from, szFromName, sizeof(szFromName));

                UTF8 line[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(line), sizeof(line),
                    "[%s] %-3d (%4zu) From: %-16s Sub: %.25s",
                    reinterpret_cast<const char *>(status_chars(mp)),
                    i, nSize,
                    reinterpret_cast<const char *>(szFromName),
                    reinterpret_cast<const char *>(mp->subject));
                m_pINotify->RawNotify(player, line);
            }
        }
        mp = MailListNext(mp, player);
    }
    m_pINotify->RawNotify(player, MOD_DASH_LINE);

    set_player_folder(player, original_folder);
}

void CMailMod::ListMailInFolder(dbref player, const UTF8 *folder_name,
    const UTF8 *msglist)
{
    int folder = 0;

    if (nullptr == folder_name || '\0' == *folder_name)
    {
        folder = player_folder(player);
    }
    else
    {
        folder = parse_folder(player, folder_name);
    }

    if (-1 == folder)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such folder."));
        return;
    }
    ListMailInFolderNumber(player, folder, msglist);
}

// ---------------------------------------------------------------------------
// @mail/review — review sent mail.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_review_all(dbref player, const UTF8 *msglist)
{
    int i = 0, j = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        // Summary mode: list all sent messages grouped by recipient.
        //
        for (auto &kv : m_mail_htab)
        {
            dbref target = kv.first;
            bool bHeader = false;

            struct mail *mp = MailListFirst(target);
            while (nullptr != mp)
            {
                if (mail_from_player(player, mp))
                {
                    i++;

                    if (!bHeader)
                    {
                        UTF8 szName[64];
                        get_player_name(target, szName, sizeof(szName));

                        UTF8 hdr[MOD_LBUF_SIZE];
                        snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "   To: %-25s   "
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93",
                                 reinterpret_cast<const char *>(szName));
                        m_pINotify->RawNotify(player, hdr);
                        bHeader = true;
                    }

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    size_t nSize = MessageFetchSize(mp->number);
                    UTF8 line[MOD_LBUF_SIZE];
                    snprintf(reinterpret_cast<char *>(line), sizeof(line),
                             "[%s] %-3d (%4zu) From: %-16s Sub: %.25s",
                             reinterpret_cast<const char *>(status_chars(mp)),
                             i, nSize,
                             reinterpret_cast<const char *>(szFromName),
                             reinterpret_cast<const char *>(mp->subject));
                    m_pINotify->RawNotify(player, line);
                }
                mp = MailListNext(mp, target);
            }
        }

        if (0 == i)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You have no matching messages."));
        }
        else
        {
            m_pINotify->RawNotify(player, MOD_DASH_LINE);
        }
    }
    else
    {
        // Detail mode: show full messages matching msglist.
        //
        struct mail_selector ms;
        if (!parse_msglist(msglist, &ms, player))
        {
            return;
        }

        for (auto &kv : m_mail_htab)
        {
            dbref target = kv.first;

            struct mail *mp = MailListFirst(target);
            while (nullptr != mp)
            {
                if (mail_from_player(player, mp))
                {
                    i++;
                    if (mail_match(mp, ms, i))
                    {
                        j++;
                        const UTF8 *body = MessageFetch(mp->number);

                        UTF8 szFromName[64];
                        get_player_name(mp->from, szFromName, sizeof(szFromName));

                        m_pINotify->RawNotify(player, MOD_DASH_LINE);

                        UTF8 hdr[MOD_LBUF_SIZE];
                        snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
                            "%-3d         From:  %-16s  At: %-25s  %s\r\n"
                            "Fldr   : %-2d Status: %s%s%s%s%s%s%s\r\n"
                            "Subject: %.65s",
                            i,
                            reinterpret_cast<const char *>(szFromName),
                            reinterpret_cast<const char *>(mp->time),
                            is_connected_visible(mp->from, player) ? " (Conn)" : "      ",
                            0,
                            Read(mp)     ? "Read"    : "Unread",
                            Cleared(mp)  ? " Cleared" : "",
                            Urgent(mp)   ? " Urgent"  : "",
                            Mass(mp)     ? " Mass"    : "",
                            Forward(mp)  ? " Forward" : "",
                            M_Safe(mp)   ? " Safe"    : "",
                            Tagged(mp)   ? " Tagged"  : "",
                            reinterpret_cast<const char *>(mp->subject));
                        m_pINotify->RawNotify(player, hdr);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                        m_pINotify->RawNotify(player, body);
                        m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    }
                }
                mp = MailListNext(mp, target);
            }
        }

        if (!j)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You don\xE2\x80\x99t have that many matching messages!"));
        }
    }
}

void CMailMod::do_mail_review(dbref player, const UTF8 *name,
    const UTF8 *msglist)
{
    if (  nullptr != name
       && '\0' != *name
       && 0 == strcasecmp(reinterpret_cast<const char *>(name), "all"))
    {
        do_mail_review_all(player, msglist);
        return;
    }

    // Look up the target player.
    //
    if (nullptr == name || '\0' == *name)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        // Prefix with '*' for player lookup.
        //
        UTF8 namebuf[256];
        snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                 "*%s", reinterpret_cast<const char *>(name));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }

    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    int i = 0, j = 0;

    if (nullptr == msglist || '\0' == *msglist)
    {
        // Summary: list mail sent to target.
        //
        UTF8 szName[64];
        get_player_name(target, szName, sizeof(szName));

        UTF8 hdr[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "   To: %-25s   "
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
                 "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93",
                 reinterpret_cast<const char *>(szName));
        m_pINotify->RawNotify(player, hdr);

        struct mail *mp = MailListFirst(target);
        while (nullptr != mp)
        {
            if (mail_from_player(player, mp))
            {
                i++;

                UTF8 szFromName[64];
                get_player_name(mp->from, szFromName, sizeof(szFromName));

                size_t nSize = MessageFetchSize(mp->number);
                UTF8 line[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(line), sizeof(line),
                         "[%s] %-3d (%4zu) From: %-16s Sub: %.25s",
                         reinterpret_cast<const char *>(status_chars(mp)),
                         i, nSize,
                         reinterpret_cast<const char *>(szFromName),
                         reinterpret_cast<const char *>(mp->subject));
                m_pINotify->RawNotify(player, line);
            }
            mp = MailListNext(mp, target);
        }
        m_pINotify->RawNotify(player, MOD_DASH_LINE);
    }
    else
    {
        // Detail mode: show full messages.
        //
        struct mail_selector ms;
        if (!parse_msglist(msglist, &ms, target))
        {
            return;
        }

        struct mail *mp = MailListFirst(target);
        while (nullptr != mp)
        {
            if (mail_from_player(player, mp))
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    const UTF8 *body = MessageFetch(mp->number);

                    UTF8 szFromName[64];
                    get_player_name(mp->from, szFromName, sizeof(szFromName));

                    m_pINotify->RawNotify(player, MOD_DASH_LINE);

                    UTF8 hdr2[MOD_LBUF_SIZE];
                    snprintf(reinterpret_cast<char *>(hdr2), sizeof(hdr2),
                        "%-3d         From:  %-16s  At: %-25s  %s\r\n"
                        "Fldr   : %-2d Status: %s%s%s%s%s%s%s\r\n"
                        "Subject: %.65s",
                        i,
                        reinterpret_cast<const char *>(szFromName),
                        reinterpret_cast<const char *>(mp->time),
                        is_connected_visible(mp->from, player) ? " (Conn)" : "      ",
                        0,
                        Read(mp)     ? "Read"    : "Unread",
                        Cleared(mp)  ? " Cleared" : "",
                        Urgent(mp)   ? " Urgent"  : "",
                        Mass(mp)     ? " Mass"    : "",
                        Forward(mp)  ? " Forward" : "",
                        M_Safe(mp)   ? " Safe"    : "",
                        Tagged(mp)   ? " Tagged"  : "",
                        reinterpret_cast<const char *>(mp->subject));
                    m_pINotify->RawNotify(player, hdr2);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);
                    m_pINotify->RawNotify(player, body);
                    m_pINotify->RawNotify(player, MOD_DASH_LINE);
                }
            }
            mp = MailListNext(mp, target);
        }

        if (!j)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You don\xE2\x80\x99t have that many matching messages!"));
        }
    }
}

// ---------------------------------------------------------------------------
// @mail/retract — retract unread sent mail.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_retract(dbref player, const UTF8 *name,
    const UTF8 *msglist)
{
    if (nullptr == name || '\0' == *name)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    // Handle malias target.
    //
    if (*name == '*')
    {
        int nResult;
        malias_t *m = get_malias(player, name, &nResult);
        if (nResult == GMA_NOTFOUND)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "MAIL: Mail alias %s not found.",
                     reinterpret_cast<const char *>(name));
            m_pINotify->RawNotify(player, msg);
            return;
        }
        if (nResult == GMA_FOUND)
        {
            for (int k = 0; k < m->numrecep; k++)
            {
                UTF8 dbrefbuf[32];
                snprintf(reinterpret_cast<char *>(dbrefbuf),
                         sizeof(dbrefbuf), "#%d", m->list[k]);
                do_mail_retract(player, dbrefbuf, msglist);
            }
        }
        return;
    }

    // Look up target player.
    //
    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                 "*%s", reinterpret_cast<const char *>(name));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }

    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: No such player."));
        return;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, target))
    {
        return;
    }

    int i = 0, j = 0;
    struct mail *mp = MailListFirst(target);
    while (nullptr != mp)
    {
        struct mail *next = MailListNext(mp, target);
        if (mail_from_player(player, mp))
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                if (Unread(mp))
                {
                    sqlite_wt_delete_mail(mp);
                    MailListRemove(target, mp);
                    m_pINotify->RawNotify(player, T("MAIL: Mail retracted."));
                }
                else
                {
                    m_pINotify->RawNotify(player,
                        T("MAIL: That message has been read."));
                }
            }
        }
        mp = next;
    }

    if (!j)
    {
        m_pINotify->RawNotify(player, T("MAIL: No matching messages."));
    }
}

// ---------------------------------------------------------------------------
// @mail/nuke — admin: clear all mail in the database.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_nuke(dbref player)
{
    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(player, &bGod);
    }
    if (!bGod)
    {
        m_pINotify->RawNotify(player,
            T("The postal service issues a warrant for your arrest."));
        return;
    }

    // Remove all mail from all players.
    //
    for (auto it = m_mail_htab.begin(); it != m_mail_htab.end(); )
    {
        dbref target = it->first;
        ++it;
        MailListRemoveAll(target);
        sqlite_wt_delete_all_mail(target);
    }

    UTF8 msg[128];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: ** MAIL PURGE ** done by #%d.", player);
    if (nullptr != m_pILog)
    {
        m_pILog->log_text(msg);
    }
    m_pINotify->RawNotify(player,
        T("You annihilate the post office. All messages cleared."));
}

// ---------------------------------------------------------------------------
// Delivery pipeline — send_mail, mail_to_list, make_numlist, make_namelist.
// ---------------------------------------------------------------------------

struct mail *CMailMod::mail_fetch(dbref player, int num)
{
    int i = 0;
    int fld = player_folder(player);
    struct mail *mp;
    for (mp = MailListFirst(player); nullptr != mp;
         mp = MailListNext(mp, player))
    {
        if (  Folder(mp) == fld
           && mail_to_player(player, mp))
        {
            i++;
            if (i == num)
            {
                return mp;
            }
        }
    }
    return nullptr;
}

void CMailMod::send_mail
(
    dbref player,
    dbref target,
    const UTF8 *tolist,
    const UTF8 *subject,
    int number,
    mail_flag flags,
    bool silent
)
{
    // Verify target is a player.
    //
    bool bIsPlayer = false;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsPlayer(target, &bIsPlayer);
    }
    if (!bIsPlayer)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You cannot send mail to non-existent people."));
        }
        return;
    }

    // Check mail lock via IMailDelivery.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bOk = false;
        m_pIMailDelivery->MailCheck(player, target, &bOk);
        if (!bOk)
        {
            return;
        }
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    const UTF8 *pTimeStr = ltaNow.ReturnDateString(0);

    // Initialize the mail structure.
    //
    struct mail *newp = nullptr;
    try
    {
        newp = new struct mail;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == newp)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: Out of memory."));
        }
        return;
    }

    newp->to = target;
    newp->sqlite_id = -1;

    // Determine sender: if player is not a player, use owner if owner
    // is a wizard.
    //
    if (bIsPlayer)
    {
        // Check if player itself is a player.
        //
        bool bSenderIsPlayer = false;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(player, &bSenderIsPlayer);
        }
        if (bSenderIsPlayer)
        {
            newp->from = player;
        }
        else
        {
            dbref mailbag = -1;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->GetOwner(player, &mailbag);
            }
            bool bWiz = false;
            if (nullptr != m_pIPermissions && mailbag >= 0)
            {
                m_pIPermissions->IsWizard(mailbag, &bWiz);
            }
            newp->from = bWiz ? player : mailbag;
        }
    }
    else
    {
        newp->from = player;
    }

    if (!tolist || tolist[0] == '\0')
    {
        newp->tolist = reinterpret_cast<UTF8 *>(strdup("*HIDDEN*"));
    }
    else
    {
        newp->tolist = reinterpret_cast<UTF8 *>(strdup(reinterpret_cast<const char *>(tolist)));
    }

    newp->number = number;
    MessageReferenceInc(number);
    newp->time = reinterpret_cast<UTF8 *>(strdup(reinterpret_cast<const char *>(pTimeStr)));
    newp->subject = reinterpret_cast<UTF8 *>(strdup(reinterpret_cast<const char *>(subject)));

    // Send to folder 0.
    //
    newp->read = flags & M_FMASK;

    // Reject if the target's mailbox is full.
    //
    if (0 < m_mail_per_player)
    {
        // Check No_Mail_Expire — we don't have direct access to the power,
        // but Wizards always have it, and that's what the server checks too.
        //
        bool bWizTarget = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsWizard(target, &bWizTarget);
        }
        if (!bWizTarget)
        {
            int total = 0;
            struct mail *mp;
            for (mp = MailListFirst(target); nullptr != mp;
                 mp = MailListNext(mp, target))
            {
                total++;
            }
            if (total >= m_mail_per_player)
            {
                UTF8 targetname[MOD_LBUF_SIZE];
                get_player_name(target, targetname, sizeof(targetname));

                UTF8 msg[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                    "MAIL: %s\xE2\x80\x99s mailbox is full (%d messages).",
                    targetname, total);
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }

                MessageReferenceDec(newp->number);
                free(newp->subject);
                free(newp->tolist);
                free(newp->time);
                delete newp;
                return;
            }
        }
    }

    // Append to target's mail list and persist.
    //
    MailListAppend(target, newp);
    sqlite_wt_insert_mail(newp);

    // Notify via IMailDelivery.
    //
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->NotifyDelivery(newp->from, target, subject, silent);
    }
}

// make_numlist — resolve space-separated player names/aliases to dbref numbers.
//
// Returns a malloc'd string of space-separated dbref numbers, or nullptr
// on error. Caller must free() the result.
//
UTF8 *CMailMod::make_numlist(dbref player, const UTF8 *arg, bool bBlind)
{
    if (!arg || !*arg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No players specified."));
        }
        return nullptr;
    }

    dbref aRecip[4000];
    int nRecip = 0;

    // Work on a mutable copy.
    //
    UTF8 buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(buf),
            reinterpret_cast<const char *>(arg), MOD_LBUF_SIZE - 1);
    buf[MOD_LBUF_SIZE - 1] = '\0';

    UTF8 *head = buf;
    while (head && *head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }
        UTF8 spot = *tail;
        *tail = '\0';

        if (*head == '*')
        {
            // Malias expansion.
            //
            int nResult;
            malias_t *m = get_malias(player, head, &nResult);
            if (nResult == GMA_NOTFOUND)
            {
                UTF8 msg[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                    "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 does not exist.",
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return nullptr;
            }
            else if (nResult == GMA_INVALIDFORM)
            {
                UTF8 msg[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                    "MAIL: \xE2\x80\x98%s\xE2\x80\x99 is a badly-formed alias.",
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return nullptr;
            }
            for (int i = 0; i < m->numrecep && nRecip < 4000; i++)
            {
                aRecip[nRecip++] = m->list[i];
            }
        }
        else
        {
            // Player lookup via IObjectInfo::MatchThing.
            //
            UTF8 lookup[MOD_LBUF_SIZE];
            if (*head != '*')
            {
                snprintf(reinterpret_cast<char *>(lookup), sizeof(lookup),
                    "*%s", reinterpret_cast<const char *>(head));
            }
            else
            {
                strncpy(reinterpret_cast<char *>(lookup),
                        reinterpret_cast<const char *>(head), sizeof(lookup) - 1);
                lookup[sizeof(lookup) - 1] = '\0';
            }

            dbref target = NOTHING;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->MatchThing(player, lookup, &target);
            }
            if (target >= 0)
            {
                if (nRecip < 4000)
                {
                    aRecip[nRecip++] = target;
                }
            }
            else
            {
                UTF8 msg[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                    "MAIL: \xE2\x80\x98%s\xE2\x80\x99 does not exist.",
                    reinterpret_cast<const char *>(head));
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player, msg);
                }
                return nullptr;
            }
        }

        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }

    if (nRecip <= 0)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No players specified."));
        }
        return nullptr;
    }

    // De-duplicate and build result string.
    //
    UTF8 result[MOD_LBUF_SIZE];
    UTF8 *rp = result;
    for (int i = 0; i < nRecip; i++)
    {
        if (aRecip[i] != NOTHING)
        {
            // Remove duplicates.
            //
            for (int j = i + 1; j < nRecip; j++)
            {
                if (aRecip[i] == aRecip[j])
                {
                    aRecip[j] = NOTHING;
                }
            }

            if (rp != result)
            {
                *rp++ = ' ';
            }
            if (bBlind)
            {
                *rp++ = '!';
            }
            int n = snprintf(reinterpret_cast<char *>(rp),
                sizeof(result) - (rp - result),
                "%d", aRecip[i]);
            rp += n;
        }
    }
    *rp = '\0';

    return reinterpret_cast<UTF8 *>(strdup(reinterpret_cast<const char *>(result)));
}

// make_namelist — convert space-separated dbref numbers to player names.
//
// Returns a malloc'd string. Caller must free().
//
UTF8 *CMailMod::make_namelist(dbref player, const UTF8 *arg)
{
    UNUSED_PARAMETER(player);

    if (!arg || !*arg)
    {
        return reinterpret_cast<UTF8 *>(strdup(""));
    }

    UTF8 buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(buf),
            reinterpret_cast<const char *>(arg), MOD_LBUF_SIZE - 1);
    buf[MOD_LBUF_SIZE - 1] = '\0';

    UTF8 result[MOD_LBUF_SIZE];
    UTF8 *rp = result;
    bool bFirst = true;

    char *saveptr = nullptr;
    char *token = strtok_r(reinterpret_cast<char *>(buf), " ", &saveptr);
    while (token)
    {
        if (!bFirst)
        {
            size_t remain = sizeof(result) - (rp - result);
            if (remain > 2)
            {
                *rp++ = ',';
                *rp++ = ' ';
            }
        }
        bFirst = false;

        char *p = token;
        bool bBCC = false;
        if (*p == '!')
        {
            bBCC = true;
            p++;
        }

        if (isdigit(static_cast<unsigned char>(*p)))
        {
            dbref target = atoi(p);
            UTF8 name[MOD_LBUF_SIZE];
            get_player_name(target, name, sizeof(name));

            if (bBCC)
            {
                size_t remain = sizeof(result) - (rp - result);
                if (remain > 1)
                {
                    *rp++ = '!';
                }
            }
            size_t nlen = strlen(reinterpret_cast<const char *>(name));
            size_t remain = sizeof(result) - (rp - result);
            if (nlen < remain)
            {
                memcpy(rp, name, nlen);
                rp += nlen;
            }
        }
        else
        {
            size_t tlen = strlen(token);
            size_t remain = sizeof(result) - (rp - result);
            if (tlen < remain)
            {
                memcpy(rp, token, tlen);
                rp += tlen;
            }
        }

        token = strtok_r(nullptr, " ", &saveptr);
    }
    *rp = '\0';

    return reinterpret_cast<UTF8 *>(strdup(reinterpret_cast<const char *>(result)));
}

// mail_to_list — send mail to all recipients in a numlist.
//
void CMailMod::mail_to_list(dbref player, UTF8 *list, const UTF8 *subject,
    const UTF8 *message, mail_flag flags, bool silent)
{
    if (!list)
    {
        return;
    }
    if (!*list)
    {
        free(list);
        return;
    }

    // Build tolist (excluding BCC) and senderlist (including BCC).
    //
    UTF8 tolist[MOD_LBUF_SIZE];
    UTF8 *p = tolist;
    UTF8 senderlist[MOD_LBUF_SIZE];
    UTF8 *sp = senderlist;
    UTF8 *head = list;

    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }

        // Append to senderlist.
        //
        size_t seglen = tail - head;
        if (sp != senderlist)
        {
            size_t remain = sizeof(senderlist) - (sp - senderlist);
            if (remain > 1)
            {
                *sp++ = ' ';
            }
        }
        {
            size_t remain = sizeof(senderlist) - (sp - senderlist);
            if (seglen < remain)
            {
                memcpy(sp, head, seglen);
                sp += seglen;
            }
        }

        // Append to tolist if not BCC (!-prefixed).
        //
        if (*head != '!')
        {
            if (p != tolist)
            {
                size_t remain = sizeof(tolist) - (p - tolist);
                if (remain > 1)
                {
                    *p++ = ' ';
                }
            }
            size_t remain = sizeof(tolist) - (p - tolist);
            if (seglen < remain)
            {
                memcpy(p, head, seglen);
                p += seglen;
            }
        }

        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    *p = '\0';
    *sp = '\0';

    // Add the message body (with signature evaluation).
    //
    int number = add_mail_message(player, message);
    if (number == NOTHING)
    {
        free(list);
        return;
    }

    // Iterate recipients and send.
    //
    head = list;
    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        UTF8 *tail = head;
        while (*tail && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }
        UTF8 spot = *tail;
        *tail = '\0';

        if (*head == '!')
        {
            head++;
        }

        if (*head == '*')
        {
            // Malias — expand and send to each member.
            //
            int nResult;
            malias_t *m = get_malias(player, head, &nResult);
            if (nResult == GMA_FOUND && nullptr != m)
            {
                for (int i = 0; i < m->numrecep; i++)
                {
                    bool bTargetIsPlayer = false;
                    if (nullptr != m_pIObjectInfo)
                    {
                        m_pIObjectInfo->IsPlayer(m->list[i], &bTargetIsPlayer);
                    }
                    if (bTargetIsPlayer)
                    {
                        send_mail(player, m->list[i],
                            (m->list[i] == player) ? senderlist : tolist,
                            subject, number, flags, silent);
                    }
                }
            }
        }
        else
        {
            dbref target = atoi(reinterpret_cast<const char *>(head));
            bool bTargetIsPlayer = false;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsPlayer(target, &bTargetIsPlayer);
            }
            if (bTargetIsPlayer)
            {
                send_mail(player, target,
                    (target == player) ? senderlist : tolist,
                    subject, number, flags, silent);
            }
        }

        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    MessageReferenceDec(number);
    free(list);
}

// add_mail_message — store a message body, optionally appending signature.
//
// This is the module equivalent of the server's add_mail_message(). It reads
// A_SIGNATURE via IAttributeAccess and evaluates it via IEvaluator.
//
int CMailMod::add_mail_message(dbref player, const UTF8 *message)
{
    // Reject if message says "clear" (common user error).
    //
    if (0 == strcasecmp(reinterpret_cast<const char *>(message), "clear"))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You probably did not intend to send a @mail saying "
                  "\xE2\x80\x98" "clear\xE2\x80\x99."));
        }
        return NOTHING;
    }

    // Read and evaluate signature.
    //
    UTF8 sigraw[MOD_LBUF_SIZE];
    sigraw[0] = '\0';
    if (nullptr != m_pIAttributeAccess)
    {
        size_t nLen = 0;
        MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, player,
            T("Signature"), sigraw, sizeof(sigraw) - 1, &nLen);
        if (MUX_SUCCEEDED(mr))
        {
            sigraw[nLen] = '\0';
        }
        else
        {
            sigraw[0] = '\0';
        }
    }

    // Evaluate the signature if non-empty.
    //
    UTF8 sigeval[MOD_LBUF_SIZE];
    sigeval[0] = '\0';
    if (sigraw[0] != '\0' && nullptr != m_pIMailDelivery)
    {
        // Use IEvaluator if available; otherwise use raw signature.
        //
        mux_IEvaluator *pIEval = nullptr;
        MUX_RESULT mr = mux_CreateInstance(CID_Evaluator, nullptr,
            UseSameProcess, IID_IEvaluator,
            reinterpret_cast<void **>(&pIEval));
        if (MUX_SUCCEEDED(mr) && nullptr != pIEval)
        {
            size_t nResultLen = 0;
            pIEval->Eval(player, player, player, sigraw,
                sigeval, sizeof(sigeval) - 1, &nResultLen);
            sigeval[nResultLen] = '\0';
            pIEval->Release();
        }
        else
        {
            strncpy(reinterpret_cast<char *>(sigeval),
                    reinterpret_cast<const char *>(sigraw),
                    sizeof(sigeval) - 1);
            sigeval[sizeof(sigeval) - 1] = '\0';
        }
    }

    // Combine message + signature.
    //
    UTF8 combined[MOD_LBUF_SIZE];
    if (sigeval[0] != '\0')
    {
        snprintf(reinterpret_cast<char *>(combined), sizeof(combined),
            "%s %s",
            reinterpret_cast<const char *>(message),
            reinterpret_cast<const char *>(sigeval));
    }
    else
    {
        strncpy(reinterpret_cast<char *>(combined),
                reinterpret_cast<const char *>(message),
                sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    }

    return new_mail_message(combined, NOTHING);
}

// ---------------------------------------------------------------------------
// Composition commands.
// ---------------------------------------------------------------------------

void CMailMod::do_expmail_start(dbref player, const UTF8 *arg,
    const UTF8 *subject)
{
    if (!arg || !*arg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I do not know whom you want to mail."));
        }
        return;
    }
    if (!subject || !*subject)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No subject."));
        }
        return;
    }

    // Check if already composing.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bComposing = false;
        m_pIMailDelivery->IsComposing(player, &bComposing);
        if (bComposing)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Mail message already in progress."));
            }
            return;
        }
    }

    // Throttle check.
    //
    if (nullptr != m_pIMailDelivery)
    {
        bool bWiz = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsWizard(player, &bWiz);
        }
        if (!bWiz)
        {
            bool bThrottled = false;
            m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
            if (bThrottled)
            {
                if (nullptr != m_pINotify)
                {
                    m_pINotify->RawNotify(player,
                        T("MAIL: Too much @mail sent recently."));
                }
                return;
            }
        }
    }

    // Resolve recipients.
    //
    UTF8 *tolist = make_numlist(player, arg, false);
    if (!tolist)
    {
        return;
    }

    // Store composition state on player attributes.
    //
    if (nullptr != m_pIAttributeAccess)
    {
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailto"), tolist);
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailsub"), subject);
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailflags"), T("0"));
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailmsg"), T(""));
    }

    // Set composing flag.
    //
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, true);
    }

    // Notify with recipient names.
    //
    UTF8 *names = make_namelist(player, tolist);
    if (nullptr != m_pINotify && nullptr != names)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
            "MAIL: You are sending mail to \xE2\x80\x98%s\xE2\x80\x99.",
            reinterpret_cast<const char *>(names));
        m_pINotify->RawNotify(player, msg);
    }
    if (names)
    {
        free(names);
    }
    free(tolist);
}

void CMailMod::do_expmail_stop(dbref player, int flags)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message started."));
        }
        return;
    }

    // Read composition attributes.
    //
    UTF8 aTolist[MOD_LBUF_SIZE];
    aTolist[0] = '\0';
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    UTF8 aMailSub[MOD_LBUF_SIZE];
    aMailSub[0] = '\0';
    UTF8 aMailFlags[MOD_LBUF_SIZE];
    aMailFlags[0] = '\0';

    if (nullptr != m_pIAttributeAccess)
    {
        size_t nLen;
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailto"), aTolist, sizeof(aTolist) - 1, &nLen);
        aTolist[nLen] = '\0';

        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailmsg"), aMailMsg, sizeof(aMailMsg) - 1, &nLen);
        aMailMsg[nLen] = '\0';

        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailsub"), aMailSub, sizeof(aMailSub) - 1, &nLen);
        aMailSub[nLen] = '\0';

        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailflags"), aMailFlags, sizeof(aMailFlags) - 1, &nLen);
        aMailFlags[nLen] = '\0';
    }

    if (aTolist[0] == '\0')
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No recipients."));
        }
        return;
    }

    if (aMailMsg[0] == '\0')
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: The body of this message is empty.  Use - to add to the message."));
        }
        return;
    }

    // Build numlist copy for mail_to_list (which takes ownership).
    //
    UTF8 *tolist_copy = reinterpret_cast<UTF8 *>(
        strdup(reinterpret_cast<const char *>(aTolist)));

    int combinedFlags = flags | atoi(reinterpret_cast<const char *>(aMailFlags));
    mail_to_list(player, tolist_copy, aMailSub, aMailMsg, combinedFlags, false);

    // Clear composing flag.
    //
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, false);
    }
}

void CMailMod::do_expmail_abort(dbref player)
{
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->SetComposing(player, false);
    }
    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(player, T("MAIL: Message aborted."));
    }
}

void CMailMod::do_mail_quick(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    if (!arg1 || !*arg1)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don\xE2\x80\x99t know who you want to mail."));
        }
        return;
    }
    if (!arg2 || !*arg2)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message."));
        }
        return;
    }

    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    // Parse "recipients/subject" from arg1.
    //
    UTF8 bufDest[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(bufDest),
            reinterpret_cast<const char *>(arg1),
            sizeof(bufDest) - 1);
    bufDest[sizeof(bufDest) - 1] = '\0';

    UTF8 *pSubject = reinterpret_cast<UTF8 *>(
        strchr(reinterpret_cast<char *>(bufDest), '/'));
    if (!pSubject)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No subject."));
        }
        return;
    }
    *pSubject++ = '\0';

    UTF8 *numlist = make_numlist(player, bufDest, false);
    mail_to_list(player, numlist, pSubject, arg2, 0, false);
}

void CMailMod::do_mail_fwd(dbref player, const UTF8 *msg,
    const UTF8 *tolist)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }
    if (!msg || !*msg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message list."));
        }
        return;
    }
    if (!tolist || !*tolist)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: To whom should I forward?"));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    int num = atoi(reinterpret_cast<const char *>(msg));
    if (!num)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don\xE2\x80\x99t understand that message number."));
        }
        return;
    }

    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You can\xE2\x80\x99t forward non-existent messages."));
        }
        return;
    }

    // Build subject: "Original Subject (fwd from SenderName)"
    //
    UTF8 fromname[MOD_LBUF_SIZE];
    get_player_name(mp->from, fromname, sizeof(fromname));
    UTF8 subj[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(subj), sizeof(subj),
        "%s (fwd from %s)",
        reinterpret_cast<const char *>(mp->subject),
        reinterpret_cast<const char *>(fromname));

    do_expmail_start(player, tolist, subj);

    // Set message body to the forwarded message.
    //
    const UTF8 *body = MessageFetch(mp->number);
    if (nullptr != m_pIAttributeAccess && body)
    {
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailmsg"), body);
    }

    // Set M_FORWARD flag.
    //
    if (nullptr != m_pIAttributeAccess)
    {
        UTF8 aFlags[32];
        size_t nLen = 0;
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailflags"), aFlags, sizeof(aFlags) - 1, &nLen);
        aFlags[nLen] = '\0';
        int iFlag = M_FORWARD;
        if (aFlags[0])
        {
            iFlag |= atoi(reinterpret_cast<const char *>(aFlags));
        }
        UTF8 flagbuf[16];
        snprintf(reinterpret_cast<char *>(flagbuf), sizeof(flagbuf),
            "%d", iFlag);
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailflags"), flagbuf);
    }
}

void CMailMod::do_mail_reply(dbref player, const UTF8 *msg, bool all,
    int key)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail message already in progress."));
        }
        return;
    }
    if (!msg || !*msg)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message list."));
        }
        return;
    }

    // Throttle check.
    //
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    if (!bWiz && nullptr != m_pIMailDelivery)
    {
        bool bThrottled = false;
        m_pIMailDelivery->ThrottleCheck(player, &bThrottled);
        if (bThrottled)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player,
                    T("MAIL: Too much @mail sent recently."));
            }
            return;
        }
    }

    int num = atoi(reinterpret_cast<const char *>(msg));
    if (!num)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: I don\xE2\x80\x99t understand that message number."));
        }
        return;
    }

    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: You can\xE2\x80\x99t reply to non-existent messages."));
        }
        return;
    }

    if (!mail_from_player(mp->from, mp))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: The original sender no longer exists."));
        }
        return;
    }

    // Build recipient list.
    //
    UTF8 tolist[MOD_LBUF_SIZE];
    UTF8 *bp = tolist;

    if (all)
    {
        // Include all original recipients except the original sender.
        //
        UTF8 oldlist[MOD_LBUF_SIZE];
        strncpy(reinterpret_cast<char *>(oldlist),
                reinterpret_cast<const char *>(mp->tolist),
                sizeof(oldlist) - 1);
        oldlist[sizeof(oldlist) - 1] = '\0';

        char *saveptr = nullptr;
        char *token = strtok_r(reinterpret_cast<char *>(oldlist),
            " ", &saveptr);
        while (token)
        {
            if (atoi(token) != mp->from)
            {
                if (bp != tolist)
                {
                    *bp++ = ' ';
                }
                *bp++ = '#';
                size_t tlen = strlen(token);
                size_t remain = sizeof(tolist) - (bp - tolist);
                if (tlen < remain)
                {
                    memcpy(bp, token, tlen);
                    bp += tlen;
                }
            }
            token = strtok_r(nullptr, " ", &saveptr);
        }

        // Add original sender.
        //
        if (bp != tolist)
        {
            *bp++ = ' ';
        }
        bp += snprintf(reinterpret_cast<char *>(bp),
            sizeof(tolist) - (bp - tolist),
            "#%d", mp->from);
    }
    else
    {
        bp += snprintf(reinterpret_cast<char *>(bp),
            sizeof(tolist) - (bp - tolist),
            "#%d", mp->from);
    }
    *bp = '\0';

    // Build subject.
    //
    const UTF8 *pSubject = mp->subject;
    UTF8 subj[MOD_LBUF_SIZE];
    if (strncmp(reinterpret_cast<const char *>(pSubject), "Re:", 3))
    {
        snprintf(reinterpret_cast<char *>(subj), sizeof(subj),
            "Re: %s", reinterpret_cast<const char *>(pSubject));
    }
    else
    {
        strncpy(reinterpret_cast<char *>(subj),
                reinterpret_cast<const char *>(pSubject),
                sizeof(subj) - 1);
        subj[sizeof(subj) - 1] = '\0';
    }

    do_expmail_start(player, tolist, subj);

    // Quote original message if requested.
    //
    if (key & MAIL_QUOTE)
    {
        UTF8 fromname[MOD_LBUF_SIZE];
        get_player_name(mp->from, fromname, sizeof(fromname));
        const UTF8 *pMessage = MessageFetch(mp->number);
        const UTF8 *pTime = mp->time;

        UTF8 body[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(body), sizeof(body),
            "On %s, %s wrote:\r\n\r\n%s\r\n\r\n********** End of included message from %s\r\n",
            reinterpret_cast<const char *>(pTime),
            reinterpret_cast<const char *>(fromname),
            pMessage ? reinterpret_cast<const char *>(pMessage) : "",
            reinterpret_cast<const char *>(fromname));

        if (nullptr != m_pIAttributeAccess)
        {
            m_pIAttributeAccess->SetAttribute(player, player,
                T("Mailmsg"), body);
        }
    }

    // Set M_REPLY flag.
    //
    if (nullptr != m_pIAttributeAccess)
    {
        UTF8 aFlags[32];
        size_t nLen = 0;
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailflags"), aFlags, sizeof(aFlags) - 1, &nLen);
        aFlags[nLen] = '\0';
        int iFlag = M_REPLY;
        if (aFlags[0])
        {
            iFlag |= atoi(reinterpret_cast<const char *>(aFlags));
        }
        UTF8 flagbuf[16];
        snprintf(reinterpret_cast<char *>(flagbuf), sizeof(flagbuf),
            "%d", iFlag);
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailflags"), flagbuf);
    }
}

void CMailMod::do_mail_proof(dbref player)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message started."));
        }
        return;
    }

    // Read and display the draft.
    //
    UTF8 aTolist[MOD_LBUF_SIZE];
    aTolist[0] = '\0';
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    UTF8 aMailSub[MOD_LBUF_SIZE];
    aMailSub[0] = '\0';

    if (nullptr != m_pIAttributeAccess)
    {
        size_t nLen;
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailto"), aTolist, sizeof(aTolist) - 1, &nLen);
        aTolist[nLen] = '\0';
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailmsg"), aMailMsg, sizeof(aMailMsg) - 1, &nLen);
        aMailMsg[nLen] = '\0';
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailsub"), aMailSub, sizeof(aMailSub) - 1, &nLen);
        aMailSub[nLen] = '\0';
    }

    UTF8 *names = make_namelist(player, aTolist);
    if (nullptr != m_pINotify)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
            "MAIL: To: %s",
            names ? reinterpret_cast<const char *>(names) : "");
        m_pINotify->RawNotify(player, msg);

        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
            "MAIL: Subject: %s",
            reinterpret_cast<const char *>(aMailSub));
        m_pINotify->RawNotify(player, msg);

        m_pINotify->RawNotify(player, aMailMsg);
    }
    if (names)
    {
        free(names);
    }
}

void CMailMod::do_edit_msg(dbref player, const UTF8 *from, const UTF8 *to)
{
    // Check composing state.
    //
    bool bComposing = false;
    if (nullptr != m_pIMailDelivery)
    {
        m_pIMailDelivery->IsComposing(player, &bComposing);
    }
    if (!bComposing)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: No message in progress."));
        }
        return;
    }

    // Read current message.
    //
    UTF8 aMailMsg[MOD_LBUF_SIZE];
    aMailMsg[0] = '\0';
    if (nullptr != m_pIAttributeAccess)
    {
        size_t nLen = 0;
        m_pIAttributeAccess->GetAttribute(player, player,
            T("Mailmsg"), aMailMsg, sizeof(aMailMsg) - 1, &nLen);
        aMailMsg[nLen] = '\0';
    }

    // Simple find-and-replace (first occurrence only, matching server behavior).
    //
    const char *pFrom = reinterpret_cast<const char *>(from);
    const char *pTo = reinterpret_cast<const char *>(to);
    size_t nFrom = strlen(pFrom);
    size_t nTo = strlen(pTo);

    if (nFrom == 0)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("MAIL: Nothing to edit."));
        }
        return;
    }

    // Build result with all occurrences replaced.
    //
    UTF8 result[MOD_LBUF_SIZE];
    char *rp = reinterpret_cast<char *>(result);
    const char *src = reinterpret_cast<const char *>(aMailMsg);
    size_t remain = sizeof(result) - 1;

    while (*src)
    {
        const char *found = strstr(src, pFrom);
        if (found)
        {
            // Copy prefix.
            //
            size_t prefix = found - src;
            if (prefix > remain)
            {
                prefix = remain;
            }
            memcpy(rp, src, prefix);
            rp += prefix;
            remain -= prefix;

            // Copy replacement.
            //
            size_t rlen = nTo;
            if (rlen > remain)
            {
                rlen = remain;
            }
            memcpy(rp, pTo, rlen);
            rp += rlen;
            remain -= rlen;

            src = found + nFrom;
        }
        else
        {
            // Copy rest.
            //
            size_t rest = strlen(src);
            if (rest > remain)
            {
                rest = remain;
            }
            memcpy(rp, src, rest);
            rp += rest;
            remain -= rest;
            break;
        }
    }
    *rp = '\0';

    // Write back.
    //
    if (nullptr != m_pIAttributeAccess)
    {
        m_pIAttributeAccess->SetAttribute(player, player,
            T("Mailmsg"), result);
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(player, T("Text edited."));
    }
}

// ---------------------------------------------------------------------------
// Malias helpers and commands.
// ---------------------------------------------------------------------------

bool CMailMod::is_exp_mail(dbref player)
{
    bool bWiz = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsWizard(player, &bWiz);
    }
    return bWiz;
}

bool CMailMod::make_canonical_alias(const UTF8 *alias, UTF8 *buf,
    size_t bufsize, size_t *pnLen)
{
    if (nullptr == alias || !isalpha(*alias))
    {
        *pnLen = 0;
        return false;
    }

    const UTF8 *p = alias;
    UTF8 *q = buf;
    size_t nLeft = bufsize - 1;

    while (*p && nLeft)
    {
        if (!isalpha(*p) && !isdigit(*p) && *p != '_')
        {
            break;
        }
        *q++ = *p++;
        nLeft--;
    }
    *q = '\0';
    *pnLen = q - buf;
    return true;
}

malias_t *CMailMod::get_malias(dbref player, const UTF8 *alias, int *pnResult)
{
    *pnResult = GMA_INVALIDFORM;
    if (nullptr == alias)
    {
        return nullptr;
    }

    if (alias[0] == '#')
    {
        if (is_exp_mail(player))
        {
            int x = atoi(reinterpret_cast<const char *>(alias + 1));
            if (x < 0 || x >= m_ma_top)
            {
                *pnResult = GMA_NOTFOUND;
                return nullptr;
            }
            *pnResult = GMA_FOUND;
            return m_malias[x];
        }
    }
    else if (alias[0] == '*')
    {
        UTF8 canonical[SIZEOF_MALIAS];
        size_t nLen;
        if (make_canonical_alias(alias + 1, canonical, sizeof(canonical), &nLen))
        {
            for (int i = 0; i < m_ma_top; i++)
            {
                malias_t *m = m_malias[i];
                if (  m->owner == player
                   || m->owner == 1  // GOD
                   || is_exp_mail(player))
                {
                    if (0 == strcasecmp(
                            reinterpret_cast<const char *>(canonical),
                            reinterpret_cast<const char *>(m->name)))
                    {
                        *pnResult = GMA_FOUND;
                        return m;
                    }
                }
            }
            *pnResult = GMA_NOTFOUND;
        }
    }

    if (*pnResult == GMA_INVALIDFORM)
    {
        if (is_exp_mail(player))
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail aliases must be of the form *<name> or #<num>."));
        }
        else
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Mail aliases must be of the form *<name>."));
        }
    }
    return nullptr;
}

void CMailMod::do_malias_create(dbref player, const UTF8 *alias,
    const UTF8 *tolist)
{
    int nResult;
    get_malias(player, alias, &nResult);

    if (nResult == GMA_INVALIDFORM)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: What alias do you want to create?."));
        return;
    }
    if (nResult == GMA_FOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Mail Alias \xE2\x80\x98%s\xE2\x80\x99 already exists.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }

    malias_t *pt = static_cast<malias_t *>(calloc(1, sizeof(malias_t)));
    if (nullptr == pt)
    {
        m_pINotify->RawNotify(player, T("MAIL: Out of memory."));
        return;
    }

    // Grow alias array if needed.
    //
    if (0 == m_ma_size)
    {
        m_ma_size = MA_INC;
        m_malias = static_cast<malias_t **>(
            calloc(m_ma_size, sizeof(malias_t *)));
        if (nullptr == m_malias)
        {
            m_pINotify->RawNotify(player, T("MAIL: Out of memory."));
            free(pt);
            return;
        }
    }
    else if (m_ma_top >= m_ma_size)
    {
        m_ma_size += MA_INC;
        malias_t **nm = static_cast<malias_t **>(
            calloc(m_ma_size, sizeof(malias_t *)));
        if (nullptr == nm)
        {
            m_pINotify->RawNotify(player, T("MAIL: Out of memory."));
            free(pt);
            return;
        }
        for (int i = 0; i < m_ma_top; i++)
        {
            nm[i] = m_malias[i];
        }
        free(m_malias);
        m_malias = nm;
    }

    m_malias[m_ma_top] = pt;

    // Parse the player list.
    //
    UTF8 head_buf[MOD_LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(head_buf),
            reinterpret_cast<const char *>(tolist),
            sizeof(head_buf) - 1);
    head_buf[sizeof(head_buf) - 1] = '\0';

    char *head = reinterpret_cast<char *>(head_buf);
    int count = 0;

    while (*head && count < (MAX_MALIAS_MEMBERSHIP - 1))
    {
        while (*head == ' ')
        {
            head++;
        }
        if (!*head)
        {
            break;
        }

        char *tail = head;
        if (*tail == '"')
        {
            head++;
            tail++;
            while (*tail && *tail != '"')
            {
                tail++;
            }
        }
        else
        {
            while (*tail && *tail != ' ')
            {
                tail++;
            }
        }

        char saved = *tail;
        *tail = '\0';

        // Look up target.
        //
        dbref target = NOTHING;
        if (0 == strcasecmp(head, "me"))
        {
            target = player;
        }
        else if (*head == '#')
        {
            target = atoi(head + 1);
        }
        else if (nullptr != m_pIObjectInfo)
        {
            UTF8 namebuf[256];
            snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                     "*%s", head);
            m_pIObjectInfo->MatchThing(player, namebuf, &target);
        }

        bool bPlayer = false;
        if (target >= 0 && nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(target, &bPlayer);
        }

        if (!bPlayer)
        {
            m_pINotify->RawNotify(player, T("MAIL: No such player."));
        }
        else
        {
            UTF8 addmsg[256];
            UTF8 szName[64];
            get_player_name(target, szName, sizeof(szName));
            snprintf(reinterpret_cast<char *>(addmsg), sizeof(addmsg),
                     "MAIL: %s added to alias %s",
                     reinterpret_cast<const char *>(szName),
                     reinterpret_cast<const char *>(alias));
            m_pINotify->RawNotify(player, addmsg);
            pt->list[count] = target;
            count++;
        }

        *tail = saved;
        head = tail;
        if (*head == '"' || *head == ' ')
        {
            head++;
        }
    }

    UTF8 canonical[SIZEOF_MALIAS];
    size_t nLen;
    if (!make_canonical_alias(alias + 1, canonical, sizeof(canonical), &nLen))
    {
        m_pINotify->RawNotify(player, T("MAIL: Invalid mail alias."));
        free(pt);
        return;
    }

    pt->list[count] = NOTHING;
    pt->name = reinterpret_cast<UTF8 *>(strdup(
        reinterpret_cast<const char *>(canonical)));
    pt->numrecep = count;
    pt->owner = player;
    pt->desc = reinterpret_cast<UTF8 *>(strdup(
        reinterpret_cast<const char *>(canonical)));
    pt->desc_width = nLen;
    m_ma_top++;
    sqlite_wt_sync_all_aliases();

    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: Alias set \xE2\x80\x98%s\xE2\x80\x99 defined.",
             reinterpret_cast<const char *>(alias));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_list(dbref player, const UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bGod);
    }
    if (!is_exp_mail(player) && player != m->owner && !bGod)
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    UTF8 buf[MOD_LBUF_SIZE];
    char *bp = reinterpret_cast<char *>(buf);
    bp += snprintf(bp, sizeof(buf), "MAIL: Alias *%s: ",
                   reinterpret_cast<const char *>(m->name));

    for (int i = m->numrecep - 1; i >= 0; i--)
    {
        UTF8 szName[64];
        get_player_name(m->list[i], szName, sizeof(szName));
        const char *n = reinterpret_cast<const char *>(szName);
        size_t remain = sizeof(buf) - (bp - reinterpret_cast<char *>(buf));
        if (remain < 64)
        {
            break;
        }
        if (strchr(n, ' '))
        {
            bp += snprintf(bp, remain, "\"%s\" ", n);
        }
        else
        {
            bp += snprintf(bp, remain, "%s ", n);
        }
    }
    m_pINotify->RawNotify(player, buf);
}

void CMailMod::do_malias_list_all(dbref player)
{
    bool bGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(player, &bGod);
    }

    bool notified = false;
    for (int i = 0; i < m_ma_top; i++)
    {
        malias_t *m = m_malias[i];
        bool bOwnerGod = false;
        if (nullptr != m_pIPermissions)
        {
            m_pIPermissions->IsGod(m->owner, &bOwnerGod);
        }
        if (bOwnerGod || m->owner == player || bGod)
        {
            if (!notified)
            {
                m_pINotify->RawNotify(player,
                    T("Name         Description                              Owner"));
                notified = true;
            }

            UTF8 szOwner[64];
            get_player_name(m->owner, szOwner, sizeof(szOwner));

            UTF8 line[256];
            snprintf(reinterpret_cast<char *>(line), sizeof(line),
                     "%-12s %-40s %-15.15s",
                     reinterpret_cast<const char *>(m->name),
                     reinterpret_cast<const char *>(m->desc),
                     reinterpret_cast<const char *>(szOwner));
            m_pINotify->RawNotify(player, line);
        }
    }
    m_pINotify->RawNotify(player, T("*****  End of Mail Aliases *****"));
}

void CMailMod::do_malias_adminlist(dbref player)
{
    if (!is_exp_mail(player))
    {
        do_malias_list_all(player);
        return;
    }

    m_pINotify->RawNotify(player,
        T("Num  Name         Description                              Owner"));

    for (int i = 0; i < m_ma_top; i++)
    {
        malias_t *m = m_malias[i];
        UTF8 szOwner[64];
        get_player_name(m->owner, szOwner, sizeof(szOwner));

        UTF8 line[256];
        snprintf(reinterpret_cast<char *>(line), sizeof(line),
                 "%-4d %-12s %-40s %-15.15s",
                 i,
                 reinterpret_cast<const char *>(m->name),
                 reinterpret_cast<const char *>(m->desc),
                 reinterpret_cast<const char *>(szOwner));
        m_pINotify->RawNotify(player, line);
    }
    m_pINotify->RawNotify(player, T("***** End of Mail Aliases *****"));
}

void CMailMod::do_malias_desc(dbref player, const UTF8 *alias,
    const UTF8 *desc)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    if (nullptr == desc || '\0' == *desc)
    {
        m_pINotify->RawNotify(player, T("MAIL: Description is not valid."));
        return;
    }

    // Simple validation: just use the desc as-is (truncated).
    //
    size_t nDesc = strlen(reinterpret_cast<const char *>(desc));
    if (nDesc > 40)
    {
        nDesc = 40;
    }
    free(m->desc);
    m->desc = reinterpret_cast<UTF8 *>(strndup(
        reinterpret_cast<const char *>(desc), nDesc));
    m->desc_width = nDesc;
    sqlite_wt_sync_all_aliases();
    m_pINotify->RawNotify(player, T("MAIL: Description changed."));
}

void CMailMod::do_malias_chown(dbref player, const UTF8 *alias,
    const UTF8 *owner)
{
    if (!is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: You cannot do that!"));
        return;
    }

    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    dbref target = NOTHING;
    if (nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                 "*%s", reinterpret_cast<const char *>(owner));
        m_pIObjectInfo->MatchThing(player, namebuf, &target);
    }
    if (NOTHING == target)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that here."));
        return;
    }
    m->owner = target;
    sqlite_wt_sync_all_aliases();
    m_pINotify->RawNotify(player, T("MAIL: Owner changed for alias."));
}

void CMailMod::do_malias_add(dbref player, const UTF8 *alias,
    const UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    dbref thing = NOTHING;
    if (nullptr != person && *person == '#')
    {
        thing = atoi(reinterpret_cast<const char *>(person) + 1);
        bool bPlayer = false;
        if (thing >= 0 && nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(thing, &bPlayer);
        }
        if (!bPlayer)
        {
            m_pINotify->RawNotify(player, T("MAIL: Only players may be added."));
            return;
        }
    }
    if (NOTHING == thing && nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                 "*%s", reinterpret_cast<const char *>(person));
        m_pIObjectInfo->MatchThing(player, namebuf, &thing);
    }
    if (NOTHING == thing)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that person here."));
        return;
    }

    for (int i = 0; i < m->numrecep; i++)
    {
        if (m->list[i] == thing)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: That person is already on the list."));
            return;
        }
    }
    if (m->numrecep >= (MAX_MALIAS_MEMBERSHIP - 1))
    {
        m_pINotify->RawNotify(player, T("MAIL: The list is full."));
        return;
    }

    m->list[m->numrecep] = thing;
    m->numrecep++;
    sqlite_wt_sync_all_aliases();

    UTF8 szName[64];
    get_player_name(thing, szName, sizeof(szName));
    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: %s added to %s",
             reinterpret_cast<const char *>(szName),
             reinterpret_cast<const char *>(m->name));
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_remove(dbref player, const UTF8 *alias,
    const UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool bOwnerGod = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->IsGod(m->owner, &bOwnerGod);
    }
    if (bOwnerGod && !is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    dbref thing = NOTHING;
    if (nullptr != person && *person == '#')
    {
        thing = atoi(reinterpret_cast<const char *>(person) + 1);
    }
    if (NOTHING == thing && nullptr != m_pIObjectInfo)
    {
        UTF8 namebuf[256];
        snprintf(reinterpret_cast<char *>(namebuf), sizeof(namebuf),
                 "*%s", reinterpret_cast<const char *>(person));
        m_pIObjectInfo->MatchThing(player, namebuf, &thing);
    }
    if (NOTHING == thing)
    {
        m_pINotify->RawNotify(player, T("MAIL: I do not see that person here."));
        return;
    }

    bool found = false;
    for (int i = 0; i < m->numrecep; i++)
    {
        if (found)
        {
            m->list[i] = m->list[i + 1];
        }
        else if (m->list[i] == thing)
        {
            m->list[i] = m->list[i + 1];
            found = true;
        }
    }

    if (found)
    {
        m->numrecep--;
        sqlite_wt_sync_all_aliases();

        UTF8 szName[64];
        get_player_name(thing, szName, sizeof(szName));
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: %s removed from alias %s.",
                 reinterpret_cast<const char *>(szName),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        UTF8 szName[64];
        get_player_name(thing, szName, sizeof(szName));
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: %s is not a member of alias %s.",
                 reinterpret_cast<const char *>(szName),
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
}

void CMailMod::do_malias_rename(dbref player, const UTF8 *alias,
    const UTF8 *newname)
{
    int nResult;
    malias_t *m = get_malias(player, newname, &nResult);
    if (nResult == GMA_FOUND)
    {
        m_pINotify->RawNotify(player, T("MAIL: That name already exists!"));
        return;
    }
    if (nResult != GMA_NOTFOUND)
    {
        return;
    }
    m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        m_pINotify->RawNotify(player, T("MAIL: I cannot find that alias!"));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!is_exp_mail(player) && m->owner != player)
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }

    UTF8 canonical[SIZEOF_MALIAS];
    size_t nLen;
    if (make_canonical_alias(newname + 1, canonical, sizeof(canonical), &nLen))
    {
        free(m->name);
        m->name = reinterpret_cast<UTF8 *>(strdup(
            reinterpret_cast<const char *>(canonical)));
        sqlite_wt_sync_all_aliases();
        m_pINotify->RawNotify(player, T("MAIL: Mailing Alias renamed."));
    }
    else
    {
        m_pINotify->RawNotify(player, T("MAIL: Alias is not valid."));
    }
}

void CMailMod::do_malias_delete(dbref player, const UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }

    bool done = false;
    for (int i = 0; i < m_ma_top; i++)
    {
        if (done)
        {
            m_malias[i] = m_malias[i + 1];
        }
        else if (m == m_malias[i])
        {
            if (m->owner == player || is_exp_mail(player))
            {
                done = true;
                m_pINotify->RawNotify(player, T("MAIL: Alias Deleted."));
                m_malias[i] = m_malias[i + 1];
            }
        }
    }

    if (!done)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found.",
                 reinterpret_cast<const char *>(alias));
        m_pINotify->RawNotify(player, msg);
    }
    else
    {
        m_ma_top--;
        free(m->name);
        free(m->desc);
        free(m);
        sqlite_wt_sync_all_aliases();
    }
}

void CMailMod::do_malias_status(dbref player)
{
    if (!is_exp_mail(player))
    {
        m_pINotify->RawNotify(player, T("MAIL: Permission denied."));
        return;
    }
    UTF8 msg[128];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: Number of mail aliases defined: %d", m_ma_top);
    m_pINotify->RawNotify(player, msg);
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: Allocated slots %d", m_ma_size);
    m_pINotify->RawNotify(player, msg);
}

void CMailMod::do_malias_switch(dbref player, const UTF8 *a1,
    const UTF8 *a2)
{
    if (nullptr != a1 && '\0' != *a1)
    {
        if (nullptr != a2 && '\0' != *a2)
        {
            do_malias_create(player, a1, a2);
        }
        else
        {
            do_malias_list(player, a1);
        }
    }
    else
    {
        do_malias_list_all(player);
    }
}

// ---------------------------------------------------------------------------
// Command implementations.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_flags(dbref player, const UTF8 *msglist,
    mail_flag flag, bool negate)
{
    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    int i = 0, j = 0;
    int folder = player_folder(player);

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (All(ms) || Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                if (negate)
                {
                    mp->read &= ~flag;
                }
                else
                {
                    mp->read |= flag;
                }
                sqlite_wt_update_mail_flags(mp);

                UTF8 msg[MOD_LBUF_SIZE];
                switch (flag)
                {
                case M_TAG:
                    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                             "MAIL: Msg #%d %s.", i,
                             negate ? "untagged" : "tagged");
                    break;

                case M_CLEARED:
                    if (Unread(mp) && !negate)
                    {
                        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                                 "MAIL: Unread Msg #%d cleared! Use @mail/unclear %d to recover.",
                                 i, i);
                    }
                    else
                    {
                        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                                 "MAIL: Msg #%d %s.", i,
                                 negate ? "uncleared" : "cleared");
                    }
                    break;

                case M_SAFE:
                    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                             "MAIL: Msg #%d marked safe.", i);
                    break;

                default:
                    msg[0] = '\0';
                    break;
                }

                if (msg[0] != '\0')
                {
                    m_pINotify->RawNotify(player, msg);
                }
            }
        }
        mp = MailListNext(mp, player);
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don\xE2\x80\x99t have any matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// Display helpers.
// ---------------------------------------------------------------------------

UTF8 *CMailMod::status_chars(struct mail *mp)
{
    static UTF8 res[10];
    UTF8 *p = res;
    *p++ = Read(mp)     ? '-' : 'N';
    *p++ = M_Safe(mp)   ? 'S' : '-';
    *p++ = Cleared(mp)  ? 'C' : '-';
    *p++ = Urgent(mp)   ? 'U' : '-';
    *p++ = Mass(mp)     ? 'M' : '-';
    *p++ = Forward(mp)  ? 'F' : '-';
    *p++ = Tagged(mp)   ? '+' : '-';
    *p = '\0';
    return res;
}

UTF8 *CMailMod::mail_list_time(const UTF8 *the_time)
{
    // Format: "day mon dd hh:mm:ss yyyy" → "day mon dd hh:mm yyyy"
    // Chop out ":ss"
    //
    static UTF8 buf[32];
    if (nullptr == the_time || '\0' == *the_time)
    {
        buf[0] = '\0';
        return buf;
    }

    const char *p = reinterpret_cast<const char *>(the_time);
    char *q = reinterpret_cast<char *>(buf);
    int i;

    // Copy first 16 chars (up to and including "hh:mm")
    //
    for (i = 0; i < 16 && *p; i++)
    {
        *q++ = *p++;
    }

    // Skip 3 chars (":ss")
    //
    for (i = 0; i < 3 && *p; i++)
    {
        p++;
    }

    // Copy remaining (up to 5 chars for " yyyy")
    //
    for (i = 0; i < 5 && *p; i++)
    {
        *q++ = *p++;
    }
    *q = '\0';
    return buf;
}

void CMailMod::get_player_name(dbref who, UTF8 *buf, size_t bufsize)
{
    const UTF8 *pName = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetName(who, &pName);
    }
    if (nullptr != pName)
    {
        strncpy(reinterpret_cast<char *>(buf),
                reinterpret_cast<const char *>(pName), bufsize - 1);
        buf[bufsize - 1] = '\0';
    }
    else
    {
        snprintf(reinterpret_cast<char *>(buf), bufsize, "#%d", who);
    }
}

bool CMailMod::is_connected_visible(dbref who, dbref viewer)
{
    UNUSED_PARAMETER(viewer);
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    bool bConnected = false;
    m_pIObjectInfo->IsConnected(who, &bConnected);
    // TODO: Check Hidden/See_Hidden when interface supports it.
    return bConnected;
}

// ---------------------------------------------------------------------------
// @mail/list — list messages in current folder.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_list(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    const UTF8 *msglist;
    int folder = player_folder(player);

    if (nullptr == arg2 || '\0' == *arg2)
    {
        msglist = arg1;
    }
    else
    {
        // arg1 is a folder, arg2 is the msglist.
        //
        if (nullptr != arg1 && isdigit(*arg1))
        {
            int f = atoi(reinterpret_cast<const char *>(arg1));
            if (f >= 0 && f <= MAX_FOLDERS)
            {
                folder = f;
            }
        }
        msglist = arg2;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    UTF8 hdr[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "   MAIL: Folder %d   "
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
             "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93",
             folder);
    m_pINotify->RawNotify(player, hdr);

    int i = 0;

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                UTF8 *time = mail_list_time(mp->time);
                size_t nSize = MessageFetchSize(mp->number);

                UTF8 szFromName[64];
                get_player_name(mp->from, szFromName, sizeof(szFromName));

                UTF8 line[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(line), sizeof(line),
                    "[%s] %-3d (%4zu) From: %-16s At: %s %s",
                    reinterpret_cast<const char *>(status_chars(mp)),
                    i, nSize,
                    reinterpret_cast<const char *>(szFromName),
                    reinterpret_cast<const char *>(time),
                    is_connected_visible(mp->from, player) ? "Conn" : " ");
                m_pINotify->RawNotify(player, line);
            }
        }
        mp = MailListNext(mp, player);
    }
    m_pINotify->RawNotify(player, MOD_DASH_LINE);
}

// ---------------------------------------------------------------------------
// @mail/read — read a mail message.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_read(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    const UTF8 *msglist;
    int folder = player_folder(player);

    if (nullptr == arg2 || '\0' == *arg2)
    {
        msglist = arg1;
    }
    else
    {
        if (nullptr != arg1 && isdigit(*arg1))
        {
            int f = atoi(reinterpret_cast<const char *>(arg1));
            if (f >= 0 && f <= MAX_FOLDERS)
            {
                folder = f;
            }
        }
        msglist = arg2;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    int i = 0, j = 0;

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                const UTF8 *body = MessageFetch(mp->number);

                m_pINotify->RawNotify(player, MOD_DASH_LINE);

                UTF8 szFromName[64];
                get_player_name(mp->from, szFromName, sizeof(szFromName));

                UTF8 hdr[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
                    "%-3d         From:  %-16s  At: %-25s  %s\r\n"
                    "Fldr   : %-2d Status: %s%s%s%s%s%s%s\r\n"
                    "Subject: %s",
                    i,
                    reinterpret_cast<const char *>(szFromName),
                    reinterpret_cast<const char *>(mp->time),
                    is_connected_visible(mp->from, player) ? " (Conn)" : "      ",
                    folder,
                    Read(mp)     ? "Read"    : "Unread",
                    Cleared(mp)  ? " Cleared" : "",
                    Urgent(mp)   ? " Urgent"  : "",
                    Mass(mp)     ? " Mass"    : "",
                    Forward(mp)  ? " Forward" : "",
                    M_Safe(mp)   ? " Safe"    : "",
                    Tagged(mp)   ? " Tagged"  : "",
                    reinterpret_cast<const char *>(mp->subject));
                m_pINotify->RawNotify(player, hdr);
                m_pINotify->RawNotify(player, MOD_DASH_LINE);
                m_pINotify->RawNotify(player, body);
                m_pINotify->RawNotify(player, MOD_DASH_LINE);

                if (Unread(mp))
                {
                    mp->read |= M_ISREAD;
                    sqlite_wt_update_mail_flags(mp);
                }
            }
        }
        mp = MailListNext(mp, player);
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don\xE2\x80\x99t have that many matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// @mail/next — read the next unread message.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_next(dbref player)
{
    int folder = player_folder(player);
    int i = 0;

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (Unread(mp))
            {
                const UTF8 *body = MessageFetch(mp->number);

                m_pINotify->RawNotify(player, MOD_DASH_LINE);

                UTF8 szFromName[64];
                get_player_name(mp->from, szFromName, sizeof(szFromName));

                UTF8 hdr[MOD_LBUF_SIZE];
                snprintf(reinterpret_cast<char *>(hdr), sizeof(hdr),
                    "%-3d         From:  %-16s  At: %-25s  %s\r\n"
                    "Fldr   : %-2d Status: Unread%s%s%s%s%s\r\n"
                    "Subject: %s",
                    i,
                    reinterpret_cast<const char *>(szFromName),
                    reinterpret_cast<const char *>(mp->time),
                    is_connected_visible(mp->from, player) ? " (Conn)" : "      ",
                    folder,
                    Urgent(mp)   ? " Urgent"  : "",
                    Mass(mp)     ? " Mass"    : "",
                    Forward(mp)  ? " Forward" : "",
                    M_Safe(mp)   ? " Safe"    : "",
                    Tagged(mp)   ? " Tagged"  : "",
                    reinterpret_cast<const char *>(mp->subject));
                m_pINotify->RawNotify(player, hdr);
                m_pINotify->RawNotify(player, MOD_DASH_LINE);
                m_pINotify->RawNotify(player, body);
                m_pINotify->RawNotify(player, MOD_DASH_LINE);

                mp->read |= M_ISREAD;
                sqlite_wt_update_mail_flags(mp);
                return;
            }
        }
        mp = MailListNext(mp, player);
    }

    m_pINotify->RawNotify(player,
        T("MAIL: You have no unread messages in that folder."));
}

void CMailMod::do_mail_purge(dbref player)
{
    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        struct mail *next = MailListNext(mp, player);
        if (Cleared(mp))
        {
            sqlite_wt_delete_mail(mp);
            MailListRemove(player, mp);
        }
        mp = next;
    }
    m_pINotify->RawNotify(player, T("MAIL: Mailbox purged."));
}

// ---------------------------------------------------------------------------
// @mail/file — move messages to a folder.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_file(dbref player, const UTF8 *msglist,
    const UTF8 *folder)
{
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    // Parse folder number.
    //
    int foldernum = -1;
    if (nullptr != folder && isdigit(*folder))
    {
        foldernum = atoi(reinterpret_cast<const char *>(folder));
        if (foldernum < 0 || foldernum > MAX_FOLDERS)
        {
            foldernum = -1;
        }
    }
    if (-1 == foldernum)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: Invalid folder specification"));
        return;
    }

    int i = 0, j = 0;
    int origfold = player_folder(player);

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (All(ms) || (Folder(mp) == origfold))
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                mp->read &= M_FMASK;
                mp->read |= FolderBit(foldernum);
                sqlite_wt_update_mail_flags(mp);

                UTF8 msg[128];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "MAIL: Msg %d filed in folder %d", i, foldernum);
                m_pINotify->RawNotify(player, msg);
            }
        }
        mp = MailListNext(mp, player);
    }

    if (!j)
    {
        m_pINotify->RawNotify(player,
            T("MAIL: You don\xE2\x80\x99t have any matching messages!"));
    }
}

// ---------------------------------------------------------------------------
// @mail/stats — personal mail statistics.
// ---------------------------------------------------------------------------

void CMailMod::do_mail_stats(dbref player, int folder)
{
    int fc = 0, fr = 0, fu = 0, tc = 0;
    size_t fs = 0;

    struct mail *mp = MailListFirst(player);
    while (nullptr != mp)
    {
        if (0 == folder || Folder(mp) == folder)
        {
            fc++;
            if (Read(mp))
            {
                fr++;
            }
            else
            {
                fu++;
            }
            if (Cleared(mp))
            {
                tc++;
            }
            fs += MessageFetchSize(mp->number);
        }
        mp = MailListNext(mp, player);
    }

    UTF8 msg[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "STRSTRSTR: %d messages in folder %d [%d read, %d unread, %d cleared].",
             fc, folder, fr, fu, tc);

    // Use a simpler format.
    //
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "MAIL: %d messages (%d read, %d unread, %d cleared). %zu bytes total.",
             fc, fr, fu, tc, fs);
    m_pINotify->RawNotify(player, msg);
}

// ---------------------------------------------------------------------------
// Default @mail handler — routes to list, read, clear, purge, or send.
// Returns true if handled, false if sending (unimplemented).
// ---------------------------------------------------------------------------

bool CMailMod::do_mail_stub(dbref player, const UTF8 *arg1,
    const UTF8 *arg2)
{
    if (nullptr == arg1 || '\0' == *arg1)
    {
        if (arg2 && *arg2)
        {
            m_pINotify->RawNotify(player,
                T("MAIL: Invalid mail command."));
            return true;
        }
        do_mail_list(player, arg1, nullptr);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "purge"))
    {
        do_mail_purge(player);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "clear"))
    {
        do_mail_flags(player, arg2, M_CLEARED, false);
        return true;
    }

    if (0 == strcasecmp(reinterpret_cast<const char *>(arg1), "unclear"))
    {
        do_mail_flags(player, arg2, M_CLEARED, true);
        return true;
    }

    if (nullptr != arg2 && '\0' != *arg2)
    {
        // Sending mail — not yet implemented in module.
        //
        return false;
    }

    // Must be reading or listing mail.
    //
    if (isdigit(*arg1) && nullptr == strchr(
            reinterpret_cast<const char *>(arg1), '-'))
    {
        do_mail_read(player, arg1, nullptr);
    }
    else
    {
        do_mail_list(player, arg1, nullptr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// mux_IMailControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CMailMod::PlayerConnect(dbref player)
{
    CheckMail(player, player_folder(player), false);
    return MUX_S_OK;
}

MUX_RESULT CMailMod::PlayerNuke(dbref player)
{
    return DestroyPlayerMail(player);
}

MUX_RESULT CMailMod::MailCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key & ~MAIL_QUOTE)
    {
    case MAIL_CLEAR:
        do_mail_flags(executor, pArg1, M_CLEARED, false);
        return MUX_S_OK;

    case MAIL_UNCLEAR:
        do_mail_flags(executor, pArg1, M_CLEARED, true);
        return MUX_S_OK;

    case MAIL_TAG:
        do_mail_flags(executor, pArg1, M_TAG, false);
        return MUX_S_OK;

    case MAIL_UNTAG:
        do_mail_flags(executor, pArg1, M_TAG, true);
        return MUX_S_OK;

    case MAIL_SAFE:
        do_mail_flags(executor, pArg1, M_SAFE, false);
        return MUX_S_OK;

    case MAIL_PURGE:
        do_mail_purge(executor);
        return MUX_S_OK;

    case MAIL_LIST:
        do_mail_list(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_READ:
        do_mail_read(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_NEXT:
        do_mail_next(executor);
        return MUX_S_OK;

    case MAIL_FILE:
        do_mail_file(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_STATS:
        do_mail_stats(executor, 0);
        return MUX_S_OK;

    case MAIL_DSTATS:
        do_mail_stats(executor, 0);
        return MUX_S_OK;

    case MAIL_FSTATS:
        do_mail_stats(executor, 0);
        return MUX_S_OK;

    case MAIL_REVIEW:
        do_mail_review(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_FOLDER:
        do_mail_change_folder(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_RETRACT:
        do_mail_retract(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_NUKE:
        do_mail_nuke(executor);
        return MUX_S_OK;

    case MAIL_SEND:
        do_expmail_stop(executor, 0);
        return MUX_S_OK;

    case MAIL_QUICK:
        do_mail_quick(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_CC:
        do_mail_quick(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_BCC:
        // BCC uses blind numlist.
        //
        do_mail_quick(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_PROOF:
        do_mail_proof(executor);
        return MUX_S_OK;

    case MAIL_ABORT:
        do_expmail_abort(executor);
        return MUX_S_OK;

    case MAIL_FORWARD:
        do_mail_fwd(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MAIL_REPLY:
        do_mail_reply(executor, pArg1, false, key);
        return MUX_S_OK;

    case MAIL_REPLYALL:
        do_mail_reply(executor, pArg1, true, key);
        return MUX_S_OK;

    case MAIL_URGENT:
        do_expmail_stop(executor, M_URGENT);
        return MUX_S_OK;

    case MAIL_EDIT:
        do_edit_msg(executor, pArg1, pArg2);
        return MUX_S_OK;

    case 0:
        // Default @mail (no switch).
        //
        if (do_mail_stub(executor, pArg1, pArg2))
        {
            return MUX_S_OK;
        }
        return MUX_E_NOTIMPLEMENTED;

    default:
        return MUX_E_NOTIMPLEMENTED;
    }
}

MUX_RESULT CMailMod::MaliasCommand(dbref executor, int key,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key)
    {
    case 0:
        do_malias_switch(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_DESC:
        do_malias_desc(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_CHOWN:
        do_malias_chown(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_ADD:
        do_malias_add(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_REMOVE:
        do_malias_remove(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_DELETE:
        do_malias_delete(executor, pArg1);
        return MUX_S_OK;

    case MALIAS_RENAME:
        do_malias_rename(executor, pArg1, pArg2);
        return MUX_S_OK;

    case MALIAS_LIST:
        do_malias_adminlist(executor);
        return MUX_S_OK;

    case MALIAS_STATUS:
        do_malias_status(executor);
        return MUX_S_OK;

    default:
        return MUX_E_NOTIMPLEMENTED;
    }
}

MUX_RESULT CMailMod::FolderCommand(dbref executor, int key, int nargs,
    const UTF8 *pArg1, const UTF8 *pArg2)
{
    switch (key)
    {
    case FOLDER_FILE:
        do_mail_file(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_LIST:
        ListMailInFolder(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_READ:
        do_mail_read(executor, pArg1, pArg2);
        return MUX_S_OK;

    case FOLDER_SET:
        do_mail_change_folder(executor, pArg1, pArg2);
        return MUX_S_OK;

    default:
        if (nullptr == pArg1 || '\0' == *pArg1)
        {
            DoListMailBrief(executor);
        }
        else if (2 == nargs)
        {
            do_mail_read(executor, pArg1, pArg2);
        }
        else
        {
            do_mail_change_folder(executor, pArg1, pArg2);
        }
        return MUX_S_OK;
    }
}

MUX_RESULT CMailMod::CheckMail(dbref player, int folder, bool silent)
{
    int rc, uc, cc, gc;
    count_mail_internal(player, folder, &rc, &uc, &cc);
    urgent_mail_internal(player, folder, &gc);

    if (rc + uc > 0)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).",
                 rc + uc, folder,
                 reinterpret_cast<const char *>(get_folder_name(player, folder)),
                 uc, cc);
        m_pINotify->RawNotify(player, msg);
    }
    else if (!silent)
    {
        m_pINotify->RawNotify(player,
            T("\r\nMAIL: You have no mail.\r\n"));
    }
    if (gc > 0)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "URGENT MAIL: You have %d urgent messages in folder %d [%s].",
                 gc, folder,
                 reinterpret_cast<const char *>(get_folder_name(player, folder)));
        m_pINotify->RawNotify(player, msg);
    }
    return MUX_S_OK;
}

MUX_RESULT CMailMod::ExpireMail(void)
{
    // TODO: Implement mail expiration (needs DO_WHOLE_DB, No_Mail_Expire, time classes).
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CMailMod::CountMail(dbref player, int folder,
    int *pRead, int *pUnread, int *pCleared)
{
    count_mail_internal(player, folder, pRead, pUnread, pCleared);
    return MUX_S_OK;
}

MUX_RESULT CMailMod::DestroyPlayerMail(dbref player)
{
    // Step 1: Purge received mail.
    //
    MailListRemoveAll(player);
    sqlite_wt_delete_all_mail(player);

    // Step 2: Orphan sent mail in every other player's mailbox.
    //
    for (auto &kv : m_mail_htab)
    {
        struct mail *mp = MailListFirst(kv.first);
        while (nullptr != mp)
        {
            if (mp->from == player)
            {
                mp->from = NOTHING;
                sqlite_wt_update_mail_flags(mp);
            }
            mp = MailListNext(mp, kv.first);
        }
    }

    // Step 3: Clean up mail aliases.
    //
    for (int i = 0; i < m_ma_top; i++)
    {
        if (nullptr != m_malias[i])
        {
            for (int j = 0; j < m_malias[i]->numrecep; j++)
            {
                if (m_malias[i]->list[j] == player)
                {
                    m_malias[i]->list[j] =
                        m_malias[i]->list[m_malias[i]->numrecep - 1];
                    m_malias[i]->numrecep--;
                    j--;
                }
            }
        }
    }
    sqlite_wt_sync_all_aliases();

    return MUX_S_OK;
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
