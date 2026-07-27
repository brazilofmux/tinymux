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

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
#include "timeutil.h"
#include "comsys_mod.h"

#include <atomic>
#include <cstring>

// Windows compatibility for POSIX functions.
//
#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

// Module bookkeeping.
//
static std::atomic<uint32_t> g_cComponents{0};
static std::atomic<uint32_t> g_cServerLocks{0};

// Module entry points.
//
static MUX_CLASS_INFO comsys_classes[] =
{
    { CID_ComsysMod }
};
#define NUM_CLASSES (sizeof(comsys_classes)/sizeof(comsys_classes[0]))

// Required by libmux ModuleLoad — without this, bLoaded stays false and
// CreateInstance never finds the class (#1191 root cause).
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, comsys_classes, nullptr);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, comsys_classes);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
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
    m_pIStorage(nullptr),
    m_revision(0)
{
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
    mr = mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
                            IID_INotify,
                            reinterpret_cast<void **>(&m_pINotify));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
                            IID_IObjectInfo,
                            reinterpret_cast<void **>(&m_pIObjectInfo));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_AttributeAccess, nullptr, UseSameProcess,
                            IID_IAttributeAccess,
                            reinterpret_cast<void **>(&m_pIAttributeAccess));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_Evaluator, nullptr, UseSameProcess,
                            IID_IEvaluator,
                            reinterpret_cast<void **>(&m_pIEvaluator));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = mux_CreateInstance(CID_Permissions, nullptr, UseSameProcess,
                            IID_IPermissions,
                            reinterpret_cast<void **>(&m_pIPermissions));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

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
    // Release the storage interface.
    //
    if (nullptr != m_pIStorage)
    {
        m_pIStorage->Release();
        m_pIStorage = nullptr;
    }

    // STL containers clean up automatically.
    //
    m_channels.clear();
    m_comsys.clear();

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
    return m_cRef.fetch_add(1, std::memory_order_relaxed) + 1;
}

uint32_t CComsysMod::Release(void)
{
    uint32_t prev = m_cRef.fetch_sub(1, std::memory_order_acq_rel);
    if (1 == prev)
    {
        delete this;
        return 0;
    }
    return prev - 1;
}

// ---------------------------------------------------------------------------
// Channel data loading via engine storage interface.
// ---------------------------------------------------------------------------

bool CComsysMod::LoadChannels(void)
{
    if (nullptr == m_pIStorage)
    {
        return false;
    }

    MUX_RESULT mr = m_pIStorage->LoadAllChannels(
        [](void *ctx, const UTF8 *name, const UTF8 *header,
            int type, int temp1, int temp2, int charge, int charge_who,
            int amount_col, int num_messages, int chan_obj)
        {
            CComsysMod *self = static_cast<CComsysMod *>(ctx);

            auto ch = std::make_unique<channel>();

            if (nullptr != name)
            {
                strncpy(reinterpret_cast<char *>(ch->name),
                        reinterpret_cast<const char *>(name), MAX_CHANNEL_LEN);
                ch->name[MAX_CHANNEL_LEN] = '\0';
            }
            if (nullptr != header)
            {
                strncpy(reinterpret_cast<char *>(ch->header),
                        reinterpret_cast<const char *>(header), MAX_HEADER_LEN);
                ch->header[MAX_HEADER_LEN] = '\0';
            }

            ch->type         = type;
            ch->temp1        = temp1;
            ch->temp2        = temp2;
            ch->charge       = charge;
            ch->charge_who   = charge_who;
            ch->amount_col   = amount_col;
            ch->num_messages = num_messages;
            ch->chan_obj     = chan_obj;

            std::vector<UTF8> key(ch->name,
                ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
            self->m_channels[key] = std::move(ch);
        }, this);

    return MUX_SUCCEEDED(mr);
}

bool CComsysMod::LoadChannelUsers(void)
{
    if (nullptr == m_pIStorage)
    {
        return false;
    }

    MUX_RESULT mr = m_pIStorage->LoadAllChannelUsers(
        [](void *ctx, const UTF8 *channel_name, int who,
            bool is_on, bool comtitle_status, bool gag_join_leave,
            const UTF8 *title)
        {
            CComsysMod *self = static_cast<CComsysMod *>(ctx);

            if (nullptr == channel_name) return;

            struct channel *ch = self->select_channel(channel_name);
            if (nullptr == ch) return;

            // #1195: skip invalid dbrefs; object listeners are allowed.
            //
            if (nullptr != self->m_pIObjectInfo)
            {
                bool bValid = false;
                self->m_pIObjectInfo->IsValid(who, &bValid);
                if (!bValid)
                {
                    return;
                }
            }

            comuser cu;
            cu.who            = who;
            cu.bUserIsOn      = is_on;
            cu.ComTitleStatus = comtitle_status;
            cu.bGagJoinLeave  = gag_join_leave;
            cu.bConnected     = false;

            if (nullptr != title && title[0] != '\0')
            {
                cu.title = reinterpret_cast<const char *>(title);
            }

            ch->users[who] = std::move(cu);
        }, this);

    return MUX_SUCCEEDED(mr);
}

bool CComsysMod::LoadPlayerChannels(void)
{
    if (nullptr == m_pIStorage)
    {
        return false;
    }

    MUX_RESULT mr = m_pIStorage->LoadAllPlayerChannels(
        [](void *ctx, int who, const UTF8 *alias,
            const UTF8 *channel_name)
        {
            CComsysMod *self = static_cast<CComsysMod *>(ctx);

            if (nullptr == alias || nullptr == channel_name) return;

            comsys_t &c = self->m_comsys[who];
            c.who = who;

            com_alias ca;
            ca.alias = reinterpret_cast<const char *>(alias);
            ca.channel = reinterpret_cast<const char *>(channel_name);
            c.aliases.push_back(std::move(ca));
        }, this);

    return MUX_SUCCEEDED(mr);
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
        return it->second.get();
    }

    // Case-insensitive fallback.
    //
    for (auto &kv : m_channels)
    {
        if (0 == strcasecmp(reinterpret_cast<const char *>(name),
                            reinterpret_cast<const char *>(kv.second->name)))
        {
            return kv.second.get();
        }
    }
    return nullptr;
}

struct comuser *CComsysMod::select_user(struct channel *ch, dbref player)
{
    if (nullptr == ch)
    {
        return nullptr;
    }

    auto it = ch->users.find(player);
    if (it != ch->users.end())
    {
        return &it->second;
    }
    return nullptr;
}

comsys_t &CComsysMod::get_comsys(dbref who)
{
    comsys_t &c = m_comsys[who];
    c.who = who;
    return c;
}

const UTF8 *CComsysMod::get_channel_from_alias(dbref player, const UTF8 *alias)
{
    static const UTF8 empty[] = { '\0' };

    auto it = m_comsys.find(player);
    if (it == m_comsys.end())
    {
        return empty;
    }

    const comsys_t &c = it->second;
    for (const auto &ca : c.aliases)
    {
        if (ca.alias == reinterpret_cast<const char *>(alias))
        {
            return reinterpret_cast<const UTF8 *>(ca.channel.c_str());
        }
    }
    return empty;
}

// ---------------------------------------------------------------------------
// Connect/disconnect helpers.
// ---------------------------------------------------------------------------

void CComsysMod::do_comconnectraw_notify(dbref player, const UTF8 *chan)
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
        UTF8 msg[MOD_LBUF_SIZE];
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

        if (nullptr != m_pINotify)
        {
            for (auto &kv : ch->users)
            {
                comuser &u = kv.second;
                if (u.bConnected && u.bUserIsOn && !u.bGagJoinLeave)
                {
                    m_pINotify->RawNotify(u.who, msg);
                }
            }
        }
    }
}

void CComsysMod::do_comdisconnectraw_notify(dbref player, const UTF8 *chan)
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
        UTF8 msg[MOD_LBUF_SIZE];
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
            for (auto &kv : ch->users)
            {
                comuser &u = kv.second;
                if (u.bConnected && u.bUserIsOn && !u.bGagJoinLeave)
                {
                    m_pINotify->RawNotify(u.who, msg);
                }
            }
        }
    }
}

void CComsysMod::do_comconnectchannel(dbref player, const UTF8 *channel,
    const std::string &alias)
{
    struct channel *ch = select_channel(channel);
    if (nullptr != ch)
    {
        struct comuser *user = select_user(ch, player);
        if (nullptr != user)
        {
            user->bConnected = true;
        }
        else if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Bad Comsys Alias: %s for Channel: %s",
                     alias.c_str(),
                     reinterpret_cast<const char *>(channel));
            m_pINotify->RawNotify(player, msg);
        }
    }
    else if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Bad Comsys Alias: %s for Channel: %s",
                 alias.c_str(),
                 reinterpret_cast<const char *>(channel));
        m_pINotify->RawNotify(player, msg);
    }
}

void CComsysMod::do_comdisconnectchannel(dbref player, const UTF8 *channel)
{
    struct channel *ch = select_channel(channel);
    if (nullptr == ch)
    {
        return;
    }

    struct comuser *user = select_user(ch, player);
    if (nullptr != user)
    {
        user->bConnected = false;
    }
}

