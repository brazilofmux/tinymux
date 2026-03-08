/*! \file comsys_mod.cpp
 * \brief Comsys Module — Channel system as a loadable module
 *
 * This module implements the MUX channel system as a dynamically loaded
 * module.  It hooks into server events for player connect/disconnect
 * and provides mux_IComsysControl for command dispatch.
 *
 * Core dependencies are accessed exclusively through COM interfaces:
 *   mux_INotify           — player notification
 *   mux_IObjectInfo       — object property queries
 *   mux_IAttributeAccess  — attribute read/write
 *   mux_IEvaluator        — softcode evaluation
 *   mux_IPermissions      — permission checks
 *   mux_ILog              — logging
 *
 * Channel data is loaded from and persisted to the game's SQLite database
 * via the module's own sqlite3 connection.
 */

#include "../copyright.h"
#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "comsys_mod.h"

#include <cstring>

// Module bookkeeping.
//
static uint32_t g_cComponents  = 0;
static uint32_t g_cServerLocks = 0;

// Module entry points.
//
static MUX_CLASS_INFO comsys_classes[] =
{
    { CID_ComsysMod }
};
#define NUM_CLASSES (sizeof(comsys_classes)/sizeof(comsys_classes[0]))

extern "C" MUX_RESULT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, comsys_classes, nullptr);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, comsys_classes);
}

extern "C" MUX_RESULT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_ComsysMod == cid)
    {
        CComsysModFactory *pFactory = nullptr;
        try
        {
            pFactory = new CComsysModFactory;
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
// CComsysMod — main module class.
// ---------------------------------------------------------------------------

CComsysMod::CComsysMod(void) : m_cRef(1),
    m_pILog(nullptr),
    m_pIServerEventsControl(nullptr),
    m_pINotify(nullptr),
    m_pIObjectInfo(nullptr),
    m_pIAttributeAccess(nullptr),
    m_pIEvaluator(nullptr),
    m_pIPermissions(nullptr),
    m_db(nullptr),
    m_num_channels(0)
{
    memset(m_comsys_table, 0, sizeof(m_comsys_table));
    g_cComponents++;
}

MUX_RESULT CComsysMod::FinalConstruct(void)
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

    mux_CreateInstance(CID_Evaluator, nullptr, UseSameProcess,
                       IID_IEvaluator,
                       reinterpret_cast<void **>(&m_pIEvaluator));

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
            m_pILog->log_text(T("Comsys module loaded."));
            m_pILog->end_log();
        }
    }

    return mr;
}

CComsysMod::~CComsysMod()
{
    // Close our SQLite connection.
    //
    CloseDatabase();

    // Free channel data.
    //
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        struct channel *ch = it->second;
        if (nullptr != ch->users)
        {
            for (int j = 0; j < ch->num_users; j++)
            {
                if (nullptr != ch->users[j])
                {
                    if (nullptr != ch->users[j]->title)
                    {
                        free(ch->users[j]->title);
                    }
                    free(ch->users[j]);
                }
            }
            free(ch->users);
        }
        free(ch);
    }
    m_channels.clear();

    // Free comsys_table entries.
    //
    for (int i = 0; i < NUM_COMSYS; i++)
    {
        comsys_t *c = m_comsys_table[i];
        while (nullptr != c)
        {
            comsys_t *next = c->next;
            if (nullptr != c->alias)
            {
                free(c->alias);
            }
            if (nullptr != c->channels)
            {
                for (int j = 0; j < c->numchannels; j++)
                {
                    if (nullptr != c->channels[j])
                    {
                        free(c->channels[j]);
                    }
                }
                free(c->channels);
            }
            free(c);
            c = next;
        }
        m_comsys_table[i] = nullptr;
    }

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Comsys module unloading."));
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

    if (nullptr != m_pIEvaluator)
    {
        m_pIEvaluator->Release();
        m_pIEvaluator = nullptr;
    }

    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->Release();
        m_pIPermissions = nullptr;
    }

    g_cComponents--;
}

