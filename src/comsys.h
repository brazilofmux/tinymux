// Comsys.h
//
// $Id: comsys.h,v 1.2 2001-02-10 09:57:06 sdennis Exp $

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
#define MAX_TITLE_LEN   50
#define MAX_ALIAS_LEN    6

struct comuser
{
    dbref who;
    int bUserIsOn;
    char *title;
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
    int charge_who;
    int amount_col;
    int num_users;
    int max_users;
    int chan_obj;
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

extern comsys_t *comsys_table[NUM_COMSYS];

void save_comsys(char *filename);
void load_comsys(char *filename);
void del_comsys(dbref who);
void add_comsys(comsys_t *c);
void do_processcom(dbref player, char *arg1, char *arg2);
int do_test_access(dbref player, long access, struct channel *chan);
void do_joinchannel(dbref player, struct channel *ch);
void do_comdisconnectraw_notify(dbref player, char *chan);
void do_comconnectraw_notify(dbref player, char *chan);
void do_comdisconnectchannel(dbref player, char *channel);
void load_channels(FILE *fp);
void load_old_channels(FILE *fp);
void purge_comsystem(void);
void save_channels(FILE *fp);
void destroy_comsys(comsys_t *c);
void sort_com_aliases(comsys_t *c);
void load_comsystem(FILE *fp);
void save_comsystem(FILE *fp);
void do_comsend(struct channel *ch, char *msgNormal);
void do_comwho(dbref player, struct channel *ch);
void do_leavechannel(dbref player, struct channel *ch);
void do_delcomchannel(dbref player, char *channel);
void do_listchannels(dbref player);
void do_cleanupchannels(void);
void do_channelnuke(dbref player);
void sort_users(struct channel *ch);
void do_comconnectchannel(dbref player, char *channel, char *alias, int i);
void do_comdisconnect(dbref player);
void do_comconnect(dbref player);
void do_clearcom(dbref player, dbref unused1, int unused2);
void do_addcom(dbref player, dbref cause, int key, char *arg1, char *arg2);
void do_cheader(dbref player, char *channel, char *header);

comsys_t *get_comsys ();
comsys_t *create_new_comsys ();
char     *purge_comsys();
comsys_t *get_comsys ();
comsys_t *create_new_comsys ();

extern int num_channels;
extern int max_channels;
extern struct channel **channels;

struct channel *select_channel(char *channel);

struct comuser *select_user(struct channel *ch, dbref player);

char  *get_channel_from_alias();

char  *add_lastcom();

char  *sort_channels();
char  *check_channel();
char  *add_spaces();

char  *send_csdebug();

int   do_comsystem(dbref who, char *cmd);
char  *do_comdisconnectnotify();
char  *do_comconnectnotify();
void  do_chanlist(dbref player, dbref cause, int key);

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