// ---------------------------------------------------------------------------
// Write-through helpers — delegate to engine storage interface.
// ---------------------------------------------------------------------------

void CComsysMod::bump_revision(void)
{
    // Wrap-safe for softcode gen-sync; softcode treats any change as dirty.
    //
    m_revision++;
    if (m_revision < 0)
    {
        m_revision = 1;
    }
}

void CComsysMod::sqlite_wt_channel_user(const UTF8 *channel_name,
    const comuser &user)
{
    if (nullptr == m_pIStorage) return;
    m_pIStorage->SyncChannelUser(channel_name, user.who,
        user.bUserIsOn, user.ComTitleStatus, user.bGagJoinLeave,
        reinterpret_cast<const UTF8 *>(user.title.c_str()));
    bump_revision();
}

void CComsysMod::sqlite_wt_channel(struct channel *ch)
{
    if (nullptr == m_pIStorage) return;
    m_pIStorage->SyncChannel(ch->name, ch->header, ch->type,
        ch->temp1, ch->temp2, ch->charge, ch->charge_who,
        ch->amount_col, ch->num_messages, ch->chan_obj);
    bump_revision();
}

void CComsysMod::sqlite_wt_player_channel(dbref who, const UTF8 *alias,
    const UTF8 *channel_name)
{
    if (nullptr == m_pIStorage) return;
    m_pIStorage->SyncPlayerChannel(who, alias, channel_name);
    bump_revision();
}

void CComsysMod::sqlite_wt_delete_player_channel(dbref who, const UTF8 *alias)
{
    if (nullptr == m_pIStorage) return;
    m_pIStorage->DeletePlayerChannel(who, alias);
    bump_revision();
}

void CComsysMod::sqlite_wt_delete_channel_user(const UTF8 *channel_name,
    dbref who)
{
    if (nullptr == m_pIStorage) return;
    m_pIStorage->DeleteChannelUser(channel_name, who);
    bump_revision();
}

// ---------------------------------------------------------------------------
// Channel access checks.
// ---------------------------------------------------------------------------

// Attribute numbers for channel-object locks (attrs.h A_LOCK/A_LUSE/A_LENTER).
// Duplicated here so the module does not include engine attrs.h.
//
static constexpr int kA_LOCK   = 42;
static constexpr int kA_LENTER = 59;
static constexpr int kA_LUSE   = 62;

// #1084: type-bit OR could_doit on channel object — engine comsys parity.
//
static bool channel_lock_ok(mux_IPermissions *pPerm, dbref player,
    dbref chan_obj, int atr)
{
    if (nullptr == pPerm || NOTHING == chan_obj)
    {
        return false;
    }
    bool ok = false;
    pPerm->CouldDoit(player, chan_obj, atr, &ok);
    return ok;
}

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

    return ((ch->type & access) != 0)
        || channel_lock_ok(m_pIPermissions, player, ch->chan_obj, kA_LUSE);
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

    return ((ch->type & access) != 0)
        || channel_lock_ok(m_pIPermissions, player, ch->chan_obj, kA_LENTER);
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

    return ((ch->type & access) != 0)
        || channel_lock_ok(m_pIPermissions, player, ch->chan_obj, kA_LOCK);
}

// ---------------------------------------------------------------------------
// Policy helpers (#1194 / #1195).
// ---------------------------------------------------------------------------

bool CComsysMod::flag_set(dbref obj, int word, unsigned int bit)
{
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    unsigned int flags = 0;
    if (MUX_FAILED(m_pIObjectInfo->GetFlags(obj, word, &flags)))
    {
        return false;
    }
    return (flags & bit) != 0;
}

bool CComsysMod::is_wizard(dbref obj)
{
    if (nullptr != m_pIPermissions)
    {
        bool b = false;
        m_pIPermissions->IsWizard(obj, &b);
        return b;
    }
    if (nullptr != m_pIObjectInfo)
    {
        bool b = false;
        m_pIObjectInfo->IsWizard(obj, &b);
        return b;
    }
    return false;
}

bool CComsysMod::is_gagged(dbref obj)
{
    return flag_set(obj, MOD_FLAG_WORD2, MOD_FLAG_GAGGED);
}

bool CComsysMod::is_hidden(dbref obj)
{
    // Engine Hidden(x) is (Flags(x) & DARK) — FLAG_WORD1.
    //
    return flag_set(obj, MOD_FLAG_WORD1, MOD_FLAG_DARK);
}

bool CComsysMod::is_guest(dbref obj)
{
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    unsigned int powers = 0;
    if (MUX_FAILED(m_pIObjectInfo->GetPowers(obj, &powers)))
    {
        return false;
    }
    return (powers & MOD_POW_GUEST) != 0;
}

bool CComsysMod::undead_connected(dbref obj)
{
    // Engine UNDEAD(x): Good_obj && (not player || Connected).
    //
    if (nullptr == m_pIObjectInfo)
    {
        return false;
    }
    bool bValid = false;
    m_pIObjectInfo->IsValid(obj, &bValid);
    if (!bValid)
    {
        return false;
    }
    bool bPlayer = false;
    m_pIObjectInfo->IsPlayer(obj, &bPlayer);
    if (!bPlayer)
    {
        return true;
    }
    bool bConnected = false;
    m_pIObjectInfo->IsConnected(obj, &bConnected);
    return bConnected;
}

bool CComsysMod::pay_channel_charge(dbref player, struct channel *ch)
{
    if (nullptr == ch || 0 >= ch->charge)
    {
        return true;
    }
    if (nullptr == m_pIObjectInfo)
    {
        return true;
    }

    const int cost = is_guest(player) ? 0 : ch->charge;
    bool bPaid = false;
    if (MUX_FAILED(m_pIObjectInfo->PayFor(player, cost, &bPaid)) || !bPaid)
    {
        return false;
    }
    ch->amount_col += ch->charge;
    sqlite_wt_channel(ch);
    m_pIObjectInfo->GiveTo(ch->charge_who, ch->charge);
    return true;
}

bool CComsysMod::blocked_by_mogrify(dbref player, struct channel *ch,
    const UTF8 *arg2)
{
    // Engine call_mogrifier(MOGRIFY`BLOCK): non-empty result suppresses send.
    //
    if (  nullptr == ch
       || ch->chan_obj < 0
       || nullptr == m_pIAttributeAccess
       || nullptr == m_pIEvaluator
       || nullptr == arg2)
    {
        return false;
    }

    bool bValid = false;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsValid(ch->chan_obj, &bValid);
    }
    if (!bValid)
    {
        return false;
    }

    UTF8 atext[MOD_LBUF_SIZE];
    atext[0] = '\0';
    size_t nLen = 0;
    // GOD executor so AF_DARK channel-object attrs are readable.
    //
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(1, ch->chan_obj,
        T("MOGRIFY`BLOCK"), atext, sizeof(atext) - 1, &nLen);
    if (MUX_FAILED(mr) || 0 == nLen || '\0' == atext[0])
    {
        return false;
    }

    UTF8 chattype[2];
    chattype[0] = (':' == arg2[0] || ';' == arg2[0]) ? arg2[0] : '"';
    chattype[1] = '\0';

    UTF8 sdrBuf[32];
    snprintf(reinterpret_cast<char *>(sdrBuf), sizeof(sdrBuf), "#%d", player);

    const UTF8 *pName = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetName(player, &pName);
    }
    if (nullptr == pName)
    {
        pName = T("???");
    }

    const UTF8 *block_args[5] = {
        chattype, ch->name, arg2, pName, sdrBuf
    };

    UTF8 result[MOD_LBUF_SIZE];
    result[0] = '\0';
    size_t nResult = 0;
    mr = m_pIEvaluator->EvalWithArgs(ch->chan_obj, ch->chan_obj, player,
        atext, block_args, 5, result, sizeof(result), &nResult);
    if (MUX_FAILED(mr) || 0 == nResult || '\0' == result[0])
    {
        return false;
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(player, result);
    }
    return true;
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

            UTF8 msg[MOD_LBUF_SIZE];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%s %s has left this channel.",
                     reinterpret_cast<const char *>(ch->header),
                     reinterpret_cast<const char *>(pName));
            SendChannelMessage(player, ch, msg, true);
        }
    }

    ch->users.erase(player);
    sqlite_wt_delete_channel_user(channel, player);
}

// ---------------------------------------------------------------------------
// Sort aliases for display/lookup.
// ---------------------------------------------------------------------------

void CComsysMod::sort_com_aliases(comsys_t &c)
{
    std::sort(c.aliases.begin(), c.aliases.end(),
        [](const com_alias &a, const com_alias &b)
        {
            return a.alias < b.alias;
        });
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

    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;
        if (bJoinLeaveMsg && user.bGagJoinLeave)
        {
            continue;
        }
        if (user.bConnected && user.bUserIsOn
            && test_receive_access(user.who, ch))
        {
            m_pINotify->RawNotify(user.who, msg);
        }
    }

    // Engine only evaluates MOGRIFY`* for non-join/leave traffic
    // (comsys.cpp:1701).  Pass that through so join/leave history is not
    // suppressed by a sticky NOBUFFER=1 under the module alone.
    //
    RecordChannelHistory(executor, ch, msg, bJoinLeaveMsg);
}