MUX_RESULT CComsysMod::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IComsysControl *>(this);
    }
    else if (IID_IComsysControl == iid)
    {
        *ppv = static_cast<mux_IComsysControl *>(this);
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

uint32_t CComsysMod::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CComsysMod::Release(void)
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

bool CComsysMod::OpenDatabase(const UTF8 *pPath)
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
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("DB"));
            if (fStarted)
            {
                m_pILog->log_text(T("Comsys module: sqlite3_open failed: "));
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

void CComsysMod::CloseDatabase(void)
{
    if (nullptr != m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Channel data loading from SQLite.
// ---------------------------------------------------------------------------

bool CComsysMod::LoadChannels(void)
{
    if (nullptr == m_db)
    {
        return false;
    }

    const char *sql = "SELECT name, header, type, temp1, temp2, charge, "
                      "charge_who, amount_col, num_messages, chan_obj "
                      "FROM channels;";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (SQLITE_OK != rc)
    {
        return false;
    }

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        struct channel *ch = static_cast<struct channel *>(
            calloc(1, sizeof(struct channel)));
        if (nullptr == ch)
        {
            break;
        }

        const UTF8 *pName = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 0));
        const UTF8 *pHeader = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 1));

        if (nullptr != pName)
        {
            strncpy(reinterpret_cast<char *>(ch->name),
                    reinterpret_cast<const char *>(pName), MAX_CHANNEL_LEN);
            ch->name[MAX_CHANNEL_LEN] = '\0';
        }

        if (nullptr != pHeader)
        {
            strncpy(reinterpret_cast<char *>(ch->header),
                    reinterpret_cast<const char *>(pHeader), MAX_HEADER_LEN);
            ch->header[MAX_HEADER_LEN] = '\0';
        }

        ch->type        = sqlite3_column_int(stmt, 2);
        ch->temp1       = sqlite3_column_int(stmt, 3);
        ch->temp2       = sqlite3_column_int(stmt, 4);
        ch->charge      = sqlite3_column_int(stmt, 5);
        ch->charge_who  = sqlite3_column_int(stmt, 6);
        ch->amount_col  = sqlite3_column_int(stmt, 7);
        ch->num_messages = sqlite3_column_int(stmt, 8);
        ch->chan_obj     = sqlite3_column_int(stmt, 9);
        ch->num_users    = 0;
        ch->max_users    = 0;
        ch->users        = nullptr;
        ch->on_users     = nullptr;

        std::vector<UTF8> key(ch->name,
            ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
        m_channels[key] = ch;
        m_num_channels++;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool CComsysMod::LoadChannelUsers(void)
{
    if (nullptr == m_db)
    {
        return false;
    }

    const char *sql = "SELECT channel_name, who, is_on, comtitle_status, "
                      "gag_joinleave, title FROM channel_users;";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (SQLITE_OK != rc)
    {
        return false;
    }

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        const UTF8 *pChanName = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 0));
        if (nullptr == pChanName)
        {
            continue;
        }

        struct channel *ch = select_channel(pChanName);
        if (nullptr == ch)
        {
            continue;
        }

        struct comuser *cu = static_cast<struct comuser *>(
            calloc(1, sizeof(struct comuser)));
        if (nullptr == cu)
        {
            break;
        }

        cu->who             = sqlite3_column_int(stmt, 1);
        cu->bUserIsOn       = (0 != sqlite3_column_int(stmt, 2));
        cu->ComTitleStatus  = (0 != sqlite3_column_int(stmt, 3));
        cu->bGagJoinLeave   = (0 != sqlite3_column_int(stmt, 4));
        cu->on_next         = nullptr;

        const UTF8 *pTitle = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 5));
        if (nullptr != pTitle && pTitle[0] != '\0')
        {
            cu->title = reinterpret_cast<UTF8 *>(
                strdup(reinterpret_cast<const char *>(pTitle)));
        }
        else
        {
            cu->title = nullptr;
        }

        // Grow users array if needed.
        //
        if (ch->num_users >= ch->max_users)
        {
            int newmax = (ch->max_users == 0) ? 8 : ch->max_users * 2;
            struct comuser **newusers = static_cast<struct comuser **>(
                realloc(ch->users, newmax * sizeof(struct comuser *)));
            if (nullptr == newusers)
            {
                free(cu->title);
                free(cu);
                break;
            }
            ch->users = newusers;
            ch->max_users = newmax;
        }
        ch->users[ch->num_users] = cu;
        ch->num_users++;
    }

    sqlite3_finalize(stmt);

    // Sort users arrays by dbref for binary search.
    //
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
    {
        struct channel *ch = it->second;
        if (ch->num_users > 1)
        {
            // Simple insertion sort (users array is typically small).
            //
            for (int i = 1; i < ch->num_users; i++)
            {
                struct comuser *key = ch->users[i];
                int j = i - 1;
                while (j >= 0 && ch->users[j]->who > key->who)
                {
                    ch->users[j + 1] = ch->users[j];
                    j--;
                }
                ch->users[j + 1] = key;
            }
        }
    }

    return true;
}

bool CComsysMod::LoadPlayerChannels(void)
{
    if (nullptr == m_db)
    {
        return false;
    }

    const char *sql = "SELECT who, alias, channel_name FROM player_channels "
                      "ORDER BY who;";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (SQLITE_OK != rc)
    {
        return false;
    }

    dbref current_who = NOTHING;
    comsys_t *c = nullptr;

    while (SQLITE_ROW == sqlite3_step(stmt))
    {
        dbref who = sqlite3_column_int(stmt, 0);
        const UTF8 *pAlias = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 1));
        const UTF8 *pChan = reinterpret_cast<const UTF8 *>(
            sqlite3_column_text(stmt, 2));

        if (nullptr == pAlias || nullptr == pChan)
        {
            continue;
        }

        if (who != current_who)
        {
            if (nullptr != c)
            {
                add_comsys(c);
            }
            c = create_new_comsys();
            c->who = who;
            current_who = who;
        }

        // Grow arrays if needed.
        //
        if (c->numchannels >= c->maxchannels)
        {
            int newmax = (c->maxchannels == 0) ? 4 : c->maxchannels * 2;
            UTF8 *newAlias = static_cast<UTF8 *>(
                realloc(c->alias, newmax * ALIAS_SIZE));
            UTF8 **newChannels = static_cast<UTF8 **>(
                realloc(c->channels, newmax * sizeof(UTF8 *)));
            if (nullptr == newAlias || nullptr == newChannels)
            {
                if (nullptr != newAlias) c->alias = newAlias;
                if (nullptr != newChannels) c->channels = newChannels;
                break;
            }
            c->alias = newAlias;
            c->channels = newChannels;
            c->maxchannels = newmax;
        }

        // Copy alias into the contiguous alias buffer.
        //
        UTF8 *pSlot = c->alias + c->numchannels * ALIAS_SIZE;
        strncpy(reinterpret_cast<char *>(pSlot),
                reinterpret_cast<const char *>(pAlias), MAX_ALIAS_LEN);
        pSlot[MAX_ALIAS_LEN] = '\0';

        // Clone channel name.
        //
        c->channels[c->numchannels] = reinterpret_cast<UTF8 *>(
            strdup(reinterpret_cast<const char *>(pChan)));

        c->numchannels++;
    }

    if (nullptr != c)
    {
        add_comsys(c);
    }

    sqlite3_finalize(stmt);
    return true;
}

// ---------------------------------------------------------------------------
// Internal data structure helpers.
// ---------------------------------------------------------------------------

struct channel *CComsysMod::select_channel(const UTF8 *name)
{
    std::vector<UTF8> key(name,
        name + strlen(reinterpret_cast<const char *>(name)) + 1);
    auto it = m_channels.find(key);
    if (it != m_channels.end())
    {
        return it->second;
    }

    // Case-insensitive fallback.
    //
    for (auto it2 = m_channels.begin(); it2 != m_channels.end(); ++it2)
    {
        if (0 == strcasecmp(reinterpret_cast<const char *>(name),
                            reinterpret_cast<const char *>(it2->second->name)))
        {
            return it2->second;
        }
    }
    return nullptr;
}

struct comuser *CComsysMod::select_user(struct channel *ch, dbref player)
{
    if (nullptr == ch || nullptr == ch->users)
    {
        return nullptr;
    }

    // Binary search by dbref.
    //
    int lo = 0;
    int hi = ch->num_users - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (ch->users[mid]->who == player)
        {
            return ch->users[mid];
        }
        else if (ch->users[mid]->who < player)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return nullptr;
}

