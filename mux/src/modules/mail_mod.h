/*! \file mail_mod.h
 * \brief Mail Module — @mail system as a loadable module
 *
 * This module implements the MUX @mail system as a dynamically loaded
 * module rather than compiled into the netmux binary.
 */

#ifndef MAIL_MOD_H
#define MAIL_MOD_H

#include <sqlite3.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "../timeutil.h"

#ifndef NOTHING
#define NOTHING (-1)
#endif

const MUX_CID CID_MailMod = UINT64_C(0x00000002D7A3E1B5);

#define MOD_LBUF_SIZE   8000

// Mail message flags (mirrored from mail.h).
//
#define M_ISREAD    0x0001
#define M_UNREAD    0x0FFE
#define M_CLEARED   0x0002
#define M_URGENT    0x0004
#define M_MASS      0x0008
#define M_SAFE      0x0010
#define M_TAG       0x0040
#define M_FORWARD   0x0080
#define M_FMASK     0xF0FF
#define M_ALL       0x1000
#define M_MSUNREAD  0x2000
#define M_REPLY     0x4000

#define MAX_FOLDERS 15
#define All(ms)     ((ms).flags & M_ALL)
#define FolderBit(f) (256 * (f))
#define Folder(m)   ((m->read & ~M_FMASK) >> 8)
#define Read(m)     (m->read & M_ISREAD)
#define Cleared(m)  (m->read & M_CLEARED)
#define Unread(m)   (!Read(m))
#define Urgent(m)   (m->read & M_URGENT)
#define Mass(m)     (m->read & M_MASS)
#define M_Safe(m)   (m->read & M_SAFE)
#define Forward(m)  (m->read & M_FORWARD)
#define Tagged(m)   (m->read & M_TAG)

// @mail command switch keys (mirrored from externs.h).
//
#define MAIL_STATS      1
#define MAIL_DSTATS     2
#define MAIL_FSTATS     3
#define MAIL_DEBUG      4
#define MAIL_NUKE       5
#define MAIL_FOLDER     6
#define MAIL_LIST       7
#define MAIL_READ       8
#define MAIL_CLEAR      9
#define MAIL_UNCLEAR    10
#define MAIL_PURGE      11
#define MAIL_FILE       12
#define MAIL_TAG        13
#define MAIL_UNTAG      14
#define MAIL_FORWARD    15
#define MAIL_SEND       16
#define MAIL_EDIT       17
#define MAIL_URGENT     18
#define MAIL_ALIAS      19
#define MAIL_ALIST      20
#define MAIL_PROOF      21
#define MAIL_ABORT      22
#define MAIL_QUICK      23
#define MAIL_REVIEW     24
#define MAIL_RETRACT    25
#define MAIL_CC         26
#define MAIL_SAFE       27
#define MAIL_REPLY      28
#define MAIL_REPLYALL   29
#define MAIL_BCC        30
#define MAIL_NEXT       31
#define MAIL_QUOTE      0x100

// @malias switch keys.
//
#define MALIAS_DESC     1
#define MALIAS_CHOWN    2
#define MALIAS_ADD      3
#define MALIAS_REMOVE   4
#define MALIAS_DELETE   5
#define MALIAS_RENAME   6
#define MALIAS_LIST     8
#define MALIAS_STATUS   9

// Folder switch keys.
//
#define FOLDER_SET      1
#define FOLDER_READ     2
#define FOLDER_FILE     4
#define FOLDER_LIST     8

// Mail message structure.
//
struct mail
{
    struct mail *next;
    struct mail *prev;
    dbref        to;
    dbref        from;
    int          number;
    UTF8        *time;
    UTF8        *subject;
    UTF8        *tolist;
    int          read;
    int64_t      sqlite_id;
};

// Message body storage (reference-counted deduplication).
//
struct mail_body
{
    size_t m_nMessage;
    UTF8  *m_pMessage;
    int    m_nRefs;
};

// Mail alias.
//
#define MAX_MALIAS_MEMBERSHIP 100

typedef struct malias
{
    int   owner;
    int   numrecep;
    UTF8 *name;
    UTF8 *desc;
    size_t desc_width;
    dbref list[MAX_MALIAS_MEMBERSHIP];
} malias_t;

// Mail selector for list/read operations.
//
typedef unsigned int mail_flag;

struct mail_selector
{
    int       low;
    int       high;
    mail_flag flags;
    dbref     player;
    int       days;
    int       day_comp;
};

