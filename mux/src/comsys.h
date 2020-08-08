/*! \file comsys.h
 * \brief Channel Communication System.
 *
 */

#ifndef __COMSYS_H__
#define __COMSYS_H__

#define NUM_COMSYS 500

//! \def MAX_USERS_PER_CHANNEL
// Maximum Users Per Channel
#define MAX_USERS_PER_CHANNEL 1000000

//! \def MAX_CHANNEL_LEN
// Maximum length for channel names
#define MAX_CHANNEL_LEN 50

//! \def MAX_HEADER_LEN
// Maximum length for channel headers (display names)
#define MAX_HEADER_LEN  100

//! \def MAX_TITLE_LEN
// Maximum length for Player titles for channels
#define MAX_TITLE_LEN   200

//! \def MAX_ALIAS_LEN
// Maximum length for an alias to a channel
#define MAX_ALIAS_LEN   15

//! \def ALIAS_SIZE
// Actual size of a channel alias
#define ALIAS_SIZE      (MAX_ALIAS_LEN+1)

//! \def MAX_COST
// Maximum cost to use for a channel
#define MAX_COST        32767

//! \struct comuser
// Comsys user data
struct comuser
{
    //! dbref of the user
    dbref who;
    //! Is the user on the channel
    bool bUserIsOn;
    //! Per channel display name
    UTF8 *title;
    //! Status of the title
    bool ComTitleStatus;
    //! Pointer to the next user on the channel
    struct comuser *on_next;
};

//! \struct channel
// Channel data for Comsys
struct channel
{
    //! Name of the Channel
    UTF8 name[MAX_CHANNEL_LEN+1];
    //! Header / Display Name of the channel
    UTF8 header[MAX_HEADER_LEN+1];
    //! Flag data for channel
    int type;
    int temp1;
    int temp2;
    //! Cost for channel usage
    int charge;
    //! Person to charge for usage
    dbref charge_who;
    int amount_col;
    //! Users on the channel
    int num_users;
    //! Maximum users allowed
    int max_users;
    //! Channel object if set
    dbref chan_obj;
    //! Linked list of user data for all players on the channel
    struct comuser **users;
    //! Linked list of user data for connected players
    struct comuser *on_users;
    //! Number of messages sent on the channel
    int num_messages;
};

//! \struct tagComsys
// Data for storing user information for players on channels
typedef struct tagComsys
{
    //! DBREF of the user
    dbref who;

    //! Number of channels
    int numchannels;
    int maxchannels;
    //! Alias for this channel
    UTF8 *alias;
    UTF8 **channels;

    struct tagComsys *next;
} comsys_t;

//! \brief Save communication system data to disk
//! \param filename - file to use for for writing data
void save_comsys(UTF8 *filename);

//! \brief Save user aliases on a per-channel basis
//! \param fp - FILE pointer used for writing data
void save_channels(FILE *fp);

//! \brief Save channel data and some user state for each user on the channel
void save_comsystem(FILE *fp);

//! \brief Open file and load comsystem data from disk
//! \param filename - filename to open for reading
void load_comsys(UTF8 *filename);

//! \brief Delete comsystem information for the given dbref
void del_comsys(dbref who);

//! \brief Deallocates previously allocated memory for all fields of comsys_t
//! \param c - comsys_t pointer to deallocate
void destroy_comsys(comsys_t *c);

//! \brief Inserts the given comsys_t pointer into the comsys_table
//! \param c - comsys_t pointer to insert into the table
void add_comsys(comsys_t *c);

//! \brief Add player as an active member of the given channel
//! \param player - dbref of player to add
//! \param ch - comsys channel pointer to add to
void do_joinchannel(dbref player, struct channel *ch);

//! \brief Remove disconnecting player from the active user list for all channels
//! \param player - dbref to remove
void do_comdisconnect(dbref player);

//! \brief Remove player from the active user list of channel
//! \param player - dbref of the player to remove
//! \param channel - name of the channel
void do_comdisconnectchannel(dbref player, UTF8 *channel);

//! \brief Handle connecting player channel activations
//! \param player - dbref of the connecting player to process
void do_comconnect(dbref player);

