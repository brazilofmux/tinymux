// comsys.h
//
// $Id: comsys.h,v 1.12 2002-07-25 13:14:00 jake Exp $
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
#if 0
void do_cleanupchannels(void);
#endif // 0
void do_channelnuke(dbref player);
void do_comdisconnect(dbref player);
void do_comconnect(dbref player);
void do_clearcom(dbref executor, dbref caller, dbref enactor, int unused2);
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

struct channel *select_channel(char *channel);

BOOL  do_comsystem(dbref who, char *cmd);
void  do_chanlist(dbref executor, dbref caller, dbref enactor, int key);

#define CHANNEL_JOIN      0x1
#define CHANNEL_TRANSMIT  0x2
#define CHANNEL_RECEIVE   0x4

#define CHANNEL_PL_MULT   0x001 // See JOIN, TRANSMIT, RECEIVE
#define CHANNEL_OBJ_MULT  0x010 // See JOIN, TRANSMIT, RECEIVE
#define CHANNEL_LOUD      0x100
#define CHANNEL_PUBLIC    0x200
#define CHANNEL_SPOOF     0x400


// explanation of logic: If it's not owned by god, and it's either not
// a player, or a connected player, it's good... If it is owned by god,
// then if it's going, assume it's already gone, no matter what it is.
// :)
//
#define UNDEAD(x) (((!God(Owner(x))) || !(Going(x))) && \
        ((Typeof(x) != TYPE_PLAYER) || (Connected(x))))

#endif // __COMSYS_H__