// ---------------------------------------------------------------------------
// Append a message to the channel's recall buffer (#1564).
//
// The engine's SendChannelMessage writes HISTORY_n at comsys.cpp:1851.  The
// module never did, so do_crecall above read HISTORY_n attributes that
// nothing wrote: recall came back empty on a channel with obvious traffic,
// with cmsgs and cbuffer both reporting healthy values because those come
// from elsewhere.  Silent, and invisible to any test that did not read the
// history back.
//
// The index must be num_messages % logmax computed AFTER the increment in
// the caller, which is exactly what do_crecall's read expects.
//
// Join and leave messages are logged too.  The engine guards its logging
// block only on the channel object being valid, not on bJoinLeaveMsg, and
// its recall output does include "has joined this channel".
//
// GOD (dbref 1) is the executor, matching both the engine -- which writes
// with atr_add(..., GOD, ...) -- and this module's existing use of a GOD
// executor for channel-object attributes.  The speaker is an ordinary
// member who will not usually control the channel object, so passing the
// executor through would fail bCanSetAttr for most messages.
//
// LOG_TIMESTAMPS (#1570): when the attribute is present and non-empty on the
// channel object, wrap the line as the engine does -- "[%s] %s" with
// CLinearTimeAbsolute::ReturnDateString(0).  libmux exports that class, so
// the module can match the engine's format rather than inventing a second
// one with strftime.  @cset/timestamp_logs sets or clears the attribute;
// without this consult it reported success while history stayed plain.
//
// MOGRIFY`NOBUFFER (#1572).
//
// The engine evaluates the hook with three arguments -- channel name, message,
// and the sender as "#<dbref>" -- and skips the recall buffer when the result
// is true by xlate() (comsys.cpp:1727).  The module can do the same: it already
// holds mux_IEvaluator, whose EvalWithArgs exists for exactly this (the
// interface comment cites MOGRIFY`BLOCK, #1194).
//
// Evaluated via EvalWithArgs (same path as blocked_by_mogrify), with xlate-
// style truth on the result so "0" and empty stay off.  A hook added now
// should match the engine rather than invent a second meaning.
//
bool CComsysMod::nobuffer_by_mogrify(dbref executor, struct channel *ch,
    const UTF8 *msg)
{
    if (  nullptr == ch
       || ch->chan_obj < 0
       || nullptr == m_pIAttributeAccess
       || nullptr == m_pIEvaluator
       || nullptr == msg)
    {
        return false;
    }

    bool bValid = false;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsValid(ch->chan_obj, &bValid);
    }
    if (!bValid)
    {
        return false;
    }

    UTF8 expr[MOD_LBUF_SIZE];
    size_t nExpr = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(1, ch->chan_obj,
        T("MOGRIFY`NOBUFFER"), expr, sizeof(expr) - 1, &nExpr);
    if (MUX_FAILED(mr) || 0 == nExpr)
    {
        return false;
    }
    expr[nExpr] = '\0';

    UTF8 sdrBuf[32];
    snprintf(reinterpret_cast<char *>(sdrBuf), sizeof(sdrBuf), "#%d",
             static_cast<int>(executor));
    const UTF8 *args[3] = { ch->name, msg, sdrBuf };

    UTF8 result[MOD_LBUF_SIZE];
    size_t nResult = 0;
    // Match call_mogrifier / blocked_by_mogrify: evaluate as the channel
    // object with the speaker as enactor (comsys.cpp:1342).
    //
    mr = m_pIEvaluator->EvalWithArgs(ch->chan_obj, ch->chan_obj, executor,
        expr, args, 3, result, sizeof(result) - 1, &nResult);
    if (MUX_FAILED(mr) || 0 == nResult)
    {
        return false;
    }
    result[nResult] = '\0';

    // Approximate xlate() (functions.cpp) for the common 0/1/empty results
    // without pulling engine-private xlate into the module.  Leading "#-" is
    // false; bare "#N" is true; "0" is false; other non-empty is true.
    //
    const char *p = reinterpret_cast<const char *>(result);
    while (' ' == *p) p++;
    if ('\0' == *p) return false;
    if ('#' == *p) return ('-' != p[1]);
    if ('0' == p[0] && '\0' == p[1]) return false;
    return true;
}

void CComsysMod::RecordChannelHistory(dbref executor, struct channel *ch,
    const UTF8 *msg, bool bJoinLeaveMsg)
{
    // MOGRIFY`NOBUFFER keeps a line out of the recall buffer (#1572).  The
    // engine honours it; the module did not, so a channel that suppressed
    // history under the engine still recorded it under the module.
    // Join/leave is not evaluated by the engine either (comsys.cpp:1701).
    //
    if (  !bJoinLeaveMsg
       && nobuffer_by_mogrify(executor, ch, msg))
    {
        return;
    }

    if (  nullptr == m_pIAttributeAccess
       || nullptr == m_pIObjectInfo
       || nullptr == ch
       || nullptr == msg)
    {
        return;
    }

    bool bValid = false;
    m_pIObjectInfo->IsValid(ch->chan_obj, &bValid);
    if (!bValid)
    {
        return;
    }

    UTF8 valbuf[64];
    size_t nLen = 0;
    int logmax = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(1, ch->chan_obj,
        T("MAX_LOG"), valbuf, sizeof(valbuf) - 1, &nLen);
    if (MUX_SUCCEEDED(mr) && 0 < nLen)
    {
        valbuf[nLen] = '\0';
        logmax = atoi(reinterpret_cast<const char *>(valbuf));
    }

    if (logmax < 1)
    {
        // Channel does not log; do_crecall reports the same condition.
        //
        return;
    }

    UTF8 histattr[64];
    snprintf(reinterpret_cast<char *>(histattr), sizeof(histattr),
             "HISTORY_%d", ((ch->num_messages % logmax) + logmax) % logmax);

    const UTF8 *payload = msg;
    UTF8 stamped[MOD_LBUF_SIZE];
    UTF8 tsattr[64];
    size_t nTs = 0;
    mr = m_pIAttributeAccess->GetAttribute(1, ch->chan_obj,
        T("LOG_TIMESTAMPS"), tsattr, sizeof(tsattr) - 1, &nTs);
    // Engine uses atr_get_info (presence); @cset clears by writing "".
    // Treat non-empty as on, empty/missing as off.
    //
    if (MUX_SUCCEEDED(mr) && 0 < nTs)
    {
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetLocal();
        // Same shape as comsys.cpp:1857-1865.
        //
        snprintf(reinterpret_cast<char *>(stamped), sizeof(stamped),
                 "[%s] %s",
                 reinterpret_cast<const char *>(ltaNow.ReturnDateString(0)),
                 reinterpret_cast<const char *>(msg));
        payload = stamped;
    }

    // Check the write (#1620).  SetAttribute is permission-checked, and the
    // engine writes these same attributes as GOD with AF_CONST -- which
    // bCanSetAttr denies in every branch, God included.  So on a game that
    // ever ran the built-in comsys, a HISTORY_n slot the engine wrote is
    // refused here forever once the ring wraps onto it.  num_messages is a
    // relational column and keeps counting, so the counter and the history
    // silently diverge: exactly #1564's symptom, from a different cause.
    //
    // This does not make the write land.  It makes a lost message say so
    // instead of vanishing, which is the difference between #1564 taking an
    // investigation and taking a log grep.
    //
    MUX_RESULT mrHist = m_pIAttributeAccess->SetAttribute(1, ch->chan_obj,
        histattr, payload);
    if (MUX_FAILED(mrHist) && nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("COM"), T("HIST"));
        if (fStarted)
        {
            UTF8 logbuf[256];
            snprintf(reinterpret_cast<char *>(logbuf), sizeof(logbuf),
                "Comsys module: channel history write refused (%s on #%d, "
                "result %d); message not recorded.",
                reinterpret_cast<const char *>(histattr),
                static_cast<int>(ch->chan_obj), mrHist);
            m_pILog->log_text(logbuf);
            m_pILog->end_log();
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
        // #1195: enforce membership cap like the engine.
        //
        if (static_cast<int>(ch->users.size()) >= MAX_USERS_PER_CHANNEL)
        {
            if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "Too many people on channel %s already.",
                         reinterpret_cast<const char *>(ch->name));
                m_pINotify->RawNotify(player, msg);
            }
            return;
        }

        comuser cu;
        cu.who = player;
        cu.bUserIsOn = true;
        cu.ComTitleStatus = true;
        cu.bGagJoinLeave = false;
        // #1195: UNDEAD connection, not "always true".
        //
        cu.bConnected = undead_connected(player);

        auto result = ch->users.emplace(player, std::move(cu));
        user = &result.first->second;

        sqlite_wt_channel_user(ch->name, *user);
    }
    else if (!user->bUserIsOn)
    {
        user->bUserIsOn = true;
        sqlite_wt_channel_user(ch->name, *user);
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

    // Engine suppresses join announce for Hidden players.
    //
    if (is_hidden(player))
    {
        return;
    }

    const UTF8 *pName = nullptr;
    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->GetMoniker(player, &pName);
    }
    if (nullptr == pName)
    {
        pName = T("???");
    }

    UTF8 msg[MOD_LBUF_SIZE];
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
        // Engine suppresses leave announce for Hidden players.
        //
        if (!is_hidden(player))
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

            UTF8 msg[MOD_LBUF_SIZE];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%s %s has left this channel.",
                     reinterpret_cast<const char *>(ch->header),
                     reinterpret_cast<const char *>(pName));
            SendChannelMessage(player, ch, msg, true);
        }

        user->bUserIsOn = false;
        sqlite_wt_channel_user(ch->name, *user);
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

    // #1195: mirror engine do_comwho — players vs objects; filter Hidden
    // unless Wizard_Who / See_Hidden.
    //
    bool bSeeHidden = false;
    bool bWizardWho = false;
    m_pIObjectInfo->SeeHidden(player, &bSeeHidden);
    m_pIObjectInfo->WizardWho(player, &bWizardWho);
    const bool bMaySeeHidden = bSeeHidden || bWizardWho;

    m_pINotify->RawNotify(player, T("-- Players --"));

    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;
        bool bPlayer = false;
        m_pIObjectInfo->IsPlayer(user.who, &bPlayer);
        if (!bPlayer)
        {
            continue;
        }

        bool bConnected = false;
        m_pIObjectInfo->IsConnected(user.who, &bConnected);
        const bool bHidden = is_hidden(user.who);

        if (bConnected && (!bHidden || bMaySeeHidden))
        {
            if (user.bUserIsOn)
            {
                const UTF8 *pName = nullptr;
                m_pIObjectInfo->GetMoniker(user.who, &pName);
                if (nullptr != pName)
                {
                    m_pINotify->RawNotify(player, pName);
                }
            }
        }
        else if (!bHidden)
        {
            // Stale presence — disconnect from this channel's view.
            //
            do_comdisconnectchannel(user.who, ch->name);
        }
    }

    m_pINotify->RawNotify(player, T("-- Objects --"));

    for (auto &kv : ch->users)
    {
        const comuser &user = kv.second;
        bool bPlayer = false;
        m_pIObjectInfo->IsPlayer(user.who, &bPlayer);
        if (bPlayer)
        {
            continue;
        }

        bool bGoing = false;
        m_pIObjectInfo->IsGoing(user.who, &bGoing);
        if (bGoing)
        {
            continue;
        }
        if (user.bUserIsOn)
        {
            const UTF8 *pName = nullptr;
            m_pIObjectInfo->GetMoniker(user.who, &pName);
            if (nullptr != pName)
            {
                m_pINotify->RawNotify(player, pName);
            }
        }
    }

    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "-- %s --", reinterpret_cast<const char *>(ch->name));
    m_pINotify->RawNotify(player, msg);
}