comsys_t *CComsysMod::create_new_comsys(void)
{
    comsys_t *c = static_cast<comsys_t *>(calloc(1, sizeof(comsys_t)));
    c->who = NOTHING;
    return c;
}

void CComsysMod::add_comsys(comsys_t *c)
{
    if (c->who < 0)
    {
        return;
    }
    int bucket = c->who % NUM_COMSYS;
    c->next = m_comsys_table[bucket];
    m_comsys_table[bucket] = c;
}

comsys_t *CComsysMod::get_comsys(dbref who)
{
    if (who < 0)
    {
        return nullptr;
    }

    comsys_t *c = m_comsys_table[who % NUM_COMSYS];
    while (nullptr != c && c->who != who)
    {
        c = c->next;
    }

    if (nullptr == c)
    {
        c = create_new_comsys();
        c->who = who;
        add_comsys(c);
    }
    return c;
}

const UTF8 *CComsysMod::get_channel_from_alias(dbref player, const UTF8 *alias)
{
    static const UTF8 empty[] = { '\0' };

    comsys_t *c = get_comsys(player);
    if (nullptr == c || c->numchannels <= 0)
    {
        return empty;
    }

    // Linear search through aliases.
    //
    for (int i = 0; i < c->numchannels; i++)
    {
        if (0 == strcmp(reinterpret_cast<const char *>(c->alias + i * ALIAS_SIZE),
                        reinterpret_cast<const char *>(alias)))
        {
            return c->channels[i];
        }
    }
    return empty;
}

// ---------------------------------------------------------------------------
// Connect/disconnect helpers.
// ---------------------------------------------------------------------------

void CComsysMod::do_comconnectraw_notify(dbref player, UTF8 *chan)
{
    struct channel *ch = select_channel(chan);
    if (nullptr == ch)
    {
        return;
    }
    struct comuser *cu = select_user(ch, player);
    if (nullptr == cu)
    {
        return;
    }

    // Only send if channel is LOUD, user is on, and player is not hidden.
    //
    if (  (ch->type & CHANNEL_LOUD)
       && cu->bUserIsOn)
    {
        // Check if player is hidden via object info.
        // For now, send the notification unconditionally.
        // TODO: Check Hidden(player) via COM interface.
        //
        // Build a simple connect message.
        //
        UTF8 msg[1024];
        const UTF8 *pName = nullptr;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->GetName(player, &pName);
        }
        if (nullptr == pName)
        {
            pName = T("???");
        }

        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s has connected.",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pName));

        // Notify all on_users.
        //
        if (nullptr != m_pINotify)
        {
            for (struct comuser *u = ch->on_users; u; u = u->on_next)
            {
                if (u->bUserIsOn && !u->bGagJoinLeave)
                {
                    m_pINotify->RawNotify(u->who, msg);
                }
            }
        }
    }
}

void CComsysMod::do_comdisconnectraw_notify(dbref player, UTF8 *chan)
{
    struct channel *ch = select_channel(chan);
    if (nullptr == ch)
    {
        return;
    }
    struct comuser *cu = select_user(ch, player);
    if (nullptr == cu)
    {
        return;
    }

    if (  (ch->type & CHANNEL_LOUD)
       && cu->bUserIsOn)
    {
        UTF8 msg[1024];
        const UTF8 *pName = nullptr;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->GetName(player, &pName);
        }
        if (nullptr == pName)
        {
            pName = T("???");
        }

        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s has disconnected.",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pName));

        if (nullptr != m_pINotify)
        {
            for (struct comuser *u = ch->on_users; u; u = u->on_next)
            {
                if (u->bUserIsOn && !u->bGagJoinLeave)
                {
                    m_pINotify->RawNotify(u->who, msg);
                }
            }
        }
    }
}

void CComsysMod::do_comconnectchannel(dbref player, UTF8 *channel,
    UTF8 *alias, int i)
{
    struct channel *ch = select_channel(channel);
    if (nullptr != ch)
    {
        // Check if already on on_users list.
        //
        struct comuser *user;
        for (user = ch->on_users; user && user->who != player;
             user = user->on_next)
        {
        }

        if (nullptr == user)
        {
            user = select_user(ch, player);
            if (nullptr != user)
            {
                user->on_next = ch->on_users;
                ch->on_users = user;
            }
            else if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "Bad Comsys Alias: %s for Channel: %s",
                         reinterpret_cast<const char *>(alias + i * ALIAS_SIZE),
                         reinterpret_cast<const char *>(channel));
                m_pINotify->RawNotify(player, msg);
            }
        }
    }
    else if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Bad Comsys Alias: %s for Channel: %s",
                 reinterpret_cast<const char *>(alias + i * ALIAS_SIZE),
                 reinterpret_cast<const char *>(channel));
        m_pINotify->RawNotify(player, msg);
    }
}

void CComsysMod::do_comdisconnectchannel(dbref player, UTF8 *channel)
{
    struct channel *ch = select_channel(channel);
    if (nullptr == ch)
    {
        return;
    }

    struct comuser *prevuser = nullptr;
    for (struct comuser *user = ch->on_users; user;)
    {
        if (user->who == player)
        {
            if (nullptr != prevuser)
            {
                prevuser->on_next = user->on_next;
            }
            else
            {
                ch->on_users = user->on_next;
            }
            return;
        }
        prevuser = user;
        user = user->on_next;
    }
}

// ---------------------------------------------------------------------------
// SQLite write-through helpers.
// ---------------------------------------------------------------------------

void CComsysMod::sqlite_wt_channel_user(const UTF8 *channel_name,
    struct comuser *user)
{
    if (nullptr == m_db)
    {
        return;
    }

