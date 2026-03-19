/*! \file comsys_mod.h
 * \brief Comsys Module — Channel system as a loadable module
 *
 * This module implements the MUX channel system (@channel, addcom,
 * delcom, etc.) as a dynamically loaded module rather than compiled
 * into the netmux binary.
 */

#ifndef COMSYS_MOD_H
#define COMSYS_MOD_H

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOTHING
#define NOTHING (-1)
#endif

const MUX_CID CID_ComsysMod = UINT64_C(0x00000002C5A2F193);

// Channel type flags (mirrored from comsys.h for module use).
//
#define CHANNEL_PLAYER_JOIN     0x00000001
#define CHANNEL_PLAYER_TRANSMIT 0x00000002
#define CHANNEL_PLAYER_RECEIVE  0x00000004
#define CHANNEL_OBJECT_JOIN     0x00000010
#define CHANNEL_OBJECT_TRANSMIT 0x00000020
#define CHANNEL_OBJECT_RECEIVE  0x00000040
#define CHANNEL_LOUD            0x00000100
#define CHANNEL_PUBLIC          0x00000200
#define CHANNEL_SPOOF           0x00000400

#define CHANNEL_DEFAULT (CHANNEL_PLAYER_JOIN | CHANNEL_PLAYER_TRANSMIT \
    | CHANNEL_PLAYER_RECEIVE | CHANNEL_OBJECT_JOIN \
    | CHANNEL_OBJECT_TRANSMIT | CHANNEL_OBJECT_RECEIVE)

#define MAX_CHANNEL_LEN  50
#define MAX_HEADER_LEN  100
#define MAX_TITLE_LEN   200
#define MAX_ALIAS_LEN    15
#define MAX_ALIASES_PER_PLAYER 100

// Command switch keys (mirrored from externs.h).
//
#define CEMIT_NOHEADER  1
#define CLIST_FULL      1
#define CLIST_HEADERS   2
#define COMTITLE_ON     1
#define COMTITLE_OFF    2
#define COMTITLE_GAG    3
#define COMTITLE_UNGAG  4

// @cset switch keys (mirrored from externs.h).
//
#define CSET_PUBLIC     0
#define CSET_PRIVATE    1
#define CSET_LOUD       2
#define CSET_QUIET      3
#define CSET_LIST       4
#define CSET_OBJECT     5
#define CSET_SPOOF      6
#define CSET_NOSPOOF    7
#define CSET_HEADER     8
#define CSET_LOG        9
#define CSET_LOG_TIME  10

// @cboot switch keys.
//
#define CBOOT_QUIET     1

// @editchannel flag keys.
//
#define EDIT_CHANNEL_CCHOWN   0
#define EDIT_CHANNEL_CCHARGE  1
#define EDIT_CHANNEL_CPFLAGS  2
#define EDIT_CHANNEL_COFLAGS  3

#define MAX_COST        32767
#define MOD_LBUF_SIZE   8000

// Per-player alias entry — pairs an alias string with its channel name.
//
struct com_alias
{
    std::string alias;
    std::string channel;
};

// Per-user channel subscription record.
//
struct comuser
{
    dbref who;
    bool  bUserIsOn;
    std::string title;
    bool  ComTitleStatus;
    bool  bGagJoinLeave;
    bool  bConnected;

    comuser() : who(NOTHING), bUserIsOn(false),
        ComTitleStatus(true), bGagJoinLeave(false), bConnected(false) {}
};

// Channel definition.
//
struct channel
{
    UTF8 name[MAX_CHANNEL_LEN + 1];
    UTF8 header[MAX_HEADER_LEN + 1];
    int  type;
    int  temp1;
    int  temp2;
    int  charge;
    dbref charge_who;
    int  amount_col;
    dbref chan_obj;
    std::map<dbref, comuser> users;
    int  num_messages;

    channel() : type(0), temp1(0), temp2(0), charge(0),
        charge_who(NOTHING), amount_col(0), chan_obj(NOTHING),
        num_messages(0)
    {
        memset(name, 0, sizeof(name));
        memset(header, 0, sizeof(header));
    }
};

// Per-player alias tracking.
//
struct comsys_t
{
    dbref who;
    std::vector<com_alias> aliases;

    comsys_t() : who(NOTHING) {}
};

class CComsysMod : public mux_IComsysControl, mux_IServerEventsSink
{
private:
    mux_ILog                  *m_pILog;
    mux_IServerEventsControl  *m_pIServerEventsControl;
    mux_INotify               *m_pINotify;
    mux_IObjectInfo           *m_pIObjectInfo;
    mux_IAttributeAccess      *m_pIAttributeAccess;
    mux_IEvaluator            *m_pIEvaluator;
    mux_IPermissions          *m_pIPermissions;

    // Storage interface — routes SQLite access through the engine.
    //
    mux_IComsysStorage *m_pIStorage;

