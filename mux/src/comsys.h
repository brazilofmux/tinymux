/*! \file comsys.h
 * \brief Channel Communication System.
 *
 * $Id$
 *
 */

#ifndef __COMSYS_H__
#define __COMSYS_H__

typedef struct chanentry CHANENT;
struct chanentry
{
    UTF8 *channame;
    struct channel *chan;
};

#define NUM_COMSYS 500

#define MAX_USERS_PER_CHANNEL 1000000
#define MAX_CHANNEL_LEN 50
#define MAX_HEADER_LEN  100
#define MAX_TITLE_LEN   200
#define MAX_ALIAS_LEN   5
#define ALIAS_SIZE      (MAX_ALIAS_LEN+1)
#define MAX_COST        32767

struct comuser
{
    dbref who;
    bool bUserIsOn;
    UTF8 *title;
    bool ComTitleStatus;
    struct comuser *on_next;
};

struct channel
{
    UTF8 name[MAX_CHANNEL_LEN+1];
    UTF8 header[MAX_HEADER_LEN+1];
    int type;
    int temp1;
    int temp2;
    int charge;
    dbref charge_who;
    int amount_col;
    int num_users;
    int max_users;
    dbref chan_obj;
    struct comuser **users;
    struct comuser *on_users;   /* Linked list of who is on */
    int num_messages;
};

typedef struct tagComsys
{
    dbref who;

    int numchannels;
    int maxchannels;
    UTF8 *alias;
    UTF8 **channels;

    struct tagComsys *next;
} comsys_t;

void save_comsys(UTF8 *filename);
void load_comsys(UTF8 *filename);
void del_comsys(dbref who);
void add_comsys(comsys_t *c);
bool test_join_access(dbref player, struct channel *chan);
bool test_transmit_access(dbref player, struct channel *chan);
bool test_receive_access(dbref player, struct channel *chan);
void do_joinchannel(dbref player, struct channel *ch);
void do_comdisconnectchannel(dbref player, UTF8 *channel);
void purge_comsystem(void);
void save_channels(FILE *fp);
void destroy_comsys(comsys_t *c);
void sort_com_aliases(comsys_t *c);
void save_comsystem(FILE *fp);
void SendChannelMessage
(
    dbref  player,
    struct channel *ch,
    UTF8  *msgNormal,
    UTF8  *msgNoComtitle
);
void do_comwho(dbref player, struct channel *ch);
void do_comlast(dbref player, struct channel *ch, int arg);
void do_leavechannel(dbref player, struct channel *ch);
void do_delcomchannel(dbref player, UTF8 *channel, bool bQuiet);
#if 0
void do_cleanupchannels(void);
#endif // 0
void do_channelnuke(dbref player);
void sort_users(struct channel *ch);
void do_comdisconnect(dbref player);
void do_comconnect(dbref player);
void do_clearcom(dbref executor, dbref caller, dbref enactor, int unused2);
void do_cheader(dbref player, UTF8 *channel, UTF8 *header);
void do_addcom
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2
);

comsys_t *create_new_comsys ();

struct channel *select_channel(UTF8 *channel);
struct comuser *select_user(struct channel *ch, dbref player);

UTF8  *get_channel_from_alias();
UTF8 *MakeCanonicalComAlias
(
    const UTF8 *pAlias,
    size_t *nValidAlias,
    bool *bValidAlias
);

bool  do_comsystem(dbref who, UTF8 *cmd);

#define CHANNEL_PLAYER_JOIN     (0x00000001UL)
#define CHANNEL_PLAYER_TRANSMIT (0x00000002UL)
#define CHANNEL_PLAYER_RECEIVE  (0x00000004UL)
#define CHANNEL_OBJECT_JOIN     (0x00000010UL)
#define CHANNEL_OBJECT_TRANSMIT (0x00000020UL)
#define CHANNEL_OBJECT_RECEIVE  (0x00000040UL)
#define CHANNEL_LOUD            (0x00000100UL)
#define CHANNEL_PUBLIC          (0x00000200UL)
#define CHANNEL_SPOOF           (0x00000400UL)

// Connected players and non-garbage objects are ok.
//
#define UNDEAD(x) (Good_obj(x) && ((Typeof(x) != TYPE_PLAYER) || Connected(x)))

#endif // __COMSYS_H__