    const char *sql =
        "INSERT OR REPLACE INTO channel_users "
        "(channel_name, who, is_on, comtitle_status, gag_joinleave, title) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_text(stmt, 1,
        reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user->who);
    sqlite3_bind_int(stmt, 3, user->bUserIsOn ? 1 : 0);
    sqlite3_bind_int(stmt, 4, user->ComTitleStatus ? 1 : 0);
    sqlite3_bind_int(stmt, 5, user->bGagJoinLeave ? 1 : 0);
    sqlite3_bind_text(stmt, 6,
        (nullptr != user->title)
            ? reinterpret_cast<const char *>(user->title) : "",
        -1, SQLITE_STATIC);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CComsysMod::sqlite_wt_channel(struct channel *ch)
{
    if (nullptr == m_db)
    {
        return;
    }

    const char *sql =
        "INSERT OR REPLACE INTO channels "
        "(name, header, type, temp1, temp2, charge, charge_who, "
        "amount_col, num_messages, chan_obj) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_text(stmt, 1,
        reinterpret_cast<const char *>(ch->name), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2,
        reinterpret_cast<const char *>(ch->header), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, ch->type);
    sqlite3_bind_int(stmt, 4, ch->temp1);
    sqlite3_bind_int(stmt, 5, ch->temp2);
    sqlite3_bind_int(stmt, 6, ch->charge);
    sqlite3_bind_int(stmt, 7, ch->charge_who);
    sqlite3_bind_int(stmt, 8, ch->amount_col);
    sqlite3_bind_int(stmt, 9, ch->num_messages);
    sqlite3_bind_int(stmt, 10, ch->chan_obj);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CComsysMod::sqlite_wt_player_channel(dbref who, const UTF8 *alias,
    const UTF8 *channel_name)
{
    if (nullptr == m_db)
    {
        return;
    }

    const char *sql =
        "INSERT OR REPLACE INTO player_channels (who, alias, channel_name) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, who);
    sqlite3_bind_text(stmt, 2,
        reinterpret_cast<const char *>(alias), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3,
        reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CComsysMod::sqlite_wt_delete_player_channel(dbref who, const UTF8 *alias)
{
    if (nullptr == m_db)
    {
        return;
    }

    const char *sql =
        "DELETE FROM player_channels WHERE who = ? AND alias = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_int(stmt, 1, who);
    sqlite3_bind_text(stmt, 2,
        reinterpret_cast<const char *>(alias), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void CComsysMod::sqlite_wt_delete_channel_user(const UTF8 *channel_name,
    dbref who)
{
    if (nullptr == m_db)
    {
        return;
    }

    const char *sql =
        "DELETE FROM channel_users WHERE channel_name = ? AND who = ?;";

    sqlite3_stmt *stmt = nullptr;
    if (SQLITE_OK != sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
    {
        return;
    }

    sqlite3_bind_text(stmt, 1,
        reinterpret_cast<const char *>(channel_name), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, who);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---------------------------------------------------------------------------
// Channel access checks.
// ---------------------------------------------------------------------------

bool CComsysMod::test_transmit_access(dbref player, struct channel *ch)
{
    if (nullptr != m_pIPermissions)
    {
        bool bCommAll = false;
        m_pIPermissions->HasCommAll(player, &bCommAll);
        if (bCommAll)
        {
            return true;
        }
    }

    int access;
    if (nullptr != m_pIObjectInfo)
    {
        bool bPlayer = false;
        m_pIObjectInfo->IsPlayer(player, &bPlayer);
        access = bPlayer ? CHANNEL_PLAYER_TRANSMIT : CHANNEL_OBJECT_TRANSMIT;
    }
    else
    {
        access = CHANNEL_PLAYER_TRANSMIT;
    }

    return ((ch->type & access) != 0);
}

bool CComsysMod::test_receive_access(dbref player, struct channel *ch)
{
    if (nullptr != m_pIPermissions)
    {
        bool bCommAll = false;
        m_pIPermissions->HasCommAll(player, &bCommAll);
        if (bCommAll)
        {
            return true;
        }
    }

    int access;
    if (nullptr != m_pIObjectInfo)
    {
        bool bPlayer = false;
        m_pIObjectInfo->IsPlayer(player, &bPlayer);
        access = bPlayer ? CHANNEL_PLAYER_RECEIVE : CHANNEL_OBJECT_RECEIVE;
    }
    else
    {
        access = CHANNEL_PLAYER_RECEIVE;
    }

    return ((ch->type & access) != 0);
}

bool CComsysMod::test_join_access(dbref player, struct channel *ch)
{
    if (nullptr != m_pIPermissions)
    {
        bool bCommAll = false;
        m_pIPermissions->HasCommAll(player, &bCommAll);
        if (bCommAll)
        {
            return true;
        }
    }

    int access;
    if (nullptr != m_pIObjectInfo)
    {
        bool bPlayer = false;
        m_pIObjectInfo->IsPlayer(player, &bPlayer);
        access = bPlayer ? CHANNEL_PLAYER_JOIN : CHANNEL_OBJECT_JOIN;
    }
    else
    {
        access = CHANNEL_PLAYER_JOIN;
    }

    return ((ch->type & access) != 0);
}

// ---------------------------------------------------------------------------
// Unsubscribe a player from a channel completely.
// ---------------------------------------------------------------------------

void CComsysMod::do_delcomchannel(dbref player, const UTF8 *channel,
    bool bQuiet)
{
    struct channel *ch = select_channel(channel);
    if (nullptr == ch)
    {
        return;
    }

    struct comuser *user = select_user(ch, player);
    if (nullptr == user)
    {
        return;
    }

    if (user->bUserIsOn)
    {
        if (!bQuiet && nullptr != m_pINotify)
        {
            const UTF8 *pName = nullptr;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->GetMoniker(player, &pName);
            }
            if (nullptr == pName)
            {
                pName = T("???");
            }

            UTF8 msg[1024];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%s %s has left this channel.",
                     reinterpret_cast<const char *>(ch->header),
                     reinterpret_cast<const char *>(pName));
            SendChannelMessage(player, ch, msg, true);
        }

        // Remove from on_users list.
        //
        do_comdisconnectchannel(player,
            const_cast<UTF8 *>(channel));
    }

    // Remove from users array.
    //
    for (int i = 0; i < ch->num_users; i++)
    {
        if (ch->users[i] == user)
        {
            if (nullptr != user->title)
            {
                free(user->title);
            }
            free(user);
            ch->num_users--;
            for (int j = i; j < ch->num_users; j++)
            {
                ch->users[j] = ch->users[j + 1];
            }
            break;
        }
    }

    sqlite_wt_delete_channel_user(channel, player);
}

// ---------------------------------------------------------------------------
// Sort aliases for binary search.
// ---------------------------------------------------------------------------

void CComsysMod::sort_com_aliases(comsys_t *c)
{
    // Insertion sort by alias name.
    //
    for (int i = 1; i < c->numchannels; i++)
    {
        UTF8 tmpAlias[ALIAS_SIZE];
        memcpy(tmpAlias, c->alias + i * ALIAS_SIZE, ALIAS_SIZE);
        UTF8 *tmpChan = c->channels[i];

        int j = i - 1;
        while (j >= 0 && strcmp(reinterpret_cast<const char *>(c->alias + j * ALIAS_SIZE),
                                reinterpret_cast<const char *>(tmpAlias)) > 0)
        {
            memcpy(c->alias + (j + 1) * ALIAS_SIZE,
                   c->alias + j * ALIAS_SIZE, ALIAS_SIZE);
            c->channels[j + 1] = c->channels[j];
            j--;
        }
        memcpy(c->alias + (j + 1) * ALIAS_SIZE, tmpAlias, ALIAS_SIZE);
        c->channels[j + 1] = tmpChan;
    }
}

// ---------------------------------------------------------------------------
// Channel message broadcasting.
// ---------------------------------------------------------------------------

void CComsysMod::SendChannelMessage(dbref executor, struct channel *ch,
    const UTF8 *msg, bool bJoinLeaveMsg)
{
    ch->num_messages++;
    sqlite_wt_channel(ch);

    if (nullptr == m_pINotify)
    {
        return;
    }

    for (struct comuser *user = ch->on_users; user; user = user->on_next)
    {
        if (bJoinLeaveMsg && user->bGagJoinLeave)
        {
            continue;
        }
        if (user->bUserIsOn && test_receive_access(user->who, ch))
        {
            m_pINotify->RawNotify(user->who, msg);
        }
    }
}

// ---------------------------------------------------------------------------
// Join/leave channel.
// ---------------------------------------------------------------------------

void CComsysMod::do_joinchannel(dbref player, struct channel *ch)
{
    struct comuser *user = select_user(ch, player);

    if (nullptr == user)
    {
        // Create new user on channel.
        //
        user = static_cast<struct comuser *>(calloc(1, sizeof(struct comuser)));
        if (nullptr == user)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(player, T("Out of memory."));
            }
            return;
        }

        ch->num_users++;
        if (ch->num_users >= ch->max_users)
        {
            int newmax = ch->num_users + 10;
            struct comuser **cu = static_cast<struct comuser **>(
                realloc(ch->users, newmax * sizeof(struct comuser *)));
            if (nullptr == cu)
            {
                free(user);
                ch->num_users--;
                return;
            }
            ch->users = cu;
            ch->max_users = newmax;
        }

        // Insert sorted by dbref.
        //
        int i;
        for (i = ch->num_users - 1;
             0 < i && player < ch->users[i - 1]->who; --i)
        {
            ch->users[i] = ch->users[i - 1];
        }
        ch->users[i] = user;

        user->who = player;
        user->bUserIsOn = true;
        user->ComTitleStatus = true;
        user->bGagJoinLeave = false;
        user->title = reinterpret_cast<UTF8 *>(strdup(""));
        user->on_next = ch->on_users;
        ch->on_users = user;

        sqlite_wt_channel_user(ch->name, user);
    }
    else if (!user->bUserIsOn)
    {
        user->bUserIsOn = true;
        sqlite_wt_channel_user(ch->name, user);
    }
    else
    {
        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "You are already on channel %s.",
                     reinterpret_cast<const char *>(ch->name));
            m_pINotify->RawNotify(player, msg);
        }
        return;
    }

    // Send join notification.
    //
    const UTF8 *pName = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetMoniker(player, &pName);
    }
    if (nullptr == pName)
    {
        pName = T("???");
    }

    UTF8 msg[1024];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "%s %s has joined this channel.",
             reinterpret_cast<const char *>(ch->header),
             reinterpret_cast<const char *>(pName));
    SendChannelMessage(player, ch, msg, true);
}