class CMailMod : public mux_IMailControl, mux_IServerEventsSink
{
private:
    mux_ILog                  *m_pILog;
    mux_IServerEventsControl  *m_pIServerEventsControl;
    mux_INotify               *m_pINotify;
    mux_IObjectInfo           *m_pIObjectInfo;
    mux_IAttributeAccess      *m_pIAttributeAccess;
    mux_IPermissions          *m_pIPermissions;

    // SQLite connection — module's own handle to the game database.
    //
    sqlite3 *m_db;

    // Configuration (passed via Initialize).
    //
    int m_mail_expiration;
    int m_mail_per_player;

    // Mail data — owned by the module.
    //
    std::unordered_map<dbref, struct mail *> m_mail_htab;
    struct mail_body *m_mail_list;
    int m_mail_db_top;
    int m_mail_db_size;

    // Alias data.
    //
    malias_t **m_malias;
    int m_ma_size;
    int m_ma_top;

    // Internal helpers.
    //
    bool OpenDatabase(const UTF8 *pPath);
    void CloseDatabase(void);
    bool LoadMailBodies(void);
    bool LoadMailHeaders(void);
    bool LoadMailAliases(void);
    void ClearRuntimeData(void);

    // Selector and matching helpers.
    //
    bool parse_msglist(const UTF8 *msglist, struct mail_selector *ms,
        dbref player);
    bool mail_match(struct mail *mp, struct mail_selector &ms, int num);
    int  player_folder(dbref player);

    // Display helpers.
    //
    static UTF8 *status_chars(struct mail *mp);
    UTF8 *mail_list_time(const UTF8 *the_time);
    void get_player_name(dbref who, UTF8 *buf, size_t bufsize);
    bool is_connected_visible(dbref who, dbref viewer);

    // Command implementations.
    //
    void do_mail_flags(dbref player, const UTF8 *msglist,
        mail_flag flag, bool negate);
    void do_mail_purge(dbref player);
    void do_mail_list(dbref player, const UTF8 *arg1,
        const UTF8 *arg2);
    void do_mail_read(dbref player, const UTF8 *arg1,
        const UTF8 *arg2);
    void do_mail_next(dbref player);
    void do_mail_file(dbref player, const UTF8 *msglist,
        const UTF8 *folder);
    void do_mail_stats(dbref player, int folder);
    bool do_mail_stub(dbref player, const UTF8 *arg1,
        const UTF8 *arg2);

    // Message body management.
    //
    void mail_db_grow(int newtop);
    int  new_mail_message(const UTF8 *message, int number);
    const UTF8 *MessageFetch(int number);
    size_t MessageFetchSize(int number);
    void MessageReferenceInc(int number);
    void MessageReferenceDec(int number);

    // Mail list management.
    //
    struct mail *MailListFirst(dbref player);
    struct mail *MailListNext(struct mail *mp, dbref player);
    void MailListAppend(dbref player, struct mail *mp);
    void MailListRemove(dbref player, struct mail *mp);
    void MailListRemoveAll(dbref player);

    // SQLite write-through.
    //
    void sqlite_wt_insert_mail(struct mail *mp);
    void sqlite_wt_update_mail_flags(struct mail *mp);
    void sqlite_wt_delete_mail(struct mail *mp);
    void sqlite_wt_delete_all_mail(int to_player);
    void sqlite_wt_mail_body(int number, const UTF8 *message);
    void sqlite_wt_delete_mail_body(int number);
    void sqlite_wt_sync_all_aliases(void);

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_IMailControl
    //
    MUX_RESULT Initialize(const UTF8 *pDatabasePath,
        int mail_expiration, int mail_per_player) override;
    MUX_RESULT PlayerConnect(dbref player) override;
    MUX_RESULT PlayerNuke(dbref player) override;
    MUX_RESULT MailCommand(dbref executor, int key,
        const UTF8 *pArg1, const UTF8 *pArg2) override;
    MUX_RESULT MaliasCommand(dbref executor, int key,
        const UTF8 *pArg1, const UTF8 *pArg2) override;
    MUX_RESULT FolderCommand(dbref executor, int key, int nargs,
        const UTF8 *pArg1, const UTF8 *pArg2) override;
    MUX_RESULT CheckMail(dbref player, int folder, bool silent) override;
    MUX_RESULT ExpireMail(void) override;
    MUX_RESULT CountMail(dbref player, int folder,
        int *pRead, int *pUnread, int *pCleared) override;
    MUX_RESULT DestroyPlayerMail(dbref player) override;

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

    CMailMod(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CMailMod();

private:
    uint32_t m_cRef;
    bool m_bLoading;  // Suppress write-through during bulk load.
};

class CMailModFactory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid,
        void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CMailModFactory(void);
    virtual ~CMailModFactory();

private:
    uint32_t m_cRef;
};

#endif // MAIL_MOD_H