// ---------------------------------------------------------------------------
// Recall channel message history from channel object attributes.
// ---------------------------------------------------------------------------

void CComsysMod::do_comlast(dbref player, struct channel *ch, int arg)
{
    if (nullptr == m_pINotify || nullptr == m_pIAttributeAccess)
    {
        return;
    }

    // Validate the channel object.
    //
    bool bValid = false;
    if (NOTHING != ch->chan_obj && nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->IsValid(ch->chan_obj, &bValid);
    }
    if (!bValid)
    {
        m_pINotify->RawNotify(player,
            T("Channel does not have an object."));
        return;
    }

    const dbref obj = ch->chan_obj;

    // Read MAX_LOG attribute to determine logging depth.
    //
    UTF8 valbuf[64];
    size_t nLen = 0;
    int logmax = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(player, obj,
        T("MAX_LOG"), valbuf, sizeof(valbuf) - 1, &nLen);
    if (MUX_SUCCEEDED(mr) && 0 < nLen)
    {
        valbuf[nLen] = '\0';
        logmax = atoi(reinterpret_cast<const char *>(valbuf));
    }

    if (logmax < 1)
    {
        m_pINotify->RawNotify(player, T("Channel does not log."));
        return;
    }

    if (arg < 1)
    {
        arg = 1;
    }
    if (arg > logmax)
    {
        arg = logmax;
    }

    int histnum = ch->num_messages - arg;

    UTF8 msg[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "%s -- Begin Comsys Recall --",
             reinterpret_cast<const char *>(ch->header));
    m_pINotify->RawNotify(player, msg);

    for (int count = 0; count < arg; count++)
    {
        histnum++;
        UTF8 attrname[64];
        snprintf(reinterpret_cast<char *>(attrname), sizeof(attrname),
                 "HISTORY_%d", ((histnum % logmax) + logmax) % logmax);

        UTF8 message[MOD_LBUF_SIZE];
        size_t msgLen = 0;
        mr = m_pIAttributeAccess->GetAttribute(player, obj,
            attrname, message, sizeof(message) - 1, &msgLen);
        if (MUX_SUCCEEDED(mr) && 0 < msgLen)
        {
            message[msgLen] = '\0';
            m_pINotify->RawNotify(player, message);
        }
    }

    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "%s -- End Comsys Recall --",
             reinterpret_cast<const char *>(ch->header));
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

    // Handle "last [N]" — channel history recall.
    //
    if (0 == strncmp(reinterpret_cast<const char *>(arg2), "last", 4)
        && ('\0' == arg2[4]
            || (' ' == arg2[4]
                && '\0' != arg2[5])))
    {
        int nRecall = 10; // DFLT_RECALL_REQUEST
        if (' ' == arg2[4])
        {
            nRecall = atoi(reinterpret_cast<const char *>(arg2 + 5));
        }
        do_comlast(player, ch, nRecall);
        return;
    }

    // #1194: Gagged (non-wizard) may not speak on channels.
    //
    if (is_gagged(player) && !is_wizard(player))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("GAGGED players may not speak on channels."));
        }
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

    // #1194: channel charge (payfor + giveto to charge_who).
    //
    if (!pay_channel_charge(player, ch))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(player,
                T("You don’t have enough coins."));
        }
        return;
    }

    // #1194: MOGRIFY`BLOCK on the channel object.
    //
    if (blocked_by_mogrify(player, ch, arg2))
    {
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

    UTF8 msg[MOD_LBUF_SIZE];
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
        // Say: "<header> <name> says, “<text>”"
        //
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s says, “%s”",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pMoniker),
                 pPose);
    }

    SendChannelMessage(player, ch, msg, false);
}