void CComsysMod::do_leavechannel(dbref player, struct channel *ch)
{
    struct comuser *user = select_user(ch, player);
    if (nullptr == user)
    {
        return;
    }

    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "You have left channel %s.",
                 reinterpret_cast<const char *>(ch->name));
        m_pINotify->RawNotify(player, msg);
    }

    if (user->bUserIsOn)
    {
        // Send leave notification before turning off.
        //
        const UTF8 *pName = nullptr;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->GetMoniker(player, &pName);
        }
        if (nullptr == pName)
        {
            pName = T("???");
        }

        UTF8 msg[1024];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s has left this channel.",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pName));
        SendChannelMessage(player, ch, msg, true);

        user->bUserIsOn = false;
        sqlite_wt_channel_user(ch->name, user);
    }
}

// ---------------------------------------------------------------------------
// Channel who list.
// ---------------------------------------------------------------------------

void CComsysMod::do_comwho(dbref player, struct channel *ch)
{
    if (nullptr == m_pINotify || nullptr == m_pIObjectInfo)
    {
        return;
    }

    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "-- %s --", reinterpret_cast<const char *>(ch->name));
    m_pINotify->RawNotify(player, msg);

    int count = 0;
    for (struct comuser *user = ch->on_users; user; user = user->on_next)
    {
        if (user->bUserIsOn)
        {
            const UTF8 *pName = nullptr;
            m_pIObjectInfo->GetMoniker(user->who, &pName);
            if (nullptr != pName)
            {
                m_pINotify->RawNotify(player, pName);
                count++;
            }
        }
    }

    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "-- %s -- %d connected", reinterpret_cast<const char *>(ch->name),
             count);
    m_pINotify->RawNotify(player, msg);
}

// ---------------------------------------------------------------------------
// Process a channel message (alias dispatch target).
// ---------------------------------------------------------------------------