//! \brief Remove objects with no channels or objects that are destroyed
void purge_comsystem(void);

//! Sort aliases for the given comsys_t data
void sort_com_aliases(comsys_t *c);

//! \brief Xmit messages to the listening objects on channel & log data
//! \param player - dbref of executor
//! \param ch - channel data to transmit the message on
//! \param msgNormal - message to send with comtitle
//! \param msgNoComtitle - message to send w/o comtitle
void SendChannelMessage(dbref player, struct channel *ch, UTF8 *msgNormal,
    UTF8 *msgNoComtitle);

//! \brief Process '<alias> who' command to show online channel users
//! \brief player - enacting player
//! \brief ch - channel to display
void do_comwho(dbref player, struct channel *ch);

//! \brief Display message history for a particular channel
//! \param player - requesting player
//! \param ch - channel to determine history for
//! \param arg - number of historical messages to display
void do_comlast(dbref player, struct channel *ch, int arg);

//! \brief Process a player request to temporary leave a channel
//! \param player - player to remove
//! \param ch - channel to remove the player from
void do_leavechannel(dbref player, struct channel *ch);

//! \brief Process a channel removal for player ("delcom")
//! \param player - player to remove
//! \param channel - channel name to remove
//! \param quiet - show messages or not
void do_delcomchannel(dbref player, UTF8 *channel, bool bQuiet);

//! \brief Clear all channel subscriptions for the enactor ("clearcom")
void do_clearcom(dbref executor, dbref caller, dbref enactor, int eval, int key);

//! \brief Add a new channel subscription/alias for the enactor
void do_addcom(dbref executor, dbref caller, dbref enactor, int eval, int key, int nargs,
    UTF8 *arg1, UTF8 *arg2, const UTF8 *cargs[], int ncargs);

//! \brief Process a request to set a channel header
//! \param player - player requesting the change
//! \param channel - channel name to change
//! \param header - new header to use
void do_cheader(dbref player, UTF8 *channel, UTF8 *header);

//! \brief Locate a channel structure by channel name
//! \param channel - name of channel to locate
//! @return struct channel pointer or nullptr if not found
struct channel *select_channel(UTF8 *channel);

//! \brief Locate player in the user list for the given channel
//! \param ch - channel data to search
//! \param player - dbref of the player to locate
//! @return nullptr if not found, or struct comuser pointer
struct comuser *select_user(struct channel *ch, dbref player);

//! \brief Validate alias limits of 1-5 characters, no spaces, no ANSI
//! \param pAlias - incoming alias
//! \param nValidAlias - return size of the valid alias
//! \param bValidAlias - is alias valid or not
//! \return new alias data or nullptr on failure
UTF8 *MakeCanonicalComAlias(const UTF8 *pAlias, size_t *nValidAlias,
        bool *bValidAlias);

//! \brief Allocate and initialize a new comsys_t structure
//! \return initialized memory or nullptr if allocation failed
comsys_t *create_new_comsys();

//! \brief Purge channels owned by player. Internal cleanup called from db.c
//! \param player - dbref of cleanup target
void do_channelnuke(dbref player);

//! \brief Sort users on channel into dbref order
//! \param ch - channel to process
void sort_users(struct channel *ch);

//! \brief Process comsys related commands 'alias <cmd>' (from command.cpp)
//! \param who - enactor
//! \param cmd - command to process
//! \return true to continue searching command matches, false to stop
bool do_comsystem(dbref who, UTF8 *cmd);

//! \brief Check if player can join a channel
//! \param player - player to check for join access
//! \param chan - channel to check
//! \return true if allowed, false if denied
bool test_join_access(dbref player, struct channel *chan);

//! \brief Check if player can transmit on a channel
//! \param player - player to check for join access
//! \param chan - channel to check
//! \return true if allowed, false if denied
bool test_transmit_access(dbref player, struct channel *chan);

//! \brief Check if player can receive channel transmission
//! \param player - player to check for join access
//! \param chan - channel to check
//! \return true if allowed, false if denied
bool test_receive_access(dbref player, struct channel *chan);

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
