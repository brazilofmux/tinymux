// comsys.h
//
// $Id: comsys.h,v 1.17 2002-08-24 02:33:19 jake Exp $
//

#ifndef __COMSYS_H__
#define __COMSYS_H__

typedef struct chanentry CHANENT;
struct chanentry
{
    char *channame;
    struct channel *chan;
};

#define NUM_COMSYS 500

#define MAX_CHANNEL_LEN 50
#define MAX_HEADER_LEN  100
#define MAX_TITLE_LEN   200
#define MAX_ALIAS_LEN   5
#define ALIAS_SIZE      (MAX_ALIAS_LEN+1)

struct comuser
{
    dbref who;
    BOOL bUserIsOn;
    char *title;
    BOOL ComTitleStatus;
    struct comuser *on_next;
};

struct channel
{
    char name[MAX_CHANNEL_LEN+1];
    char header[MAX_HEADER_LEN+1];
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
    char *alias;
    char **channels;

    struct tagComsys *next;
} comsys_t;

void save_comsys(char *filename);
void load_comsys(char *filename);
void del_comsys(dbref who);
void add_comsys(comsys_t *c);
BOOL do_test_access(dbref player, long access, struct channel *chan);
void do_joinchannel(dbref player, struct channel *ch);
void do_comdisconnectchannel(dbref player, char *channel);
void load_channels(FILE *fp);
void purge_comsystem(void);
void save_channels(FILE *fp);
void destroy_comsys(comsys_t *c);
void sort_com_aliases(comsys_t *c);
void load_comsystem(FILE *fp);
void save_comsystem(FILE *fp);
void SendChannelMessage
(
    dbref  player,
    struct channel *ch,
    char  *msgNormal,
    char  *msgNoComtitle,
    BOOL   bRaw
);
void do_comwho(dbref player, struct channel *ch);
void do_comlast(dbref player, struct channel *ch, int arg);
void do_leavechannel(dbref player, struct channel *ch);
void do_delcomchannel(dbref player, char *channel, BOOL bQuiet);
#if 0
void do_cleanupchannels(void);
#endif // 0
void do_channelnuke(dbref player);
void sort_users(struct channel *ch);
void do_comdisconnect(dbref player);
void do_comconnect(dbref player);
void do_clearcom(dbref executor, dbref caller, dbref enactor, int unused2);
void do_cheader(dbref player, char *channel, char *header);
void do_addcom
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
);

comsys_t *create_new_comsys ();

struct channel *select_channel(char *channel);
struct comuser *select_user(struct channel *ch, dbref player);

char  *get_channel_from_alias();

BOOL  do_comsystem(dbref who, char *cmd);
void  do_chanlist(dbref executor, dbref caller, dbref enactor, int key);
extern char *modSpeech(dbref player, char *message, BOOL bWhich, char *command);

#define CHANNEL_JOIN      0x1
#define CHANNEL_TRANSMIT  0x2
#define CHANNEL_RECEIVE   0x4

#define CHANNEL_PL_MULT   0x001 // See JOIN, TRANSMIT, RECEIVE
#define CHANNEL_OBJ_MULT  0x010 // See JOIN, TRANSMIT, RECEIVE
#define CHANNEL_LOUD      0x100
#define CHANNEL_PUBLIC    0x200
#define CHANNEL_SPOOF     0x400


// Connected players and non-garbage objects are ok.
//
#define UNDEAD(x) (Good_obj(x) && ((Typeof(x) != TYPE_PLAYER) || Connected(x)))

#endif // __COMSYS_H__