void CComsysMod::do_processcom(dbref player, const UTF8 *arg1, UTF8 *arg2)
{
    if (nullptr == arg2 || '\0' == *arg2)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player, T("No message."));
        }
        return;
    }

    // Truncate excessively long messages.
    //
    if (3500 < strlen(reinterpret_cast<const char *>(arg2)))
    {
        arg2[3500] = '\0';
    }

    struct channel *ch = select_channel(arg1);
    if (nullptr == ch)
    {
        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Unknown channel %s.",
                     reinterpret_cast<const char *>(arg1));
            m_pINotify->RawNotify(player, msg);
        }
        return;
    }

    struct comuser *user = select_user(ch, player);
    if (nullptr == user)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("You are not listed as on that channel.  "
                  "Delete this alias and readd."));
        }
        return;
    }

    // Handle sub-commands.
    //
    if (0 == strcmp(reinterpret_cast<const char *>(arg2), "on"))
    {
        do_joinchannel(player, ch);
        return;
    }
    if (0 == strcmp(reinterpret_cast<const char *>(arg2), "off"))
    {
        do_leavechannel(player, ch);
        return;
    }

    if (!user->bUserIsOn)
    {
        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "You must be on %s to do that.",
                     reinterpret_cast<const char *>(arg1));
            m_pINotify->RawNotify(player, msg);
        }
        return;
    }

    if (0 == strcmp(reinterpret_cast<const char *>(arg2), "who"))
    {
        do_comwho(player, ch);
        return;
    }

    // Check transmit access.
    //
    if (!test_transmit_access(player, ch))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("That channel type cannot be transmitted on."));
        }
        return;
    }

    // Build and send the message.
    //
    const UTF8 *pMoniker = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetMoniker(player, &pMoniker);
    }
    if (nullptr == pMoniker)
    {
        pMoniker = T("???");
    }

    UTF8 msg[4096];
    const char *pPose = reinterpret_cast<const char *>(arg2);

    if (':' == pPose[0])
    {
        // Pose: "<header> <name> <action>"
        //
        pPose++;
        if (' ' == *pPose)
        {
            pPose++;
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%s %s%s",
                     reinterpret_cast<const char *>(ch->header),
                     reinterpret_cast<const char *>(pMoniker),
                     pPose);
        }
        else
        {
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%s %s %s",
                     reinterpret_cast<const char *>(ch->header),
                     reinterpret_cast<const char *>(pMoniker),
                     pPose);
        }
    }
    else if (';' == pPose[0])
    {
        // Semipose: "<header> <name><text>"
        //
        pPose++;
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s%s",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pMoniker),
                 pPose);
    }
    else
    {
        // Say: "<header> <name> says, \xe2\x80\x9c<text>\xe2\x80\x9d"
        //
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s says, \xE2\x80\x9C%s\xE2\x80\x9D",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pMoniker),
                 pPose);
    }

    SendChannelMessage(player, ch, msg, false);
}