    // Channel data — owned by the module.
    //
    std::map<std::vector<UTF8>, std::unique_ptr<channel>> m_channels;
    std::unordered_map<dbref, comsys_t> m_comsys;

    // Internal helpers.
    //
    bool LoadChannels(void);
    bool LoadChannelUsers(void);
    bool LoadPlayerChannels(void);

    struct channel *select_channel(const UTF8 *name);
    struct comuser *select_user(struct channel *ch, dbref player);
    comsys_t &get_comsys(dbref who);
    const UTF8 *get_channel_from_alias(dbref player, const UTF8 *alias);
    void do_comconnectchannel(dbref player, const UTF8 *channel,
        const std::string &alias);
    void do_comdisconnectchannel(dbref player, const UTF8 *channel);
    void do_comconnectraw_notify(dbref player, const UTF8 *chan);
    void do_comdisconnectraw_notify(dbref player, const UTF8 *chan);

    bool test_transmit_access(dbref player, struct channel *ch);
    bool test_receive_access(dbref player, struct channel *ch);
    void do_processcom(dbref player, const UTF8 *channel, UTF8 *msg);
    void SendChannelMessage(dbref executor, struct channel *ch,
        const UTF8 *msg, bool bJoinLeaveMsg);
    void do_joinchannel(dbref player, struct channel *ch);
    void do_leavechannel(dbref player, struct channel *ch);
    void do_comwho(dbref player, struct channel *ch);

    void sqlite_wt_channel_user(const UTF8 *channel_name,
        const comuser &user);
    void sqlite_wt_channel(struct channel *ch);
    void sqlite_wt_player_channel(dbref who, const UTF8 *alias,
        const UTF8 *channel_name);
    void sqlite_wt_delete_player_channel(dbref who, const UTF8 *alias);
    void sqlite_wt_delete_channel_user(const UTF8 *channel_name, dbref who);

    bool test_join_access(dbref player, struct channel *ch);
    void do_delcomchannel(dbref player, const UTF8 *channel, bool bQuiet);
    void do_comlast(dbref player, struct channel *ch, int arg);
    void sort_com_aliases(comsys_t &c);

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_IComsysControl
    //
    MUX_RESULT Initialize(mux_IComsysStorage *pStorage) override;
    MUX_RESULT PlayerConnect(dbref player) override;
    MUX_RESULT PlayerDisconnect(dbref player) override;
    MUX_RESULT PlayerNuke(dbref player) override;
    MUX_RESULT AddAlias(dbref executor, const UTF8 *pAlias,
        const UTF8 *pChannel) override;
    MUX_RESULT DelAlias(dbref executor, const UTF8 *pAlias) override;
    MUX_RESULT ClearAliases(dbref executor) override;
    MUX_RESULT CreateChannel(dbref executor, const UTF8 *pName) override;
    MUX_RESULT DestroyChannel(dbref executor, const UTF8 *pName) override;
    MUX_RESULT AllCom(dbref executor, const UTF8 *pAction) override;
    MUX_RESULT ComList(dbref executor, const UTF8 *pPattern) override;
    MUX_RESULT ComTitle(dbref executor, const UTF8 *pAlias,
        const UTF8 *pTitle, int key) override;
    MUX_RESULT ChanList(dbref executor, const UTF8 *pPattern,
        int key) override;
    MUX_RESULT ChanWho(dbref executor, const UTF8 *pArg) override;
    MUX_RESULT CEmit(dbref executor, const UTF8 *pChannel,
        const UTF8 *pText, int key) override;
    MUX_RESULT CSet(dbref executor, const UTF8 *pChannel,
        const UTF8 *pValue, int key) override;
    MUX_RESULT EditChannel(dbref executor, const UTF8 *pChannel,
        const UTF8 *pValue, int flag) override;
    MUX_RESULT CBoot(dbref executor, const UTF8 *pChannel,
        const UTF8 *pVictim, int key) override;
    MUX_RESULT ProcessCommand(dbref executor, const UTF8 *pCmd,
        bool *pbHandled) override;

    // mux_IServerEventsSink
    //
    void startup(void) override;
    void presync_database(void) override;
    void presync_database_sigsegv(void) override;
    void dump_database(int dump_type) override;
    void dump_complete_signal(void) override;
    void shutdown(void) override;
    void dbck(void) override;
    void connect(dbref player, int isnew, int num) override;
    void disconnect(dbref player, int num) override;
    void data_create(dbref object) override;
    void data_clone(dbref clone, dbref source) override;
    void data_free(dbref object) override;

    CComsysMod(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CComsysMod();

private:
    uint32_t m_cRef;
};

class CComsysModFactory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CComsysModFactory(void);
    virtual ~CComsysModFactory();

private:
    uint32_t m_cRef;
};

#endif // COMSYS_MOD_H