// ---------------------------------------------------------------------------
// mux_IComsysControl implementation.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::Initialize(mux_IComsysStorage *pStorage)
{
    if (nullptr == pStorage)
    {
        return MUX_E_INVALIDARG;
    }

    m_pIStorage = pStorage;
    m_pIStorage->AddRef();

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
            m_pILog->log_number(static_cast<int>(m_channels.size()));
            m_pILog->log_text(T(" channels from SQLite."));
            m_pILog->end_log();
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerConnect(dbref player)
{
    auto it = m_comsys.find(player);
    if (it == m_comsys.end())
    {
        return MUX_S_OK;
    }

    comsys_t &c = it->second;

    // Track which channels we have already connected to (avoid duplicates).
    //
    std::vector<std::string> seen;

    for (const auto &ca : c.aliases)
    {
        bool bFound = false;
        for (const auto &s : seen)
        {
            if (s == ca.channel)
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            seen.push_back(ca.channel);
            do_comconnectchannel(player,
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()),
                ca.alias);
            do_comconnectraw_notify(player,
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerDisconnect(dbref player)
{
    auto it = m_comsys.find(player);
    if (it == m_comsys.end())
    {
        return MUX_S_OK;
    }

    comsys_t &c = it->second;

    std::vector<std::string> seen;

    for (const auto &ca : c.aliases)
    {
        bool bFound = false;
        for (const auto &s : seen)
        {
            if (s == ca.channel)
            {
                bFound = true;
                break;
            }
        }

        if (!bFound)
        {
            seen.push_back(ca.channel);
            do_comdisconnectchannel(player,
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
            do_comdisconnectraw_notify(player,
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CComsysMod::PlayerNuke(dbref player)
{
    if (player < 0)
    {
        return MUX_E_INVALIDARG;
    }

    // Mirror engine ReleaseAllResources comsys side (#1193):
    // disconnect presence, clear aliases/membership, destroy owned
    // channels, drop per-player comsys row, and purge SQLite player_channels.
    //

    // 1. Quiet disconnect of channel presence.
    //
    PlayerDisconnect(player);

    // 2. Remove all aliases and channel membership for this player.
    //
    auto itCom = m_comsys.find(player);
    if (itCom != m_comsys.end())
    {
        comsys_t &c = itCom->second;
        std::vector<com_alias> aliases = c.aliases;
        for (const auto &ca : aliases)
        {
            do_delcomchannel(player,
                reinterpret_cast<const UTF8 *>(ca.channel.c_str()), true);
            sqlite_wt_delete_player_channel(player,
                reinterpret_cast<const UTF8 *>(ca.alias.c_str()));
        }
        c.aliases.clear();
        m_comsys.erase(itCom);
    }

    // 3. Strip residual membership (joined without a surviving alias).
    //
    for (auto &kv : m_channels)
    {
        struct channel *ch = kv.second.get();
        if (nullptr == ch)
        {
            continue;
        }
        if (0 != ch->users.erase(player))
        {
            sqlite_wt_delete_channel_user(ch->name, player);
        }
    }

    // 4. Destroy channels owned by this player.  SQLite ON DELETE CASCADE
    // clears channel_users / player_channels for those channel names; also
    // drop other players' in-memory aliases that pointed at them.
    //
    std::vector<std::string> destroyedNames;
    for (auto it = m_channels.begin(); it != m_channels.end(); )
    {
        if (player == it->second->charge_who)
        {
            destroyedNames.emplace_back(
                reinterpret_cast<const char *>(it->second->name));
            if (nullptr != m_pIStorage)
            {
                m_pIStorage->DeleteChannel(it->second->name);
                bump_revision();
            }
            it = m_channels.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (!destroyedNames.empty())
    {
        for (auto &kv : m_comsys)
        {
            comsys_t &c = kv.second;
            for (auto ait = c.aliases.begin(); ait != c.aliases.end(); )
            {
                bool bHit = false;
                for (const auto &nm : destroyedNames)
                {
                    if (ait->channel == nm)
                    {
                        bHit = true;
                        break;
                    }
                }
                if (bHit)
                {
                    sqlite_wt_delete_player_channel(c.who,
                        reinterpret_cast<const UTF8 *>(ait->alias.c_str()));
                    ait = c.aliases.erase(ait);
                }
                else
                {
                    ++ait;
                }
            }
        }
    }

    // 5. Belt-and-suspenders: remaining player_channels rows for this who.
    //
    if (nullptr != m_pIStorage)
    {
        m_pIStorage->DeleteAllPlayerChannels(player);
    }

    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// allcom — send command to all channels.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::AllCom(dbref executor, const UTF8 *pAction)
{
    if (nullptr == pAction)
    {
        return MUX_E_INVALIDARG;
    }

    const char *a = reinterpret_cast<const char *>(pAction);
    if (0 != strcmp(a, "who") && 0 != strcmp(a, "on") && 0 != strcmp(a, "off"))
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Only options available are: on, off and who."));
        }
        return MUX_E_INVALIDARG;
    }

    auto it = m_comsys.find(executor);
    if (it == m_comsys.end())
    {
        return MUX_S_OK;
    }

    comsys_t &c = it->second;

    for (const auto &ca : c.aliases)
    {
        do_processcom(executor,
            reinterpret_cast<const UTF8 *>(ca.channel.c_str()),
            const_cast<UTF8 *>(pAction));
        if (0 == strcmp(a, "who") && nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor, T(""));
        }
    }
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// comlist — list player's channel aliases.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::ComList(dbref executor, const UTF8 *pPattern)
{
    if (nullptr == m_pINotify)
    {
        return MUX_E_FAIL;
    }

    m_pINotify->RawNotify(executor,
        T("Alias           Channel            Status   Title"));

    auto it = m_comsys.find(executor);
    if (it == m_comsys.end())
    {
        m_pINotify->RawNotify(executor, T("-- End of comlist --"));
        return MUX_S_OK;
    }

    comsys_t &c = it->second;
    bool bWild = (nullptr != pPattern && '\0' != *pPattern);

    for (const auto &ca : c.aliases)
    {
        struct channel *ch = select_channel(
            reinterpret_cast<const UTF8 *>(ca.channel.c_str()));
        struct comuser *user = (nullptr != ch)
            ? select_user(ch, executor) : nullptr;

        if (nullptr != user)
        {
            if (bWild && std::string::npos == ca.channel.find(
                    reinterpret_cast<const char *>(pPattern)))
            {
                continue;
            }

            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "%-15.15s %-18.18s %s %s%s %s",
                     ca.alias.c_str(),
                     ca.channel.c_str(),
                     user->bUserIsOn ? "on " : "off",
                     user->ComTitleStatus ? "con " : "coff",
                     user->bGagJoinLeave ? " gag" : "",
                     user->title.c_str());
            m_pINotify->RawNotify(executor, msg);
        }
        else
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Bad Comsys Alias: %s for Channel: %s",
                     ca.alias.c_str(),
                     ca.channel.c_str());
            m_pINotify->RawNotify(executor, msg);
        }
    }

    m_pINotify->RawNotify(executor, T("-- End of comlist --"));
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// comtitle — set/get comtitle for a channel alias.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::ComTitle(dbref executor, const UTF8 *pAlias,
    const UTF8 *pTitle, int key)
{
    if (nullptr == pAlias || '\0' == *pAlias)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Need an alias to do comtitle."));
        }
        return MUX_E_INVALIDARG;
    }

    const UTF8 *chName = get_channel_from_alias(executor, pAlias);
    if ('\0' == chName[0])
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor, T("Unknown alias."));
        }
        return MUX_E_NOTFOUND;
    }

    struct channel *ch = select_channel(chName);
    if (nullptr == ch)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor,
                T("Illegal comsys alias, please delete."));
        }
        return MUX_E_NOTFOUND;
    }

    struct comuser *user = select_user(ch, executor);
    if (nullptr == user)
    {
        return MUX_E_NOTFOUND;
    }

    UTF8 msg[256];
    switch (key)
    {
    case COMTITLE_OFF:
        if (0 == (ch->type & CHANNEL_SPOOF))
        {
            user->ComTitleStatus = false;
            sqlite_wt_channel_user(ch->name, *user);
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Comtitles are now off for channel %s",
                     reinterpret_cast<const char *>(ch->name));
        }
        else
        {
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "You can not turn off comtitles on that channel.");
        }
        break;

    case COMTITLE_ON:
        user->ComTitleStatus = true;
        sqlite_wt_channel_user(ch->name, *user);
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Comtitles are now on for channel %s",
                 reinterpret_cast<const char *>(ch->name));
        break;

    case COMTITLE_GAG:
        user->bGagJoinLeave = true;
        sqlite_wt_channel_user(ch->name, *user);
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Join/leave messages are now gagged for channel %s",
                 reinterpret_cast<const char *>(ch->name));
        break;

    case COMTITLE_UNGAG:
        user->bGagJoinLeave = false;
        sqlite_wt_channel_user(ch->name, *user);
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Join/leave messages are now ungagged for channel %s",
                 reinterpret_cast<const char *>(ch->name));
        break;

    default:
        // Set title.
        //
        if (nullptr != pTitle && '\0' != *pTitle)
        {
            user->title.assign(reinterpret_cast<const char *>(pTitle),
                0, MAX_TITLE_LEN);
        }
        else
        {
            user->title.clear();
        }
        sqlite_wt_channel_user(ch->name, *user);
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Title set to ‘%s’ on channel %s.",
                 user->title.c_str(),
                 reinterpret_cast<const char *>(ch->name));
        break;
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->RawNotify(executor, msg);
    }
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// @clist — list channels.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::ChanList(dbref executor, const UTF8 *pPattern,
    int key)
{
    if (nullptr == m_pINotify)
    {
        return MUX_E_FAIL;
    }

    if (key & CLIST_HEADERS)
    {
        m_pINotify->RawNotify(executor,
            T("*** Channel       Owner           Header"));
    }
    else
    {
        m_pINotify->RawNotify(executor,
            T("*** Channel       Owner           Description"));
    }

    bool bWild = (nullptr != pPattern && '\0' != *pPattern);

    for (auto &kv : m_channels)
    {
        struct channel *ch = kv.second.get();

        // Simple wildcard filter.
        //
        if (bWild && nullptr == strstr(
                reinterpret_cast<const char *>(ch->name),
                reinterpret_cast<const char *>(pPattern)))
        {
            continue;
        }

        // Visibility check: public or owner or Comm_All.
        //
        bool bVisible = ((ch->type & CHANNEL_PUBLIC) != 0);
        if (!bVisible && nullptr != m_pIPermissions)
        {
            bool bCommAll = false;
            m_pIPermissions->HasCommAll(executor, &bCommAll);
            bVisible = bCommAll;
        }
        if (!bVisible)
        {
            bool bControls = false;
            if (nullptr != m_pIPermissions)
            {
                m_pIPermissions->HasControl(executor, ch->charge_who,
                    &bControls);
            }
            bVisible = bControls;
        }

        if (!bVisible)
        {
            continue;
        }

        const UTF8 *pOwnerName = nullptr;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->GetMoniker(ch->charge_who, &pOwnerName);
        }
        if (nullptr == pOwnerName)
        {
            pOwnerName = T("???");
        }

        const char *pDesc = (key & CLIST_HEADERS)
            ? reinterpret_cast<const char *>(ch->header)
            : "No description.";

        UTF8 line[256];
        snprintf(reinterpret_cast<char *>(line), sizeof(line),
                 "%c%c%c %-13.13s %-15.15s %s",
                 (ch->type & CHANNEL_PUBLIC) ? 'P' : '-',
                 (ch->type & CHANNEL_LOUD) ? 'L' : '-',
                 (ch->type & CHANNEL_SPOOF) ? 'S' : '-',
                 reinterpret_cast<const char *>(ch->name),
                 reinterpret_cast<const char *>(pOwnerName),
                 pDesc);
        m_pINotify->RawNotify(executor, line);
    }

    m_pINotify->RawNotify(executor,
        T("-- End of list of Channels --"));
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// @cwho — who is on a channel.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::ChanWho(dbref executor, const UTF8 *pArg)
{
    if (nullptr == m_pINotify)
    {
        return MUX_E_FAIL;
    }

    if (nullptr == pArg || '\0' == *pArg)
    {
        m_pINotify->RawNotify(executor, T("You must specify a channel."));
        return MUX_E_INVALIDARG;
    }

    // Parse "channel/all" format.
    //
    UTF8 chanName[MAX_CHANNEL_LEN + 1];
    size_t i = 0;
    while ('\0' != pArg[i] && '/' != pArg[i] && i < MAX_CHANNEL_LEN)
    {
        chanName[i] = pArg[i];
        i++;
    }
    chanName[i] = '\0';

    bool bAll = ('/' == pArg[i] && 'a' == pArg[i + 1]);

    struct channel *ch = select_channel(chanName);
    if (nullptr == ch)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Unknown channel %s.",
                 reinterpret_cast<const char *>(chanName));
        m_pINotify->RawNotify(executor, msg);
        return MUX_E_NOTFOUND;
    }

    // Permission check.
    //
    bool bAllowed = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->HasCommAll(executor, &bAllowed);
        if (!bAllowed)
        {
            m_pIPermissions->HasControl(executor, ch->charge_who, &bAllowed);
        }
    }
    if (!bAllowed)
    {
        m_pINotify->RawNotify(executor, T("Permission denied."));
        return MUX_E_PERMISSION;
    }

    UTF8 msg[256];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "-- %s --", reinterpret_cast<const char *>(ch->name));
    m_pINotify->RawNotify(executor, msg);

    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "%-29.29s %-6.6s %-6.6s", "Name", "Status", "Player");
    m_pINotify->RawNotify(executor, msg);

    for (auto &kv : ch->users)
    {
        comuser &user = kv.second;

        // If not showing all, only show connected users.
        //
        if (!bAll)
        {
            bool bConnected = false;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsConnected(user.who, &bConnected);
            }
            if (!bConnected)
            {
                continue;
            }
        }

        const UTF8 *pName = nullptr;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->GetName(user.who, &pName);
        }
        if (nullptr == pName)
        {
            pName = T("???");
        }

        bool bPlayer = false;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(user.who, &bPlayer);
        }

        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%-29.29s %-6.6s %-6.6s",
                 reinterpret_cast<const char *>(pName),
                 user.bUserIsOn ? "on " : "off",
                 bPlayer ? "yes" : "no ");
        m_pINotify->RawNotify(executor, msg);
    }

    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "-- %s --", reinterpret_cast<const char *>(ch->name));
    m_pINotify->RawNotify(executor, msg);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// @cemit — emit to a channel.