// ---------------------------------------------------------------------------
// mux_IComsysControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::Initialize(const UTF8 *pDatabasePath)
{
    if (nullptr == pDatabasePath)
    {
        return MUX_E_INVALIDARG;
    }

    if (!OpenDatabase(pDatabasePath))
    {
        return MUX_E_FAIL;
    }

    // Load all channel data from SQLite.
    //
    if (!LoadChannels())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Comsys module: LoadChannels failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadChannelUsers())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Comsys module: LoadChannelUsers failed."));
                m_pILog->end_log();
            }
        }
    }

    if (!LoadPlayerChannels())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("LOAD"));
            if (fStarted)
            {
                m_pILog->log_text(T("Comsys module: LoadPlayerChannels failed."));
                m_pILog->end_log();
            }
        }
    }

    if (nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("LOAD"));
        if (fStarted)
        {
            m_pILog->log_text(T("Comsys module: loaded "));
            m_pILog->log_number(m_num_channels);
            m_pILog->log_text(T(" channels from SQLite."));
            m_pILog->end_log();
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerConnect(dbref player)
{
    comsys_t *c = get_comsys(player);
    if (nullptr == c)
    {
        return MUX_S_OK;
    }

    for (int i = 0; i < c->numchannels; i++)
    {
        UTF8 *CurrentChannel = c->channels[i];
        bool bFound = false;

        for (int j = 0; j < i; j++)
        {
            if (0 == strcmp(reinterpret_cast<char *>(c->channels[j]),
                            reinterpret_cast<char *>(CurrentChannel)))
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            do_comconnectchannel(player, CurrentChannel, c->alias, i);
            do_comconnectraw_notify(player, CurrentChannel);
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerDisconnect(dbref player)
{
    comsys_t *c = get_comsys(player);
    if (nullptr == c)
    {
        return MUX_S_OK;
    }

    for (int i = 0; i < c->numchannels; i++)
    {
        UTF8 *CurrentChannel = c->channels[i];
        bool bFound = false;

        for (int j = 0; j < i; j++)
        {
            if (0 == strcmp(reinterpret_cast<char *>(c->channels[j]),
                            reinterpret_cast<char *>(CurrentChannel)))
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            do_comdisconnectchannel(player, CurrentChannel);
            do_comdisconnectraw_notify(player, CurrentChannel);
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerNuke(dbref player)
{
    // Remove all channels owned by this player.
    //
    bool found = true;
    while (found)
    {
        found = false;
        for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
        {
            struct channel *ch = it->second;
            if (player == ch->charge_who)
            {
                m_num_channels--;

                // Delete from SQLite.
                //
                if (nullptr != m_db)
                {
                    const char *sql = "DELETE FROM channels WHERE name = ?;";
                    sqlite3_stmt *stmt = nullptr;
                    if (SQLITE_OK == sqlite3_prepare_v2(m_db, sql, -1,
                                                         &stmt, nullptr))
                    {
                        sqlite3_bind_text(stmt, 1,
                            reinterpret_cast<const char *>(ch->name), -1,
                            SQLITE_STATIC);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }

                // Free users.
                //
                if (nullptr != ch->users)
                {
                    for (int j = 0; j < ch->num_users; j++)
                    {
                        if (nullptr != ch->users[j])
                        {
                            if (nullptr != ch->users[j]->title)
                            {
                                free(ch->users[j]->title);
                            }
                            free(ch->users[j]);
                        }
                    }
                    free(ch->users);
                }
                free(ch);
                m_channels.erase(it);
                found = true;
                break;
            }
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::ProcessCommand(dbref executor, const UTF8 *pCmd,
    bool *pbHandled)
{
    *pbHandled = false;

    // Parse "alias message" pattern.
    //
    const char *t = strchr(reinterpret_cast<const char *>(pCmd), ' ');
    if (  nullptr == t
       || t - reinterpret_cast<const char *>(pCmd) > MAX_ALIAS_LEN
       || t[1] == '\0')
    {
        return MUX_S_OK;
    }

    // Extract alias.
    //
    UTF8 alias[ALIAS_SIZE];
    size_t nAlias = t - reinterpret_cast<const char *>(pCmd);
    memcpy(alias, pCmd, nAlias);
    alias[nAlias] = '\0';

    // Look up channel from alias.
    //
    const UTF8 *ch = get_channel_from_alias(executor, alias);
    if (ch[0] == '\0')
    {
        return MUX_S_OK;
    }

    // Found a valid alias — dispatch to the channel message processor.
    //
    UTF8 *pMsg = const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(t + 1));
    do_processcom(executor, ch, pMsg);
    *pbHandled = true;
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// Alias management.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::AddAlias(dbref executor, const UTF8 *pAlias,
    const UTF8 *pChannel)
{
    if (nullptr == pAlias || '\0' == pAlias[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You need to specify a valid alias."));
        }
        return MUX_E_INVALIDARG;
    }
    if (nullptr == pChannel || '\0' == pChannel[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You need to specify a channel."));
        }
        return MUX_E_INVALIDARG;
    }

    // Validate alias length.
    //
    size_t nAlias = strlen(reinterpret_cast<const char *>(pAlias));
    if (nAlias > MAX_ALIAS_LEN)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You need to specify a valid alias."));
        }
        return MUX_E_INVALIDARG;
    }

    struct channel *ch = select_channel(pChannel);
    if (nullptr == ch)
    {
        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Channel %s does not exist yet.",
                     reinterpret_cast<const char *>(pChannel));
            m_pINotify->RawNotify(executor, msg);
        }
        return MUX_E_NOTFOUND;
    }

    if (!test_join_access(executor, ch))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Sorry, this channel type does not allow you to join."));
        }
        return MUX_E_PERMISSION;
    }

    comsys_t *c = get_comsys(executor);
    if (c->numchannels >= MAX_ALIASES_PER_PLAYER)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Sorry, but you have reached the maximum number of "
                  "aliases allowed."));
        }
        return MUX_E_FAIL;
    }

    // Check for duplicate alias.
    //
    for (int j = 0; j < c->numchannels; j++)
    {
        if (0 == strcmp(reinterpret_cast<const char *>(pAlias),
                        reinterpret_cast<const char *>(c->alias + j * ALIAS_SIZE)))
        {
            if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "That alias is already in use for channel %s.",
                         reinterpret_cast<const char *>(c->channels[j]));
                m_pINotify->RawNotify(executor, msg);
            }
            return MUX_E_FAIL;
        }
    }

    // Grow arrays if needed.
    //
    if (c->numchannels >= c->maxchannels)
    {
        int newmax = c->maxchannels + 10;
        UTF8 *na = static_cast<UTF8 *>(
            realloc(c->alias, ALIAS_SIZE * newmax));
        UTF8 **nc = static_cast<UTF8 **>(
            realloc(c->channels, sizeof(UTF8 *) * newmax));
        if (nullptr == na || nullptr == nc)
        {
            if (nullptr != na) c->alias = na;
            if (nullptr != nc) c->channels = nc;
            return MUX_E_OUTOFMEMORY;
        }
        c->alias = na;
        c->channels = nc;
        c->maxchannels = newmax;
    }

    // Append and sort.
    //
    int where = c->numchannels;
    memcpy(c->alias + where * ALIAS_SIZE, pAlias, nAlias);
    *(c->alias + where * ALIAS_SIZE + nAlias) = '\0';
    c->channels[where] = reinterpret_cast<UTF8 *>(
        strdup(reinterpret_cast<const char *>(ch->name)));
    c->numchannels++;

    sort_com_aliases(c);
    sqlite_wt_player_channel(executor, pAlias, ch->name);

    // Join channel if not already a user.
    //
    if (nullptr == select_user(ch, executor))
    {
        do_joinchannel(executor, ch);
    }

    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Channel %s added with alias %s.",
                 reinterpret_cast<const char *>(ch->name),
                 reinterpret_cast<const char *>(pAlias));
        m_pINotify->RawNotify(executor, msg);
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::DelAlias(dbref executor, const UTF8 *pAlias)
{
    if (nullptr == pAlias || '\0' == pAlias[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Need an alias to delete."));
        }
        return MUX_E_INVALIDARG;
    }

    comsys_t *c = get_comsys(executor);

    for (int i = 0; i < c->numchannels; i++)
    {
        if (0 != strcmp(reinterpret_cast<const char *>(pAlias),
                        reinterpret_cast<const char *>(c->alias + i * ALIAS_SIZE)))
        {
            continue;
        }

        // Count other aliases for same channel.
        //
        int found = 0;
        for (int itmp = 0; itmp < c->numchannels; itmp++)
        {
            if (0 == strcmp(reinterpret_cast<const char *>(c->channels[itmp]),
                            reinterpret_cast<const char *>(c->channels[i])))
            {
                found++;
            }
        }

        // If last alias for this channel, remove from channel.
        //
        if (found <= 1)
        {
            do_delcomchannel(executor, c->channels[i], false);
            if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "Alias %s for channel %s deleted.",
                         reinterpret_cast<const char *>(pAlias),
                         reinterpret_cast<const char *>(c->channels[i]));
                m_pINotify->RawNotify(executor, msg);
            }
            free(c->channels[i]);
        }
        else
        {
            if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "Alias %s for channel %s deleted.",
                         reinterpret_cast<const char *>(pAlias),
                         reinterpret_cast<const char *>(c->channels[i]));
                m_pINotify->RawNotify(executor, msg);
            }
        }

        sqlite_wt_delete_player_channel(executor, pAlias);
        c->channels[i] = nullptr;
        c->numchannels--;

        for (; i < c->numchannels; i++)
        {
            memcpy(c->alias + i * ALIAS_SIZE,
                   c->alias + (i + 1) * ALIAS_SIZE, ALIAS_SIZE);
            c->channels[i] = c->channels[i + 1];
        }
        return MUX_S_OK;
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(executor, T("Unable to find that alias."));
    }
    return MUX_E_NOTFOUND;
}

