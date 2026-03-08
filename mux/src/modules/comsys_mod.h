/*! \file comsys_mod.h
 * \brief Comsys Module — Channel system as a loadable module
 *
 * This module implements the MUX channel system (@channel, addcom,
 * delcom, etc.) as a dynamically loaded module rather than compiled
 * into the netmux binary.
 */

#ifndef COMSYS_MOD_H
#define COMSYS_MOD_H

#include <sqlite3.h>
#include <map>
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
#define ALIAS_SIZE       16

// Per-user channel subscription record.
//
struct comuser
{
    dbref who;
    bool  bUserIsOn;
    UTF8 *title;
    bool  ComTitleStatus;
    bool  bGagJoinLeave;
    struct comuser *on_next;
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
    int  num_users;
    int  max_users;
    dbref chan_obj;
    struct comuser **users;
    struct comuser  *on_users;
    int  num_messages;
};

// Per-player alias tracking.
//
#define NUM_COMSYS 500

typedef struct tagComsys
{
    dbref who;
    int   numchannels;
    int   maxchannels;
    UTF8 *alias;
    UTF8 **channels;
    struct tagComsys *next;
} comsys_t;

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

    // SQLite connection — module's own handle to the game database.
    //
    sqlite3 *m_db;

    // Channel data — owned by the module.
    //
    std::map<std::vector<UTF8>, struct channel *> m_channels;
    comsys_t *m_comsys_table[NUM_COMSYS];
    int m_num_channels;

    // Internal helpers.
    //
    bool OpenDatabase(const UTF8 *pPath);
    void CloseDatabase(void);
    bool LoadChannels(void);
    bool LoadChannelUsers(void);
    bool LoadPlayerChannels(void);

    struct channel *select_channel(const UTF8 *name);
    struct comuser *select_user(struct channel *ch, dbref player);
    comsys_t *get_comsys(dbref who);
    void add_comsys(comsys_t *c);
    comsys_t *create_new_comsys(void);
    const UTF8 *get_channel_from_alias(dbref player, const UTF8 *alias);
    void do_comconnectchannel(dbref player, UTF8 *channel, UTF8 *alias, int i);
    void do_comdisconnectchannel(dbref player, UTF8 *channel);
    void do_comconnectraw_notify(dbref player, UTF8 *chan);
    void do_comdisconnectraw_notify(dbref player, UTF8 *chan);

    bool test_transmit_access(dbref player, struct channel *ch);
    bool test_receive_access(dbref player, struct channel *ch);
    void do_processcom(dbref player, const UTF8 *channel, UTF8 *msg);
    void SendChannelMessage(dbref executor, struct channel *ch,
        const UTF8 *msg, bool bJoinLeaveMsg);
    void do_joinchannel(dbref player, struct channel *ch);
    void do_leavechannel(dbref player, struct channel *ch);
    void do_comwho(dbref player, struct channel *ch);

    void sqlite_wt_channel_user(const UTF8 *channel_name, struct comuser *user);
    void sqlite_wt_channel(struct channel *ch);

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_IComsysControl
    //
    MUX_RESULT Initialize(const UTF8 *pDatabasePath) override;
    MUX_RESULT PlayerConnect(dbref player) override;
    MUX_RESULT PlayerDisconnect(dbref player) override;
    MUX_RESULT PlayerNuke(dbref player) override;
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