// ---------------------------------------------------------------------------

MUX_RESULT CComsysMod::CEmit(dbref executor, const UTF8 *pChannel,
    const UTF8 *pText, int key)
{
    if (nullptr == pChannel || nullptr == pText)
    {
        return MUX_E_INVALIDARG;
    }

    struct channel *ch = select_channel(pChannel);
    if (nullptr == ch)
    {
        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Channel %s does not exist.",
                     reinterpret_cast<const char *>(pChannel));
            m_pINotify->RawNotify(executor, msg);
        }
        return MUX_E_NOTFOUND;
    }

    // Permission check.
    //
    bool bAllowed = false;
    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->HasCommAll(executor, &bAllowed);
        if (!bAllowed)
        {
            m_pIPermissions->HasControl(executor, ch->charge_who, &bAllowed);
        }
    }
    if (!bAllowed)
    {
        if (nullptr != m_pINotify)
        {
            m_pINotify->RawNotify(executor, T("Permission denied."));
        }
        return MUX_E_PERMISSION;
    }

    UTF8 msg[MOD_LBUF_SIZE];
    if (key == CEMIT_NOHEADER)
    {
        strncpy(reinterpret_cast<char *>(msg),
                reinterpret_cast<const char *>(pText), sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
    }
    else
    {
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s",
                 reinterpret_cast<const char *>(ch->header),
                 reinterpret_cast<const char *>(pText));
    }

    SendChannelMessage(executor, ch, msg, false);
    return MUX_S_OK;
}

MUX_RESULT CComsysMod::CSet(dbref executor, const UTF8 *pChannel,
    const UTF8 *pValue, int key)
{
    if (CSET_LIST == key)
    {
        // Redirect to ChanList with CLIST_FULL.
        //
        return ChanList(executor, nullptr, CLIST_FULL);
    }

    struct channel *ch = select_channel(pChannel);
    if (nullptr == ch)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "@cset: Channel %s does not exist.",
                 reinterpret_cast<const char *>(pChannel));
        m_pINotify->Notify(executor, msg);
        return MUX_S_OK;
    }

    // Permission check: must control channel owner or have Comm_All.
    //
    bool bControls = false;
    m_pIPermissions->HasControl(executor, ch->charge_who, &bControls);
    bool bCommAll = false;
    m_pIPermissions->HasCommAll(executor, &bCommAll);
    if (!bControls && !bCommAll)
    {
        m_pINotify->Notify(executor,
            reinterpret_cast<const UTF8 *>("Permission denied."));
        return MUX_S_OK;
    }

    const UTF8 *msg = nullptr;
    UTF8 msgbuf[MOD_LBUF_SIZE];

    switch (key)
    {
    case CSET_PUBLIC:
        ch->type |= CHANNEL_PUBLIC;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s placed on the public listings.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_PRIVATE:
        ch->type &= ~CHANNEL_PUBLIC;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s taken off the public listings.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_LOUD:
        ch->type |= CHANNEL_LOUD;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s now sends connect/disconnect msgs.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_QUIET:
        ch->type &= ~CHANNEL_LOUD;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s connect/disconnect msgs muted.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_SPOOF:
        ch->type |= CHANNEL_SPOOF;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s set spoofable.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_NOSPOOF:
        ch->type &= ~CHANNEL_SPOOF;
        snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                 "@cset: Channel %s set unspoofable.",
                 reinterpret_cast<const char *>(pChannel));
        msg = msgbuf;
        break;

    case CSET_OBJECT:
        {
            dbref thing = NOTHING;
            if (nullptr != pValue && '\0' != pValue[0])
            {
                m_pIObjectInfo->MatchThing(executor, pValue, &thing);
            }
            bool bValid = false;
            if (NOTHING != thing)
            {
                m_pIObjectInfo->IsValid(thing, &bValid);
            }
            if (NOTHING == thing)
            {
                ch->chan_obj = NOTHING;
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "Channel %s is now disassociated from any channel object.",
                         reinterpret_cast<const char *>(ch->name));
            }
            else if (bValid)
            {
                ch->chan_obj = thing;
                const UTF8 *pName = nullptr;
                m_pIObjectInfo->GetName(thing, &pName);
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "Channel %s is now using %s(#%d) as channel object.",
                         reinterpret_cast<const char *>(ch->name),
                         pName ? reinterpret_cast<const char *>(pName) : "???",
                         thing);
            }
            else
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "%d is not a valid channel object.", thing);
            }
            msg = msgbuf;
        }
        break;

    case CSET_HEADER:
        {
            if (nullptr == pValue || '\0' == pValue[0])
            {
                // Reset to default bold [ChannelName] header.
                // COLOR_INTENSE = \xEF\x94\x81, COLOR_RESET = \xEF\x94\x80
                //
                snprintf(reinterpret_cast<char *>(ch->header),
                         sizeof(ch->header),
                         "\xEF\x94\x81[%s]\xEF\x94\x80",
                         reinterpret_cast<const char *>(ch->name));
            }
            else
            {
                strncpy(reinterpret_cast<char *>(ch->header),
                        reinterpret_cast<const char *>(pValue),
                        MAX_HEADER_LEN);
                ch->header[MAX_HEADER_LEN] = '\0';
            }
            msg = reinterpret_cast<const UTF8 *>("Set.");
        }
        break;

    case CSET_LOG:
        {
            if (nullptr == pValue || '\0' == pValue[0])
            {
                msg = reinterpret_cast<const UTF8 *>(
                    "@cset: Maximum history must be a number less than "
                    "or equal to 200.");
                break;
            }
            int value = atoi(reinterpret_cast<const char *>(pValue));
            if (value < 0 || value > 200)
            {
                msg = reinterpret_cast<const UTF8 *>(
                    "@cset: Maximum history must be a number less than "
                    "or equal to 200.");
                break;
            }

            // Set when shrinking the ring leaves entries we could not clear
            // (#1620); reported with the result rather than swallowed.
            //
            bool bStale = false;

            bool bObjValid = false;
            if (NOTHING != ch->chan_obj && nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsValid(ch->chan_obj, &bObjValid);
            }
            if (!bObjValid)
            {
                msg = reinterpret_cast<const UTF8 *>(
                    "@cset: Channel has no object. Use @cset/object first.");
                break;
            }

            // Read old MAX_LOG value.
            //
            UTF8 oldbuf[64];
            size_t nOldLen = 0;
            int oldnum = 0;
            MUX_RESULT mr2 = m_pIAttributeAccess->GetAttribute(
                executor, ch->chan_obj, T("MAX_LOG"),
                oldbuf, sizeof(oldbuf) - 1, &nOldLen);
            if (MUX_SUCCEEDED(mr2) && 0 < nOldLen)
            {
                oldbuf[nOldLen] = '\0';
                oldnum = atoi(reinterpret_cast<const char *>(oldbuf));
            }

            // If reducing, clear old HISTORY_N attributes.
            //
            if (value < oldnum)
            {
                for (int count = 0; count <= oldnum; count++)
                {
                    UTF8 histattr[64];
                    snprintf(reinterpret_cast<char *>(histattr),
                             sizeof(histattr), "HISTORY_%d", count);
                    if (MUX_FAILED(m_pIAttributeAccess->SetAttribute(
                            executor, ch->chan_obj, histattr, T(""))))
                    {
                        // Engine-written slots carry AF_CONST and refuse
                        // (#1620).  Shrinking the ring then leaves stale
                        // history above the new maximum.
                        //
                        bStale = true;
                    }
                }
            }

            // Set MAX_LOG.
            //
            UTF8 vbuf[32];
            snprintf(reinterpret_cast<char *>(vbuf), sizeof(vbuf),
                     "%d", value);
            if (MUX_FAILED(m_pIAttributeAccess->SetAttribute(
                    executor, ch->chan_obj, T("MAX_LOG"), vbuf)))
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Failed.  Could not set the maximum for %s "
                         "(the attribute is not writable here).",
                         reinterpret_cast<const char *>(pChannel));
            }
            else if (bStale)
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Channel %s maximum history set, but older "
                         "entries could not be cleared.",
                         reinterpret_cast<const char *>(pChannel));
            }
            else
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Channel %s maximum history set.",
                         reinterpret_cast<const char *>(pChannel));
            }
            msg = msgbuf;
        }
        break;

    case CSET_LOG_TIME:
        {
            if (nullptr == pValue || '\0' == pValue[0])
            {
                msg = reinterpret_cast<const UTF8 *>("@cset: Failed.");
                break;
            }
            int value = atoi(reinterpret_cast<const char *>(pValue));
            if (value != 0 && value != 1)
            {
                msg = reinterpret_cast<const UTF8 *>("@cset: Failed.");
                break;
            }

            bool bObjValid = false;
            if (NOTHING != ch->chan_obj && nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsValid(ch->chan_obj, &bObjValid);
            }
            if (!bObjValid)
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Failed.  Is logging enabled for %s?",
                         reinterpret_cast<const char *>(pChannel));
                msg = msgbuf;
                break;
            }

            // Verify logging is enabled (MAX_LOG exists).
            //
            UTF8 logbuf[64];
            size_t nLogLen = 0;
            MUX_RESULT mr2 = m_pIAttributeAccess->GetAttribute(
                executor, ch->chan_obj, T("MAX_LOG"),
                logbuf, sizeof(logbuf) - 1, &nLogLen);
            if (MUX_FAILED(mr2) || 0 == nLogLen)
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Failed.  Is logging enabled for %s?",
                         reinterpret_cast<const char *>(pChannel));
                msg = msgbuf;
                break;
            }

            // Set or clear LOG_TIMESTAMPS.
            //
            // #1585: this write is refused when the engine set the attribute
            // (GOD + AF_CONST, which bCanSetAttr denies in every branch).
            // Reporting "set." after a refusal told the player the opposite
            // of what happened, and is how the divergence stayed hidden.
            //
            MUX_RESULT mrTs = m_pIAttributeAccess->SetAttribute(
                executor, ch->chan_obj, T("LOG_TIMESTAMPS"),
                value ? T("1") : T(""));
            if (MUX_FAILED(mrTs))
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Failed.  Timestamp logging for %s could not "
                         "be changed (the attribute is not writable here).",
                         reinterpret_cast<const char *>(pChannel));
            }
            else
            {
                snprintf(reinterpret_cast<char *>(msgbuf), sizeof(msgbuf),
                         "@cset: Channel %s timestamp logging set.",
                         reinterpret_cast<const char *>(pChannel));
            }
            msg = msgbuf;
        }
        break;
    }

    sqlite_wt_channel(ch);
    if (nullptr != msg)
    {
        m_pINotify->Notify(executor, msg);
    }
    return MUX_S_OK;
}