MUX_RESULT CComsysMod::ClearAliases(dbref executor)
{
    comsys_t *c = get_comsys(executor);
    if (nullptr == c)
    {
        return MUX_S_OK;
    }

    // Walk backwards to safely remove each alias.
    //
    while (c->numchannels > 0)
    {
        const UTF8 *pAlias = c->alias + (c->numchannels - 1) * ALIAS_SIZE;
        DelAlias(executor, pAlias);
    }

    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// Channel creation/destruction.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::CreateChannel(dbref executor, const UTF8 *pName)
{
    if (nullptr == pName || '\0' == pName[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You must specify a channel to create."));
        }
        return MUX_E_INVALIDARG;
    }

    // Check permission.
    //
    if (nullptr != m_pIPermissions)
    {
        bool bCommAll = false;
        m_pIPermissions->HasCommAll(executor, &bCommAll);
        if (!bCommAll)
        {
            if (nullptr != m_pINotify)
            {
                m_pINotify->RawNotify(executor,
                    T("You do not have permission to create channels."));
            }
            return MUX_E_PERMISSION;
        }
    }

    // Check for existing channel.
    //
    if (nullptr != select_channel(pName))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("That channel already exists."));
        }
        return MUX_E_FAIL;
    }

    // Create the channel.
    //
    struct channel *ch = static_cast<struct channel *>(
        calloc(1, sizeof(struct channel)));
    if (nullptr == ch)
    {
        return MUX_E_OUTOFMEMORY;
    }

    strncpy(reinterpret_cast<char *>(ch->name),
            reinterpret_cast<const char *>(pName), MAX_CHANNEL_LEN);
    ch->name[MAX_CHANNEL_LEN] = '\0';

    snprintf(reinterpret_cast<char *>(ch->header), MAX_HEADER_LEN + 1,
             "[%s]", reinterpret_cast<const char *>(ch->name));

    ch->type = CHANNEL_DEFAULT;
    ch->charge_who = executor;
    ch->chan_obj = NOTHING;

    std::vector<UTF8> key(ch->name,
        ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
    m_channels[key] = ch;
    m_num_channels++;

    sqlite_wt_channel(ch);

    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Channel %s created.",
                 reinterpret_cast<const char *>(ch->name));
        m_pINotify->RawNotify(executor, msg);
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::DestroyChannel(dbref executor, const UTF8 *pName)
{
    if (nullptr == pName || '\0' == pName[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You must specify a channel to destroy."));
        }
        return MUX_E_INVALIDARG;
    }

    struct channel *ch = select_channel(pName);
    if (nullptr == ch)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("That channel does not exist."));
        }
        return MUX_E_NOTFOUND;
    }

    // Check permission: must be owner or Comm_All.
    //
    bool bAllowed = (ch->charge_who == executor);
    if (!bAllowed && nullptr != m_pIPermissions)
    {
        bool bCommAll = false;
        m_pIPermissions->HasCommAll(executor, &bCommAll);
        bAllowed = bCommAll;
    }

    if (!bAllowed)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("You do not have permission to destroy that channel."));
        }
        return MUX_E_PERMISSION;
    }

    // Free all users.
    //
    if (nullptr != ch->users)
    {
        for (int j = 0; j < ch->num_users; j++)
        {
            if (nullptr != ch->users[j])
            {
                if (nullptr != ch->users[j]->title)
                {
                    free(ch->users[j]->title);
                }
                free(ch->users[j]);
            }
        }
        free(ch->users);
    }

    // Remove from SQLite.
    //
    if (nullptr != m_db)
    {
        const char *sql = "DELETE FROM channels WHERE name = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (SQLITE_OK == sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr))
        {
            sqlite3_bind_text(stmt, 1,
                reinterpret_cast<const char *>(ch->name), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Remove from map.
    //
    std::vector<UTF8> key(ch->name,
        ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
    m_channels.erase(key);
    m_num_channels--;

    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Channel %s destroyed.",
                 reinterpret_cast<const char *>(pName));
        m_pINotify->RawNotify(executor, msg);
    }

    free(ch);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// mux_IServerEventsSink implementation — server lifecycle events.
// ---------------------------------------------------------------------------

void CComsysMod::startup(void)
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Comsys module startup complete."));
            m_pILog->end_log();
        }
    }
}

void CComsysMod::presync_database(void)
{
    // Nothing needed — SQLite write-through handles persistence.
}

void CComsysMod::presync_database_sigsegv(void)
{
    // Nothing.
}

void CComsysMod::dump_database(int dump_type)
{
    UNUSED_PARAMETER(dump_type);
    // Nothing needed — SQLite WAL checkpoint handles durability.
}

void CComsysMod::dump_complete_signal(void)
{
    // Nothing.
}

void CComsysMod::shutdown(void)
{
    CloseDatabase();

    if (nullptr != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS,
                                            T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("Comsys module shutting down."));
            m_pILog->end_log();
        }
    }
}

void CComsysMod::dbck(void)
{
    // TODO: Channel consistency checks.
}

void CComsysMod::connect(dbref player, int isnew, int num)
{
    UNUSED_PARAMETER(isnew);
    UNUSED_PARAMETER(num);
    PlayerConnect(player);
}

void CComsysMod::disconnect(dbref player, int num)
{
    UNUSED_PARAMETER(num);
    PlayerDisconnect(player);
}

void CComsysMod::data_create(dbref object)
{
    UNUSED_PARAMETER(object);
}

void CComsysMod::data_clone(dbref clone, dbref source)
{
    UNUSED_PARAMETER(clone);
    UNUSED_PARAMETER(source);
}

void CComsysMod::data_free(dbref object)
{
    PlayerNuke(object);
}

// ---------------------------------------------------------------------------
// CComsysModFactory — boilerplate.
// ---------------------------------------------------------------------------

CComsysModFactory::CComsysModFactory(void) : m_cRef(1)
{
}

CComsysModFactory::~CComsysModFactory()
{
}

MUX_RESULT CComsysModFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CComsysModFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CComsysModFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CComsysModFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CComsysMod *pComsysMod = nullptr;
    try
    {
        pComsysMod = new CComsysMod;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pComsysMod)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pComsysMod->FinalConstruct();
    if (MUX_FAILED(mr))
    {
        pComsysMod->Release();
        return mr;
    }

    mr = pComsysMod->QueryInterface(iid, ppv);
    pComsysMod->Release();
    return mr;
}

MUX_RESULT CComsysModFactory::LockServer(bool bLock)
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