MUX_RESULT CComsysMod::EditChannel(dbref executor, const UTF8 *pChannel,
    const UTF8 *pValue, int flag)
{
    struct channel *ch = select_channel(pChannel);
    if (nullptr == ch)
    {
        UTF8 msg[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Unknown channel %s.",
                 reinterpret_cast<const char *>(pChannel));
        m_pINotify->Notify(executor, msg);
        return MUX_S_OK;
    }

    // Permission check.
    //
    bool bControls = false;
    m_pIPermissions->HasControl(executor, ch->charge_who, &bControls);
    bool bCommAll = false;
    m_pIPermissions->HasCommAll(executor, &bCommAll);
    if (!bControls && !bCommAll)
    {
        m_pINotify->Notify(executor,
            reinterpret_cast<const UTF8 *>("Permission denied."));
        return MUX_S_OK;
    }

    bool add_remove = true;
    const UTF8 *s = pValue;
    if (nullptr != s && '!' == *s)
    {
        add_remove = false;
        s++;
    }

    switch (flag)
    {
    case EDIT_CHANNEL_CCHOWN:
        {
            dbref who = NOTHING;
            if (nullptr != pValue)
            {
                m_pIObjectInfo->MatchThing(executor, pValue, &who);
            }
            bool bValid = false;
            if (NOTHING != who)
            {
                m_pIObjectInfo->IsValid(who, &bValid);
            }
            if (bValid)
            {
                ch->charge_who = who;
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>("Set."));
            }
            else
            {
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>("Invalid player."));
            }
        }
        break;

    case EDIT_CHANNEL_CCHARGE:
        {
            int c_charge = 0;
            if (nullptr != pValue)
            {
                c_charge = atoi(reinterpret_cast<const char *>(pValue));
            }
            if (0 <= c_charge && c_charge <= MAX_COST)
            {
                ch->charge = c_charge;
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>("Set."));
            }
            else
            {
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>(
                        "That is not a reasonable cost."));
            }
        }
        break;

    case EDIT_CHANNEL_CPFLAGS:
        {
            int access = 0;
            if (nullptr != s)
            {
                if (strcmp(reinterpret_cast<const char *>(s), "join") == 0)
                {
                    access = CHANNEL_PLAYER_JOIN;
                }
                else if (strcmp(reinterpret_cast<const char *>(s),
                               "receive") == 0)
                {
                    access = CHANNEL_PLAYER_RECEIVE;
                }
                else if (strcmp(reinterpret_cast<const char *>(s),
                               "transmit") == 0)
                {
                    access = CHANNEL_PLAYER_TRANSMIT;
                }
            }

            if (access)
            {
                if (add_remove)
                {
                    ch->type |= access;
                    m_pINotify->Notify(executor,
                        reinterpret_cast<const UTF8 *>("@cpflags: Set."));
                }
                else
                {
                    ch->type &= ~access;
                    m_pINotify->Notify(executor,
                        reinterpret_cast<const UTF8 *>("@cpflags: Cleared."));
                }
            }
            else
            {
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>("@cpflags: Unknown Flag."));
            }
        }
        break;

    case EDIT_CHANNEL_COFLAGS:
        {
            int access = 0;
            if (nullptr != s)
            {
                if (strcmp(reinterpret_cast<const char *>(s), "join") == 0)
                {
                    access = CHANNEL_OBJECT_JOIN;
                }
                else if (strcmp(reinterpret_cast<const char *>(s),
                               "receive") == 0)
                {
                    access = CHANNEL_OBJECT_RECEIVE;
                }
                else if (strcmp(reinterpret_cast<const char *>(s),
                               "transmit") == 0)
                {
                    access = CHANNEL_OBJECT_TRANSMIT;
                }
            }

            if (access)
            {
                if (add_remove)
                {
                    ch->type |= access;
                    m_pINotify->Notify(executor,
                        reinterpret_cast<const UTF8 *>("@coflags: Set."));
                }
                else
                {
                    ch->type &= ~access;
                    m_pINotify->Notify(executor,
                        reinterpret_cast<const UTF8 *>("@coflags: Cleared."));
                }
            }
            else
            {
                m_pINotify->Notify(executor,
                    reinterpret_cast<const UTF8 *>("@coflags: Unknown Flag."));
            }
        }
        break;
    }

    sqlite_wt_channel(ch);
    return MUX_S_OK;
}

MUX_RESULT CComsysMod::CBoot(dbref executor, const UTF8 *pChannel,
    const UTF8 *pVictim, int key)
{
    struct channel *ch = select_channel(pChannel);
    if (nullptr == ch)
    {
        m_pINotify->Notify(executor,
            reinterpret_cast<const UTF8 *>("@cboot: Unknown channel."));
        return MUX_S_OK;
    }

    struct comuser *user = select_user(ch, executor);
    if (nullptr == user)
    {
        m_pINotify->Notify(executor,
            reinterpret_cast<const UTF8 *>(
                "@cboot: You are not on that channel."));
        return MUX_S_OK;
    }

    // Permission check.
    //
    bool bControls = false;
    m_pIPermissions->HasControl(executor, ch->charge_who, &bControls);
    bool bCommAll = false;
    m_pIPermissions->HasCommAll(executor, &bCommAll);
    if (!bControls && !bCommAll)
    {
        m_pINotify->Notify(executor,
            reinterpret_cast<const UTF8 *>(
                "@cboot: You can’t do that!"));
        return MUX_S_OK;
    }

    // Resolve victim name to dbref.
    //
    dbref thing = NOTHING;
    if (nullptr != pVictim)
    {
        m_pIObjectInfo->MatchThing(executor, pVictim, &thing);
    }
    bool bValid = false;
    if (NOTHING != thing)
    {
        m_pIObjectInfo->IsValid(thing, &bValid);
    }
    if (!bValid)
    {
        return MUX_S_OK;
    }

    struct comuser *vu = select_user(ch, thing);
    if (nullptr == vu)
    {
        const UTF8 *pMoniker = nullptr;
        m_pIObjectInfo->GetMoniker(thing, &pMoniker);
        UTF8 msg[MOD_LBUF_SIZE];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "@cboot: %s is not on the channel.",
                 pMoniker ? reinterpret_cast<const char *>(pMoniker)
                          : "???");
        m_pINotify->Notify(executor, msg);
        return MUX_S_OK;
    }

    // Notify executor and victim.
    //
    const UTF8 *pExecMoniker = nullptr;
    m_pIObjectInfo->GetMoniker(executor, &pExecMoniker);
    const UTF8 *pVictMoniker = nullptr;
    m_pIObjectInfo->GetMoniker(thing, &pVictMoniker);

    UTF8 msg[MOD_LBUF_SIZE];
    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "You boot %s off channel %s.",
             pVictMoniker ? reinterpret_cast<const char *>(pVictMoniker)
                          : "???",
             reinterpret_cast<const char *>(ch->name));
    m_pINotify->Notify(executor, msg);

    snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
             "%s boots you off channel %s.",
             pExecMoniker ? reinterpret_cast<const char *>(pExecMoniker)
                          : "???",
             reinterpret_cast<const char *>(ch->name));
    m_pINotify->Notify(thing, msg);

    if (!(key & CBOOT_QUIET))
    {
        // Broadcast boot message to channel.
        //
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "%s %s boots %s off the channel.",
                 reinterpret_cast<const char *>(ch->header),
                 pExecMoniker ? reinterpret_cast<const char *>(pExecMoniker)
                              : "???",
                 pVictMoniker ? reinterpret_cast<const char *>(pVictMoniker)
                              : "???");
        SendChannelMessage(executor, ch, msg, false);
    }

    // Remove victim from channel.
    //
    do_delcomchannel(thing, ch->name, (key & CBOOT_QUIET) != 0);
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
    UTF8 alias[MAX_ALIAS_LEN + 1];
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

MUX_RESULT CComsysMod::GetRevision(int *pRev)
{
    if (nullptr == pRev)
    {
        return MUX_E_INVALIDARG;
    }
    *pRev = m_revision;
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

    comsys_t &c = get_comsys(executor);

    if (static_cast<int>(c.aliases.size()) >= MAX_ALIASES_PER_PLAYER)
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
    for (const auto &ca : c.aliases)
    {
        if (ca.alias == reinterpret_cast<const char *>(pAlias))
        {
            if (nullptr != m_pINotify)
            {
                UTF8 msg[256];
                snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                         "That alias is already in use for channel %s.",
                         ca.channel.c_str());
                m_pINotify->RawNotify(executor, msg);
            }
            return MUX_E_FAIL;
        }
    }

    // Append and sort.
    //
    com_alias ca;
    ca.alias = reinterpret_cast<const char *>(pAlias);
    ca.channel = reinterpret_cast<const char *>(ch->name);
    c.aliases.push_back(std::move(ca));

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

    comsys_t &c = get_comsys(executor);

    for (auto it = c.aliases.begin(); it != c.aliases.end(); ++it)
    {
        if (it->alias != reinterpret_cast<const char *>(pAlias))
        {
            continue;
        }

        // Count other aliases for same channel.
        //
        int found = 0;
        for (const auto &ca : c.aliases)
        {
            if (ca.channel == it->channel)
            {
                found++;
            }
        }

        // If last alias for this channel, remove from channel.
        //
        if (found <= 1)
        {
            do_delcomchannel(executor,
                reinterpret_cast<const UTF8 *>(it->channel.c_str()),
                false);
        }

        if (nullptr != m_pINotify)
        {
            UTF8 msg[256];
            snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                     "Alias %s for channel %s deleted.",
                     reinterpret_cast<const char *>(pAlias),
                     it->channel.c_str());
            m_pINotify->RawNotify(executor, msg);
        }

        sqlite_wt_delete_player_channel(executor, pAlias);
        c.aliases.erase(it);
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
    auto it = m_comsys.find(executor);
    if (it == m_comsys.end())
    {
        return MUX_S_OK;
    }

    comsys_t &c = it->second;

    // Walk backwards to safely remove each alias.
    //
    while (!c.aliases.empty())
    {
        std::string alias = c.aliases.back().alias;
        DelAlias(executor, reinterpret_cast<const UTF8 *>(alias.c_str()));
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
    auto ch = std::make_unique<channel>();

    strncpy(reinterpret_cast<char *>(ch->name),
            reinterpret_cast<const char *>(pName), MAX_CHANNEL_LEN);
    ch->name[MAX_CHANNEL_LEN] = '\0';

    snprintf(reinterpret_cast<char *>(ch->header), MAX_HEADER_LEN + 1,
             "[%s]", reinterpret_cast<const char *>(ch->name));

    ch->type = CHANNEL_DEFAULT;
    ch->charge_who = executor;
    ch->chan_obj = NOTHING;

    channel *pCh = ch.get();

    std::vector<UTF8> key(ch->name,
        ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
    m_channels[key] = std::move(ch);

    sqlite_wt_channel(pCh);

    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Channel %s created.",
                 reinterpret_cast<const char *>(pCh->name));
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

    // Capture canonical name before the channel object goes away.
    //
    const std::string channelName(
        reinterpret_cast<const char *>(ch->name));

    // Remove from SQLite.  ON DELETE CASCADE clears channel_users and
    // player_channels rows for this channel name.
    //
    if (nullptr != m_pIStorage)
    {
        m_pIStorage->DeleteChannel(ch->name);
        bump_revision();
    }

    // #1199: purge other players' in-memory aliases that pointed here.
    // (SQLite rows are already CASCADE-deleted; still drop local aliases.)
    //
    for (auto &kv : m_comsys)
    {
        comsys_t &c = kv.second;
        for (auto ait = c.aliases.begin(); ait != c.aliases.end(); )
        {
            if (ait->channel == channelName)
            {
                sqlite_wt_delete_player_channel(c.who,
                    reinterpret_cast<const UTF8 *>(ait->alias.c_str()));
                ait = c.aliases.erase(ait);
            }
            else
            {
                ++ait;
            }
        }
    }

    // Notify before erasing (ch will be destroyed by unique_ptr).
    //
    if (nullptr != m_pINotify)
    {
        UTF8 msg[256];
        snprintf(reinterpret_cast<char *>(msg), sizeof(msg),
                 "Channel %s destroyed.",
                 reinterpret_cast<const char *>(pName));
        m_pINotify->RawNotify(executor, msg);
    }

    // Remove from map — unique_ptr cleans up channel and all users.
    //
    std::vector<UTF8> key(ch->name,
        ch->name + strlen(reinterpret_cast<const char *>(ch->name)) + 1);
    m_channels.erase(key);

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
    if (nullptr != m_pIStorage)
    {
        m_pIStorage->Release();
        m_pIStorage = nullptr;
    }

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
    // Channel consistency checks (#1195):
    // 1. Prune users with invalid dbrefs only — object listeners stay.
    // 2. Reassign channel ownership if owner is gone.
    // 3. Prune comsys alias rows for destroyed players.
    //
    for (auto &kv : m_channels)
    {
        struct channel *ch = kv.second.get();

        for (auto it = ch->users.begin(); it != ch->users.end(); )
        {
            bool bValid = false;
            if (nullptr != m_pIObjectInfo)
            {
                m_pIObjectInfo->IsValid(it->second.who, &bValid);
            }
            if (!bValid)
            {
                it = ch->users.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (ch->charge_who != NOTHING && nullptr != m_pIObjectInfo)
        {
            bool bValid = false;
            m_pIObjectInfo->IsValid(ch->charge_who, &bValid);
            if (!bValid)
            {
                ch->charge_who = 1; // GOD
            }
        }
    }

    for (auto it = m_comsys.begin(); it != m_comsys.end(); )
    {
        bool bPlayer = false;
        if (nullptr != m_pIObjectInfo)
        {
            m_pIObjectInfo->IsPlayer(it->first, &bPlayer);
        }
        if (!bPlayer)
        {
            it = m_comsys.erase(it);
        }
        else
        {
            ++it;
        }
    }
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
    return m_cRef.fetch_add(1, std::memory_order_relaxed) + 1;
}

uint32_t CComsysModFactory::Release(void)
{
    uint32_t prev = m_cRef.fetch_sub(1, std::memory_order_acq_rel);
    if (1 == prev)
    {
        delete this;
        return 0;
    }
    return prev - 1;
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
