// command.cpp -- command parser and support routines.
//
// $Id: command.cpp,v 1.80 2006-01-11 04:19:53 jake Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "mguests.h"
#include "powers.h"
#include "vattr.h"
#include "help.h"
#include "pcre.h"

// Switch tables for the various commands.
//
static NAMETAB attrib_sw[] =
{
    {"access",          1,  CA_GOD,     ATTRIB_ACCESS},
    {"delete",          1,  CA_GOD,     ATTRIB_DELETE},
    {"rename",          1,  CA_GOD,     ATTRIB_RENAME},
    { NULL,             0,       0,     0}
};

static NAMETAB boot_sw[] =
{
    {"port",            1,  CA_WIZARD,  BOOT_PORT|SW_MULTIPLE},
    {"quiet",           1,  CA_WIZARD,  BOOT_QUIET|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB cboot_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  CBOOT_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB comtitle_sw[] =
{
    {"off",             2,  CA_PUBLIC,  COMTITLE_OFF},
    {"on",              2,  CA_PUBLIC,  COMTITLE_ON},
    { NULL,             0,          0,  0}
};

static NAMETAB cemit_sw[] =
{
    {"noheader",        1,  CA_PUBLIC,  CEMIT_NOHEADER},
    { NULL,             0,          0,  0}
};

static NAMETAB clone_sw[] =
{
    {"cost",            1,  CA_PUBLIC,  CLONE_SET_COST},
    {"inherit",         3,  CA_PUBLIC,  CLONE_INHERIT|SW_MULTIPLE},
    {"inventory",       3,  CA_PUBLIC,  CLONE_INVENTORY},
    {"location",        1,  CA_PUBLIC,  CLONE_LOCATION},
    {"parent",          2,  CA_PUBLIC,  CLONE_FROM_PARENT|SW_MULTIPLE},
    {"preserve",        2,  CA_WIZARD,  CLONE_PRESERVE|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB clist_sw[] =
{
    {"full",            0,  CA_PUBLIC,  CLIST_FULL},
    {"headers",         0,  CA_PUBLIC,  CLIST_HEADERS},
    { NULL,             0,          0,  0}
};

static NAMETAB cset_sw[] =
{
    {"anon",            1,  CA_PUBLIC,  CSET_SPOOF},
    {"header",          1,  CA_PUBLIC,  CSET_HEADER},
    {"list",            2,  CA_PUBLIC,  CSET_LIST},
    {"log" ,            3,  CA_PUBLIC,  CSET_LOG},
    {"loud",            3,  CA_PUBLIC,  CSET_LOUD},
    {"mute",            1,  CA_PUBLIC,  CSET_QUIET},
    {"nospoof",         1,  CA_PUBLIC,  CSET_NOSPOOF},
    {"object",          1,  CA_PUBLIC,  CSET_OBJECT},
    {"private",         2,  CA_PUBLIC,  CSET_PRIVATE},
    {"public",          2,  CA_PUBLIC,  CSET_PUBLIC},
    {"quiet",           1,  CA_PUBLIC,  CSET_QUIET},
    {"spoof",           1,  CA_PUBLIC,  CSET_SPOOF},
    { NULL,             0,          0,  0}
};

static NAMETAB dbck_sw[] =
{
    {"full",            1,  CA_WIZARD,  DBCK_FULL},
    { NULL,             0,          0,  0}
};

static NAMETAB decomp_sw[] =
{
    {"dbref",           1,  CA_PUBLIC,  DECOMP_DBREF},
    { NULL,             0,           0, 0}
};

static NAMETAB destroy_sw[] =
{
    {"instant",         4,  CA_PUBLIC,  DEST_INSTANT|SW_MULTIPLE},
    {"override",        8,  CA_PUBLIC,  DEST_OVERRIDE|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB dig_sw[] =
{
    {"teleport",        1,  CA_PUBLIC,  DIG_TELEPORT},
    { NULL,             0,          0,  0}
};

static NAMETAB doing_sw[] =
{
    {"header",          1,  CA_PUBLIC,  DOING_HEADER},
    {"message",         1,  CA_PUBLIC,  DOING_MESSAGE},
    {"poll",            1,  CA_PUBLIC,  DOING_POLL},
    {"quiet",           1,  CA_PUBLIC,  DOING_QUIET|SW_MULTIPLE},
    {"unique",          1,  CA_PUBLIC,  DOING_UNIQUE},
    { NULL,             0,          0,  0}
};

static NAMETAB dolist_sw[] =
{
    {"delimit",         1,  CA_PUBLIC,  DOLIST_DELIMIT},
    {"notify",          1,  CA_PUBLIC,  DOLIST_NOTIFY|SW_MULTIPLE},
    {"space",           1,  CA_PUBLIC,  DOLIST_SPACE},
    { NULL,             0,          0,  0}
};

#ifdef QUERY_SLAVE
static NAMETAB query_sw[] =
{
    {"sql",             1,  CA_PUBLIC,  QUERY_SQL},
    { NULL,             0,          0,  0}
};
#endif // QUERY_SLAVE

static NAMETAB drop_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  DROP_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB dump_sw[] =
{
    {"flatfile",        1,  CA_WIZARD,  DUMP_FLATFILE|SW_MULTIPLE},
    {"structure",       1,  CA_WIZARD,  DUMP_STRUCT|SW_MULTIPLE},
    {"text",            1,  CA_WIZARD,  DUMP_TEXT|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB emit_sw[] =
{
    {"here",            2,  CA_PUBLIC,  SAY_HERE|SW_MULTIPLE},
    {"html",            2,  CA_PUBLIC,  SAY_HTML|SW_MULTIPLE},
    {"room",            1,  CA_PUBLIC,  SAY_ROOM|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB enter_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB examine_sw[] =
{
    {"brief",           1,  CA_PUBLIC,  EXAM_BRIEF},
    {"debug",           1,  CA_WIZARD,  EXAM_DEBUG},
    {"full",            1,  CA_PUBLIC,  EXAM_LONG},
    {"parent",          1,  CA_PUBLIC,  EXAM_PARENT},
    { NULL,             0,          0,  0}
};

static NAMETAB femit_sw[] =
{
    {"here",            1,  CA_PUBLIC,  PEMIT_HERE|SW_MULTIPLE},
    {"room",            1,  CA_PUBLIC,  PEMIT_ROOM|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB fixdb_sw[] =
{
    {"contents",        1,     CA_GOD,  FIXDB_CON},
    {"exits",           1,     CA_GOD,  FIXDB_EXITS},
    {"location",        1,     CA_GOD,  FIXDB_LOC},
    {"next",            1,     CA_GOD,  FIXDB_NEXT},
    {"owner",           1,     CA_GOD,  FIXDB_OWNER},
    {"pennies",         1,     CA_GOD,  FIXDB_PENNIES},
    {"rename",          1,     CA_GOD,  FIXDB_NAME},
    { NULL,             0,          0,  0}
};

static NAMETAB flag_sw[] =
{
    {"remove",          1,     CA_GOD,  FLAG_REMOVE},
    { NULL,             0,          0,  0}
};

static NAMETAB fpose_sw[] =
{
    {"default",         1,  CA_PUBLIC,  0},
    {"nospace",         1,  CA_PUBLIC,  SAY_NOSPACE},
    { NULL,             0,          0,  0}
};

static NAMETAB function_sw[] =
{
    {"list",            1,  CA_WIZARD,  FN_LIST},
    {"preserve",        3,  CA_WIZARD,  FN_PRES|SW_MULTIPLE},
    {"privileged",      3,  CA_WIZARD,  FN_PRIV|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB get_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  GET_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB give_sw[] =
{
    {"quiet",           1,  CA_WIZARD,  GIVE_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB goto_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB halt_sw[] =
{
    {"all",             1,  CA_PUBLIC,  HALT_ALL},
    { NULL,             0,          0,  0}
};

static NAMETAB hook_sw[] =
{
    {"after",           3,     CA_GOD,  HOOK_AFTER},
    {"before",          3,     CA_GOD,  HOOK_BEFORE},
    {"clear",           3,     CA_GOD,  HOOK_CLEAR|SW_MULTIPLE},
    {"fail",            1,     CA_GOD,  HOOK_AFAIL},
    {"ignore",          3,     CA_GOD,  HOOK_IGNORE},
    {"igswitch",        3,     CA_GOD,  HOOK_IGSWITCH},
    {"list",            3,     CA_GOD,  HOOK_LIST},
    {"permit",          3,     CA_GOD,  HOOK_PERMIT},
    {NULL,              0,          0,  0}
};

static NAMETAB icmd_sw[] =
{
    {"check",           2,     CA_GOD,  ICMD_CHECK},
    {"clear",           2,     CA_GOD,  ICMD_CLEAR},
    {"croom",           2,     CA_GOD,  ICMD_CROOM},
    {"disable",         1,     CA_GOD,  ICMD_DISABLE},
    {"droom",           2,     CA_GOD,  ICMD_DROOM},
    {"ignore",          1,     CA_GOD,  ICMD_IGNORE},
    {"iroom",           2,     CA_GOD,  ICMD_IROOM},
    {"lroom",           2,     CA_GOD,  ICMD_LROOM},
    {"lallroom",        2,     CA_GOD,  ICMD_LALLROOM},
    {"off",             2,     CA_GOD,  ICMD_OFF},
    {"on",              2,     CA_GOD,  ICMD_ON},
    {NULL,              0,          0,  0}
};

static NAMETAB leave_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB listmotd_sw[] =
{
    {"brief",           1,  CA_WIZARD,  MOTD_BRIEF},
    { NULL,             0,          0,  0}
};

NAMETAB lock_sw[] =
{
    {"defaultlock",     1,  CA_PUBLIC,  A_LOCK},
    {"droplock",        1,  CA_PUBLIC,  A_LDROP},
    {"enterlock",       1,  CA_PUBLIC,  A_LENTER},
    {"getfromlock",     1,  CA_PUBLIC,  A_LGET},
    {"givelock",        1,  CA_PUBLIC,  A_LGIVE},
    {"leavelock",       2,  CA_PUBLIC,  A_LLEAVE},
    {"linklock",        2,  CA_PUBLIC,  A_LLINK},
    {"maillock",        1,  CA_PUBLIC,  A_LMAIL},
    {"openlock",        1,  CA_PUBLIC,  A_LOPEN},
    {"pagelock",        3,  CA_PUBLIC,  A_LPAGE},
    {"parentlock",      3,  CA_PUBLIC,  A_LPARENT},
    {"receivelock",     1,  CA_PUBLIC,  A_LRECEIVE},
    {"speechlock",      1,  CA_PUBLIC,  A_LSPEECH},
    {"teloutlock",      2,  CA_PUBLIC,  A_LTELOUT},
    {"tportlock",       2,  CA_PUBLIC,  A_LTPORT},
    {"uselock",         1,  CA_PUBLIC,  A_LUSE},
    {"userlock",        4,  CA_PUBLIC,  A_LUSER},
    { NULL,             0,          0,  0}
};

static NAMETAB look_sw[] =
{
    {"outside",         1,  CA_PUBLIC,  LOOK_OUTSIDE},
    { NULL,             0,          0,  0}
};

static NAMETAB mail_sw[] =
{
    {"abort",           2,  CA_PUBLIC,  MAIL_ABORT},
    {"alias",           4,  CA_PUBLIC,  MAIL_ALIAS},
    {"alist",           4,  CA_PUBLIC,  MAIL_ALIST},
    {"bcc",             1,  CA_PUBLIC,  MAIL_BCC},
    {"cc",              2,  CA_PUBLIC,  MAIL_CC},
    {"clear",           2,  CA_PUBLIC,  MAIL_CLEAR},
    {"debug",           2,  CA_PUBLIC,  MAIL_DEBUG},
    {"dstats",          2,  CA_PUBLIC,  MAIL_DSTATS},
    {"edit",            1,  CA_PUBLIC,  MAIL_EDIT},
    {"file",            2,  CA_PUBLIC,  MAIL_FILE},
    {"folder",          3,  CA_PUBLIC,  MAIL_FOLDER},
    {"forward",         3,  CA_PUBLIC,  MAIL_FORWARD},
    {"fstats",          2,  CA_PUBLIC,  MAIL_FSTATS},
    {"fwd",             2,  CA_PUBLIC,  MAIL_FORWARD},
    {"list",            1,  CA_PUBLIC,  MAIL_LIST},
    {"nuke",            1,  CA_PUBLIC,  MAIL_NUKE},
    {"proof",           2,  CA_PUBLIC,  MAIL_PROOF},
    {"purge",           2,  CA_PUBLIC,  MAIL_PURGE},
    {"quick",           3,  CA_PUBLIC,  MAIL_QUICK},
    {"quote",           3,  CA_PUBLIC,  MAIL_QUOTE|SW_MULTIPLE},
    {"read",            3,  CA_PUBLIC,  MAIL_READ},
    {"reply",           3,  CA_PUBLIC,  MAIL_REPLY},
    {"replyall",        6,  CA_PUBLIC,  MAIL_REPLYALL},
    {"retract",         3,  CA_PUBLIC,  MAIL_RETRACT},
    {"review",          3,  CA_PUBLIC,  MAIL_REVIEW},
    {"safe",            2,  CA_PUBLIC,  MAIL_SAFE},
    {"send",            2,  CA_PUBLIC,  MAIL_SEND},
    {"stats",           2,  CA_PUBLIC,  MAIL_STATS},
    {"tag",             1,  CA_PUBLIC,  MAIL_TAG},
    {"unclear",         3,  CA_PUBLIC,  MAIL_UNCLEAR},
    {"untag",           3,  CA_PUBLIC,  MAIL_UNTAG},
    {"urgent",          2,  CA_PUBLIC,  MAIL_URGENT},
    { NULL,             0,          0,  0}
};

static NAMETAB malias_sw[] =
{
    {"add",             1,  CA_PUBLIC,  MALIAS_ADD},
    {"chown",           1,  CA_PUBLIC,  MALIAS_CHOWN},
    {"desc",            1,  CA_PUBLIC,  MALIAS_DESC},
    {"delete",          1,  CA_PUBLIC,  MALIAS_DELETE},
    {"list",            1,  CA_PUBLIC,  MALIAS_LIST},
    {"remove",          1,  CA_PUBLIC,  MALIAS_REMOVE},
    {"rename",          1,  CA_PUBLIC,  MALIAS_RENAME},
    {"status",          1,  CA_PUBLIC,  MALIAS_STATUS},
    { NULL,             0,          0,  0}
};

static NAMETAB mark_sw[] =
{
    {"clear",           1,  CA_PUBLIC,  MARK_CLEAR},
    {"set",             1,  CA_PUBLIC,  MARK_SET},
    { NULL,             0,          0,  0}
};

static NAMETAB markall_sw[] =
{
    {"clear",           1,  CA_PUBLIC,  MARK_CLEAR},
    {"set",             1,  CA_PUBLIC,  MARK_SET},
    { NULL,             0,          0,  0}
};

static NAMETAB motd_sw[] =
{
    {"brief",           1,  CA_WIZARD,  MOTD_BRIEF|SW_MULTIPLE},
    {"connect",         1,  CA_WIZARD,  MOTD_ALL},
    {"down",            1,  CA_WIZARD,  MOTD_DOWN},
    {"full",            1,  CA_WIZARD,  MOTD_FULL},
    {"list",            1,  CA_PUBLIC,  MOTD_LIST},
    {"wizard",          1,  CA_WIZARD,  MOTD_WIZ},
    { NULL,             0,          0,  0}
};

static NAMETAB notify_sw[] =
{
    {"all",             1,  CA_PUBLIC,  NFY_NFYALL},
    {"first",           1,  CA_PUBLIC,  NFY_NFY},
    {"quiet",           1,  CA_PUBLIC,  NFY_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB open_sw[] =
{
    {"inventory",       1,  CA_PUBLIC,  OPEN_INVENTORY},
    {"location",        1,  CA_PUBLIC,  OPEN_LOCATION},
    { NULL,             0,          0,  0}
};

static NAMETAB page_sw[] =
{
    {"noeval",          1,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB pemit_sw[] =
{
    {"contents",        1,  CA_PUBLIC,  PEMIT_CONTENTS|SW_MULTIPLE},
    {"html",            1,  CA_PUBLIC,  PEMIT_HTML|SW_MULTIPLE},
    {"list",            1,  CA_PUBLIC,  PEMIT_LIST|SW_MULTIPLE},
    {"noeval",          1,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {"object",          1,  CA_PUBLIC,  0},
    {"silent",          1,  CA_PUBLIC,  0},
    { NULL,             0,          0,  0}
};

static NAMETAB pose_sw[] =
{
    {"default",         1,  CA_PUBLIC,  0},
    {"noeval",          3,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {"nospace",         3,  CA_PUBLIC,  SAY_NOSPACE},
    { NULL,             0,          0,  0}
};

static NAMETAB ps_sw[] =
{
    {"all",             1,  CA_PUBLIC,  PS_ALL|SW_MULTIPLE},
    {"brief",           1,  CA_PUBLIC,  PS_BRIEF},
    {"long",            1,  CA_PUBLIC,  PS_LONG},
    {"summary",         1,  CA_PUBLIC,  PS_SUMM},
    { NULL,             0,          0,  0}
};

static NAMETAB quota_sw[] =
{
    {"all",             1,  CA_GOD,     QUOTA_ALL|SW_MULTIPLE},
    {"fix",             1,  CA_WIZARD,  QUOTA_FIX},
    {"remaining",       1,  CA_WIZARD,  QUOTA_REM|SW_MULTIPLE},
    {"set",             1,  CA_WIZARD,  QUOTA_SET},
    {"total",           1,  CA_WIZARD,  QUOTA_TOT|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB say_sw[] =
{
    {"noeval",          1,  CA_PUBLIC,  SAY_NOEVAL|SW_NOEVAL|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB set_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  SET_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB stats_sw[] =
{
    {"all",             1,  CA_PUBLIC,  STAT_ALL},
    {"me",              1,  CA_PUBLIC,  STAT_ME},
    {"player",          1,  CA_PUBLIC,  STAT_PLAYER},
    { NULL,             0,          0,  0}
};

static NAMETAB sweep_sw[] =
{
    {"commands",        3,  CA_PUBLIC,  SWEEP_COMMANDS|SW_MULTIPLE},
    {"connected",       3,  CA_PUBLIC,  SWEEP_CONNECT|SW_MULTIPLE},
    {"exits",           1,  CA_PUBLIC,  SWEEP_EXITS|SW_MULTIPLE},
    {"here",            1,  CA_PUBLIC,  SWEEP_HERE|SW_MULTIPLE},
    {"inventory",       1,  CA_PUBLIC,  SWEEP_ME|SW_MULTIPLE},
    {"listeners",       1,  CA_PUBLIC,  SWEEP_LISTEN|SW_MULTIPLE},
    {"players",         1,  CA_PUBLIC,  SWEEP_PLAYER|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB switch_sw[] =
{
    {"all",             1,  CA_PUBLIC,  SWITCH_ANY},
    {"default",         1,  CA_PUBLIC,  SWITCH_DEFAULT},
    {"first",           1,  CA_PUBLIC,  SWITCH_ONE},
    { NULL,             0,          0,  0}
};

static NAMETAB teleport_sw[] =
{
    {"list",            1,  CA_PUBLIC,  TELEPORT_LIST|SW_MULTIPLE},
    {"loud",            1,  CA_PUBLIC,  TELEPORT_DEFAULT},
    {"quiet",           1,  CA_PUBLIC,  TELEPORT_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB timecheck_sw[] =
{
    {"log",             1,  CA_WIZARD,  TIMECHK_LOG | SW_MULTIPLE},
    {"reset",           1,  CA_WIZARD,  TIMECHK_RESET | SW_MULTIPLE},
    {"screen",          1,  CA_WIZARD,  TIMECHK_SCREEN | SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB toad_sw[] =
{
    {"no_chown",        1,  CA_WIZARD,  TOAD_NO_CHOWN|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB trig_sw[] =
{
    {"quiet",           1,  CA_PUBLIC,  TRIG_QUIET},
    { NULL,             0,          0,  0}
};

static NAMETAB wait_sw[] =
{
    {"until",           1,  CA_PUBLIC, WAIT_UNTIL},
    { NULL,             0,          0,  0}
};

static NAMETAB wall_sw[] =
{
    {"admin",           1,  CA_ADMIN,    SHOUT_ADMINSHOUT},
    {"emit",            1,  CA_ANNOUNCE, SHOUT_WALLEMIT},
    {"no_prefix",       1,  CA_ANNOUNCE, SAY_NOTAG|SW_MULTIPLE},
    {"pose",            1,  CA_ANNOUNCE, SHOUT_WALLPOSE},
    {"wizard",          1,  CA_ANNOUNCE, SHOUT_WIZSHOUT|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};

static NAMETAB warp_sw[] =
{
    {"check",           1,  CA_WIZARD,  TWARP_CLEAN|SW_MULTIPLE},
    {"dump",            1,  CA_WIZARD,  TWARP_DUMP|SW_MULTIPLE},
    {"events",          1,  CA_WIZARD,  TWARP_EVENTS|SW_MULTIPLE},
    {"idle",            1,  CA_WIZARD,  TWARP_IDLE|SW_MULTIPLE},
    {"queue",           1,  CA_WIZARD,  TWARP_QUEUE|SW_MULTIPLE},
    { NULL,             0,          0,  0}
};


/* ---------------------------------------------------------------------------
 * Command table: Definitions for builtin commands, used to build the command
 * hash table.
 *
 * Format:  Name        Switches    Permissions Needed
 *  Key (if any)    Calling Seq         Handler
 */
static CMDENT_NO_ARG command_table_no_arg[] =
{
    {"@@",          NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_comment},
    {"@backup",     NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, do_backup},
    {"@dbck",       dbck_sw,    CA_WIZARD,   0,          CS_NO_ARGS, 0, do_dbck},
    {"@dbclean",    NULL,       CA_GOD,      0,          CS_NO_ARGS, 0, do_dbclean},
    {"@dump",       dump_sw,    CA_WIZARD,   0,          CS_NO_ARGS, 0, do_dump},
    {"@mark_all",   markall_sw, CA_WIZARD,   MARK_SET,   CS_NO_ARGS, 0, do_markall},
    {"@readcache",  NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, do_readcache},
    {"@restart",    NULL,       CA_NO_GUEST|CA_NO_SLAVE, 0, CS_NO_ARGS, 0, do_restart},
#ifndef WIN32
    {"@startslave", NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, boot_slave},
#endif // !WIN32
    {"@timecheck",  timecheck_sw, CA_WIZARD, 0,          CS_NO_ARGS, 0, do_timecheck},
    {"clearcom",    NULL,       CA_NO_SLAVE, 0,          CS_NO_ARGS, 0, do_clearcom},
    {"info",        NULL,       CA_PUBLIC,   CMD_INFO,   CS_NO_ARGS, 0, logged_out0},
    {"inventory",   NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_inventory},
    {"leave",       leave_sw,   CA_LOCATION, 0,          CS_NO_ARGS, 0, do_leave},
    {"logout",      NULL,       CA_PUBLIC,   CMD_LOGOUT, CS_NO_ARGS, 0, logged_out0},
    {"quit",        NULL,       CA_PUBLIC,   CMD_QUIT,   CS_NO_ARGS, 0, logged_out0},
    {"report",      NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_report},
    {"score",       NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_score},
    {"version",     NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_version},
    {NULL,          NULL,       0,           0,          0,          0, NULL}
};

static CMDENT_ONE_ARG command_table_one_arg[] =
{
    {"@boot",         boot_sw,    CA_NO_GUEST|CA_NO_SLAVE,    0,  CS_ONE_ARG|CS_INTERP, 0, do_boot},
    {"@break",        NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_break},
    {"@ccreate",      NULL,       CA_NO_SLAVE|CA_NO_GUEST,    0,  CS_ONE_ARG,           0, do_createchannel},
    {"@cdestroy",     NULL,       CA_NO_SLAVE|CA_NO_GUEST,    0,  CS_ONE_ARG,           0, do_destroychannel},
    {"@clist",        clist_sw,   CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_chanlist},
    {"@cut",          NULL,       CA_WIZARD|CA_LOCATION,      0,  CS_ONE_ARG|CS_INTERP, 0, do_cut},
    {"@cwho",         NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_channelwho},
    {"@destroy",      destroy_sw, CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, DEST_ONE,   CS_ONE_ARG|CS_INTERP,   0, do_destroy},
    {"@disable",      NULL,       CA_WIZARD,       GLOB_DISABLE,  CS_ONE_ARG,           0, do_global},
    {"@doing",        doing_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_doing},
    {"@emit",         emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,  SAY_EMIT,   CS_ONE_ARG|CS_INTERP,   0, do_say},
    {"@enable",       NULL,       CA_WIZARD,        GLOB_ENABLE,  CS_ONE_ARG,           0, do_global},
    {"@entrances",    NULL,       CA_NO_GUEST,                0,  CS_ONE_ARG|CS_INTERP, 0, do_entrances},
    {"@find",         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_find},
    {"@halt",         halt_sw,    CA_NO_SLAVE,                0,  CS_ONE_ARG|CS_INTERP, 0, do_halt},
    {"@hook",         hook_sw,    CA_GOD,                     0,  CS_ONE_ARG|CS_INTERP, 0, do_hook},
    {"@kick",         NULL,       CA_WIZARD,         QUEUE_KICK,  CS_ONE_ARG|CS_INTERP, 0, do_queue},
    {"@last",         NULL,       CA_NO_GUEST,                0,  CS_ONE_ARG|CS_INTERP, 0, do_last},
    {"@list",         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_list},
    {"@list_file",    NULL,       CA_WIZARD,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_list_file},
    {"@listcommands", NULL,       CA_GOD,                     0,  CS_ONE_ARG,           0, do_listcommands},
    {"@listmotd",     listmotd_sw,CA_PUBLIC,          MOTD_LIST,  CS_ONE_ARG,           0, do_motd},
    {"@mark",         mark_sw,    CA_WIZARD,          SRCH_MARK,  CS_ONE_ARG|CS_NOINTERP,   0, do_search},
    {"@motd",         motd_sw,    CA_WIZARD,                  0,  CS_ONE_ARG,           0, do_motd},
    {"@nemit",        emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE, SAY_EMIT, CS_ONE_ARG|CS_UNPARSE|CS_NOSQUISH, 0, do_say},
    {"@poor",         NULL,       CA_GOD,                     0,  CS_ONE_ARG|CS_INTERP, 0, do_poor},
    {"@ps",           ps_sw,      CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_ps},
    {"@quitprogram",  NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_quitprog},
    {"@search",       NULL,       CA_PUBLIC,        SRCH_SEARCH,  CS_ONE_ARG|CS_NOINTERP,   0, do_search},
    {"@shutdown",     NULL,       CA_NO_GUEST|CA_NO_SLAVE,    0,  CS_ONE_ARG,           0, do_shutdown},
    {"@stats",        stats_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_stats},
    {"@sweep",        sweep_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_sweep},
    {"@timewarp",     warp_sw,    CA_WIZARD,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_timewarp},
    {"@unlink",       NULL,       CA_NO_SLAVE|CA_GBL_BUILD,   0,  CS_ONE_ARG|CS_INTERP, 0, do_unlink},
    {"@unlock",       lock_sw,    CA_NO_SLAVE,                0,  CS_ONE_ARG|CS_INTERP, 0, do_unlock},
    {"@wall",         wall_sw,    CA_ANNOUNCE,      SHOUT_SHOUT,  CS_ONE_ARG|CS_INTERP, 0, do_shout},
    {"@wipe",         NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_ONE_ARG|CS_INTERP,   0, do_wipe},
    {"allcom",        NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_allcom},
    {"comlist",       NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_comlist},
    {"delcom",        NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_delcom},
    {"doing",         NULL,       CA_PUBLIC,          CMD_DOING,  CS_ONE_ARG,           0, logged_out1},
    {"drop",          drop_sw,    CA_NO_SLAVE|CA_CONTENTS|CA_LOCATION|CA_NO_GUEST,  0,  CS_ONE_ARG|CS_INTERP,   0, do_drop},
    {"enter",         enter_sw,   CA_LOCATION,                0,  CS_ONE_ARG|CS_INTERP, 0, do_enter},
    {"examine",       examine_sw, CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_examine},
    {"get",           get_sw,     CA_LOCATION|CA_NO_GUEST,    0,  CS_ONE_ARG|CS_INTERP, 0, do_get},
    {"goto",          goto_sw,    CA_LOCATION,                0,  CS_ONE_ARG|CS_INTERP, 0, do_move},
    {"look",          look_sw,    CA_LOCATION,        LOOK_LOOK,  CS_ONE_ARG|CS_INTERP, 0, do_look},
    {"outputprefix",  NULL,       CA_PUBLIC,         CMD_PREFIX,  CS_ONE_ARG,           0, logged_out1},
    {"outputsuffix",  NULL,       CA_PUBLIC,         CMD_SUFFIX,  CS_ONE_ARG,           0, logged_out1},
    {"pose",          pose_sw,    CA_LOCATION|CA_NO_SLAVE,  SAY_POSE,   CS_ONE_ARG|CS_INTERP,   0, do_say},
    {"puebloclient",  NULL,       CA_PUBLIC,   CMD_PUEBLOCLIENT,  CS_ONE_ARG,           0, logged_out1},
    {"say",           say_sw,     CA_LOCATION|CA_NO_SLAVE,  SAY_SAY,    CS_ONE_ARG|CS_INTERP,   0, do_say},
    {"session",       NULL,       CA_PUBLIC,        CMD_SESSION,  CS_ONE_ARG,           0, logged_out1},
    {"think",         NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_think},
    {"train",         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_train},
    {"use",           NULL,       CA_NO_SLAVE|CA_GBL_INTERP,  0,  CS_ONE_ARG|CS_INTERP, 0, do_use},
    {"who",           NULL,       CA_PUBLIC,            CMD_WHO,  CS_ONE_ARG,           0, logged_out1},
    {"\\",            NULL,       CA_NO_GUEST|CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP,   0, do_say},
    {":",             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {";",             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {"\"",            NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {"-",             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,  0,  CS_ONE_ARG|CS_LEADIN,   0, do_postpend},
    {"~",             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,  0,  CS_ONE_ARG|CS_LEADIN,   0, do_prepend},
    {NULL,            NULL,       0,                          0,    0,                  0, NULL}
};

static CMDENT_ONE_ARG_CMDARG command_table_one_arg_cmdarg[] =
{
    {"@apply_marked", NULL,       CA_WIZARD|CA_GBL_INTERP,    0,      CS_ONE_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND,   0, do_apply_marked},
    {"#",             NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CF_DARK,    0,      CS_ONE_ARG|CS_INTERP|CS_CMDARG, 0, do_force_prefixed},
    {NULL,            NULL,       0,     0,      0,             0,  NULL}
};

static CMDENT_TWO_ARG command_table_two_arg[] =
{
    {"@addcommand",  NULL,       CA_GOD,                                           0,           CS_TWO_ARG,           0, do_addcommand},
    {"@admin",       NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_admin},
    {"@alias",       NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          0,           CS_TWO_ARG,           0, do_alias},
    {"@attribute",   attrib_sw,  CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_attribute},
    {"@cboot",       cboot_sw,   CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_chboot},
    {"@ccharge",     NULL,       CA_NO_SLAVE|CA_NO_GUEST,                          1,           CS_TWO_ARG,           0, do_editchannel},
    {"@cchown",      NULL,       CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_editchannel},
    {"@cemit",       cemit_sw,   CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_cemit},
    {"@chown",       NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,             CHOWN_ONE,   CS_TWO_ARG|CS_INTERP, 0, do_chown},
    {"@chownall",    NULL,       CA_WIZARD|CA_GBL_BUILD,                           CHOWN_ALL,   CS_TWO_ARG|CS_INTERP, 0, do_chownall},
    {"@chzone",      NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,             0,           CS_TWO_ARG|CS_INTERP, 0, do_chzone},
    {"@clone",       clone_sw,   CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST, 0,           CS_TWO_ARG|CS_INTERP, 0, do_clone},
    {"@coflags",     NULL,       CA_NO_SLAVE,                                      4,           CS_TWO_ARG,           0, do_editchannel},
    {"@cpflags",     NULL,       CA_NO_SLAVE,                                      3,           CS_TWO_ARG,           0, do_editchannel},
    {"@create",      NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST, 0,           CS_TWO_ARG|CS_INTERP, 0, do_create},
    {"@cset",        cset_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_chopen},
    {"@decompile",   decomp_sw,  CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_decomp},
    {"@delcommand",  NULL,       CA_GOD,                                           0,           CS_TWO_ARG,           0, do_delcommand},
    {"@drain",       NULL,       CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,            NFY_DRAIN,   CS_TWO_ARG,           0, do_notify},
    {"@femit",       femit_sw,   CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,              PEMIT_FEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"@fixdb",       fixdb_sw,   CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_fixdb},
    {"@flag",        flag_sw,    CA_GOD,                                           0,           CS_TWO_ARG,           0, do_flag},
    {"@forwardlist", NULL,       CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_forwardlist},
    {"@fpose",       fpose_sw,   CA_LOCATION|CA_NO_SLAVE,                          PEMIT_FPOSE, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"@fsay",        NULL,       CA_LOCATION|CA_NO_SLAVE,                          PEMIT_FSAY,  CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"@function",    function_sw,CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_function},
    {"@link",        NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG|CS_INTERP, 0, do_link},
    {"@lock",        lock_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_lock},
    {"@log",         NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG,           0, do_log},
    {"@mail",        mail_sw,    CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_mail},
    {"@malias",      malias_sw,  CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_malias},
    {"@moniker",     NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_moniker},
    {"@name",        NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG|CS_INTERP, 0, do_name},
    {"@newpassword", NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG,           0, do_newpassword},
    {"@notify",      notify_sw,  CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,            0,           CS_TWO_ARG,           0, do_notify},
    {"@npemit",      pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_PEMIT, CS_TWO_ARG|CS_UNPARSE|CS_NOSQUISH, 0, do_pemit},
    {"@oemit",       NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_OEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"@parent",      NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG,           0, do_parent},
    {"@password",    NULL,       CA_NO_GUEST,                                      0,           CS_TWO_ARG,           0, do_password},
    {"@pcreate",     NULL,       CA_WIZARD|CA_GBL_BUILD,                           PCRE_PLAYER, CS_TWO_ARG,           0, do_pcreate},
    {"@pemit",       pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_PEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"@power",       NULL,       CA_PUBLIC,                                        0,           CS_TWO_ARG,           0, do_power},
    {"@program",     NULL,       CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_prog},
    {"@quota",       quota_sw,   CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_quota},
    {"@robot",       NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST|CA_PLAYER,   PCRE_ROBOT,  CS_TWO_ARG,           0, do_pcreate},
#ifdef REALITY_LVLS
    {"@rxlevel",    NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_rxlevel},
#endif
    {"@set",         set_sw,     CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG,           0, do_set},
    {"@teleport",    teleport_sw,CA_NO_GUEST,                                      TELEPORT_DEFAULT, CS_TWO_ARG|CS_INTERP, 0, do_teleport},
#ifdef REALITY_LVLS
    {"@txlevel",    NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_txlevel},
#endif
    {"@toad",        toad_sw,    CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_toad},
    {"addcom",       NULL,       CA_NO_SLAVE,                                      0,           CS_TWO_ARG,           0, do_addcom},
    {"comtitle",     comtitle_sw,CA_NO_SLAVE,                                      0,           CS_TWO_ARG,           0, do_comtitle},
    {"give",         give_sw,    CA_LOCATION|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_give},
    {"kill",         NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          KILL_KILL,   CS_TWO_ARG|CS_INTERP, 0, do_kill},
    {"page",         page_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_page},
    {"slay",         NULL,       CA_WIZARD,                                        KILL_SLAY,   CS_TWO_ARG|CS_INTERP, 0, do_kill},
    {"whisper",      NULL,       CA_LOCATION|CA_NO_SLAVE,                          PEMIT_WHISPER, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {"&",            NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,                  0,           CS_TWO_ARG|CS_LEADIN, 0, do_setvattr},
    {NULL,           NULL,       0,                                                0,           0,                    0, NULL}
};

static CMDENT_TWO_ARG_ARGV command_table_two_arg_argv[] =
{
    {"@cpattr",     NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV,             0, do_cpattr},
    {"@dig",        dig_sw,     CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_dig},
    {"@edit",       NULL,       CA_NO_SLAVE|CA_NO_GUEST,              0,  CS_TWO_ARG|CS_ARGV|CS_STRIP_AROUND, 0, do_edit},
    {"@icmd",       icmd_sw,    CA_GOD,                               0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_icmd},
    {"@mvattr",     NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV,             0, do_mvattr},
    {"@open",       open_sw,    CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST, 0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_open},
    {"@trigger",    trig_sw,    CA_GBL_INTERP,                        0,  CS_TWO_ARG|CS_ARGV,             0, do_trigger},
    {"@verb",       NULL,       CA_GBL_INTERP|CA_NO_SLAVE,            0,  CS_TWO_ARG|CS_ARGV|CS_INTERP|CS_STRIP_AROUND, 0, do_verb},
    {NULL,          NULL,       0,                                    0,  0,              0, NULL}
};

static CMDENT_TWO_ARG_CMDARG command_table_two_arg_cmdarg[] =
{
    {"@dolist", dolist_sw,  CA_GBL_INTERP,  0,      CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_dolist},
    {"@force",  NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CA_NO_GUEST,    0,    CS_TWO_ARG|CS_INTERP|CS_CMDARG, 0, do_force},
#ifdef QUERY_SLAVE
    {"@query",  query_sw,   CA_WIZARD,      0,      CS_TWO_ARG|CS_INTERP|CS_CMDARG,                   0, do_query},
#endif
    {"@wait",   wait_sw,    CA_GBL_INTERP,  0,      CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_wait},
    {NULL,      NULL,       0,              0,      0,              0, NULL}
};

static CMDENT_TWO_ARG_ARGV_CMDARG command_table_two_arg_argv_cmdarg[] =
{
    {"@if",     NULL,       CA_GBL_INTERP,  0,  CS_TWO_ARG|CS_ARGV|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_if},
    {"@switch", switch_sw,  CA_GBL_INTERP,  0,  CS_TWO_ARG|CS_ARGV|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_switch},
    {NULL,      NULL,       0,              0,  0,                                                        0, NULL}
};

static CMDENT *prefix_cmds[256];

static CMDENT *goto_cmdp;

void commands_no_arg_add(CMDENT_NO_ARG cmdent[])
{
    CMDENT_NO_ARG *cp0a;
    for (cp0a = cmdent; cp0a->cmdname; cp0a++)
    {
        if (!hashfindLEN(cp0a->cmdname, strlen(cp0a->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp0a->cmdname, strlen(cp0a->cmdname), cp0a,
                       &mudstate.command_htab);
        }
    }
}

void commands_one_arg_add(CMDENT_ONE_ARG cmdent[])
{
    CMDENT_ONE_ARG *cp1a;
    for (cp1a = cmdent; cp1a->cmdname; cp1a++)
    {
        if (!hashfindLEN(cp1a->cmdname, strlen(cp1a->cmdname),
                        &mudstate.command_htab))
        {
            hashaddLEN(cp1a->cmdname, strlen(cp1a->cmdname), cp1a,
                       &mudstate.command_htab);
        }
    }
}

void commands_one_arg_cmdarg_add(CMDENT_ONE_ARG_CMDARG cmdent[])
{
    CMDENT_ONE_ARG_CMDARG *cp1ac;
    for (cp1ac = cmdent; cp1ac->cmdname; cp1ac++)
    {
        if (!hashfindLEN(cp1ac->cmdname, strlen(cp1ac->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp1ac->cmdname, strlen(cp1ac->cmdname), cp1ac,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_add(CMDENT_TWO_ARG cmdent[])
{
    CMDENT_TWO_ARG *cp2a;
    for (cp2a = cmdent; cp2a->cmdname; cp2a++)
    {
        if (!hashfindLEN(cp2a->cmdname, strlen(cp2a->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2a->cmdname, strlen(cp2a->cmdname), cp2a,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_argv_add(CMDENT_TWO_ARG_ARGV cmdent[])
{
    CMDENT_TWO_ARG_ARGV *cp2aa;
    for (cp2aa = cmdent; cp2aa->cmdname; cp2aa++)
    {
        if (!hashfindLEN(cp2aa->cmdname, strlen(cp2aa->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2aa->cmdname, strlen(cp2aa->cmdname), cp2aa,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_cmdarg_add(CMDENT_TWO_ARG_CMDARG cmdent[])
{
    CMDENT_TWO_ARG_CMDARG  *cp2ac;
    for (cp2ac = cmdent; cp2ac->cmdname; cp2ac++)
    {
        if (!hashfindLEN(cp2ac->cmdname, strlen(cp2ac->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2ac->cmdname, strlen(cp2ac->cmdname), cp2ac,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_argv_cmdarg_add(CMDENT_TWO_ARG_ARGV_CMDARG cmdent[])
{
    CMDENT_TWO_ARG_ARGV_CMDARG  *cp2aac;
    for (cp2aac = cmdent; cp2aac->cmdname; cp2aac++)
    {
        if (!hashfindLEN(cp2aac->cmdname, strlen(cp2aac->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2aac->cmdname, strlen(cp2aac->cmdname), cp2aac,
                       &mudstate.command_htab);
        }
    }
}

void init_cmdtab(void)
{
    ATTR *ap;

    // Load attribute-setting commands.
    //
    for (ap = attr; ap->name; ap++)
    {
        if (ap->flags & AF_NOCMD)
        {
            continue;
        }

        int nBuffer;
        bool bValid;
        char *cbuff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        CMDENT_TWO_ARG *cp2a;
        cp2a = (CMDENT_TWO_ARG *)MEMALLOC(sizeof(CMDENT_TWO_ARG));
        ISOUTOFMEMORY(cp2a);
        cp2a->cmdname = StringClone(cbuff);
        cp2a->perms = CA_NO_GUEST | CA_NO_SLAVE;
        cp2a->switches = NULL;
        if (ap->flags & (AF_WIZARD | AF_MDARK))
        {
            cp2a->perms |= CA_WIZARD;
        }
        cp2a->extra = ap->number;
        cp2a->callseq = CS_TWO_ARG;
        cp2a->hookmask = 0;
        cp2a->handler = do_setattr;
        hashaddLEN(cp2a->cmdname, nBuffer, cp2a, &mudstate.command_htab);
    }

    // Load the builtin commands
    //
    commands_no_arg_add(command_table_no_arg);
    commands_one_arg_add(command_table_one_arg);
    commands_one_arg_cmdarg_add(command_table_one_arg_cmdarg);
    commands_two_arg_add(command_table_two_arg);
    commands_two_arg_argv_add(command_table_two_arg_argv);
    commands_two_arg_cmdarg_add(command_table_two_arg_cmdarg);
    commands_two_arg_argv_cmdarg_add(command_table_two_arg_argv_cmdarg);

    set_prefix_cmds();

    goto_cmdp = (CMDENT *) hashfindLEN((char *)"goto", strlen("goto"), &mudstate.command_htab);
}

/*! \brief Fills in the table of single-character prefix commands.
 *
 * Command entries for known prefix commands (<code>" : ; \ # & - ~</code>)
 * are copied from the regular command table. Entries for all other starting
 * characters are set to \c NULL.
 *
 * \return         None.
 */
void set_prefix_cmds()
{
    for (int i = 0; i < 256; i++)
    {
        prefix_cmds[i] = NULL;
    }

#define SET_PREFIX_CMD(s) prefix_cmds[(unsigned char)(s)[0]] = \
        (CMDENT *) hashfindLEN((char *)(s), 1, &mudstate.command_htab)
    SET_PREFIX_CMD("\"");
    SET_PREFIX_CMD(":");
    SET_PREFIX_CMD(";");
    SET_PREFIX_CMD("\\");
    SET_PREFIX_CMD("#");
    SET_PREFIX_CMD("&");
    SET_PREFIX_CMD("-");
    SET_PREFIX_CMD("~");
#undef SET_PREFIX_CMD
}

// ---------------------------------------------------------------------------
// check_access: Check if player has access to function.
//
bool check_access(dbref player, int mask)
{
    if (mask & (CA_DISABLED|CA_STATIC))
    {
        return false;
    }
    if (  God(player)
       || mudstate.bReadingConfiguration)
    {
        return true;
    }

    if (mask & CA_MUSTBE_MASK)
    {
        // Since CA_GOD by itself is a frequent case, for the sake of
        // performance, we test CA_GOD specifically. If CA_GOD were ever
        // combined with anything, it would be passed through to the general
        // case.
        //
        if ((mask & CA_MUSTBE_MASK) == CA_GOD)
        {
            return false;
        }

        // Since God(player) is always false here, CA_GOD is still handled by
        // the following code even though it doesn't appear in any of the
        // cases explicitly.  CA_WIZARD by itself is also a common case, but
        // since we have have a bit (mask & CA_MUSTBE_MASK), and since that
        // bit is not a lone CA_GOD bit (handled above), and since CA_WIZARD
        // it tested first below, it doesn't make sense to test CA_WIZARD
        // as a special case.
        //
        if (!(  ((mask & CA_WIZARD)   && Wizard(player))
             || ((mask & CA_ADMIN)    && WizRoy(player))
             || ((mask & CA_BUILDER)  && Builder(player))
             || ((mask & CA_STAFF)    && Staff(player))
             || ((mask & CA_HEAD)     && Head(player))
             || ((mask & CA_ANNOUNCE) && Announce(player))
             || ((mask & CA_IMMORTAL) && Immortal(player))
             || ((mask & CA_UNINS)    && Uninspected(player))
             || ((mask & CA_ROBOT)    && Robot(player))))
        {
            return false;
        }
    }

    // Check for forbidden flags.
    //
    if (  (mask & CA_CANTBE_MASK)
       && !Wizard(player))
    {
        if (  ((mask & CA_NO_HAVEN)   && Player_haven(player))
           || ((mask & CA_NO_ROBOT)   && Robot(player))
           || ((mask & CA_NO_SLAVE)   && Slave(player))
           || ((mask & CA_NO_SUSPECT) && Suspect(player))
           || ((mask & CA_NO_GUEST)   && Guest(player))
           || ((mask & CA_NO_UNINS)   && Uninspected(player)))
        {
            return false;
        }
    }
    return true;
}

/*****************************************************************************
 * Process the various hook calls.
 * Idea taken from TinyMUSH3, code from RhostMUSH, ported by Jake Nelson.
 * Hooks processed:  before, after, ignore, permit, fail
 *****************************************************************************/
static bool process_hook(dbref executor, dbref thing, char *s_uselock, ATTR *hk_attr,
                  bool save_flg)
{
    UNUSED_PARAMETER(s_uselock);

    bool retval = true;
    if (hk_attr)
    {
        dbref aowner;
        int aflags;
        int anum = hk_attr->number;
        char *atext = atr_get(thing, anum, &aowner, &aflags);
        if (atext[0] && !(aflags & AF_NOPROG))
        {
            char **preserve = NULL;
            int *preserve_len = NULL;
            if (save_flg)
            {
                preserve = PushPointers(MAX_GLOBAL_REGS);
                preserve_len = PushIntegers(MAX_GLOBAL_REGS);
                save_global_regs("process_hook.save", preserve, preserve_len);
            }
            char *buff, *bufc;
            bufc = buff = alloc_lbuf("process_hook");
            char *str = atext;
            mux_exec(buff, &bufc, thing, executor, executor, EV_FCHECK | EV_EVAL, &str,
                (char **)NULL, 0);
            free_lbuf(atext);
            *bufc = '\0';
            if (save_flg)
            {
                restore_global_regs("process_hook.save", preserve, preserve_len);
                PopIntegers(preserve_len, MAX_GLOBAL_REGS);
                PopPointers(preserve, MAX_GLOBAL_REGS);
            }
            retval = xlate(buff);
            free_lbuf(buff);
        }
    }
    return retval;
}

static char *hook_name(char *pCommand, int key)
{
    char *keylet;
    switch (key)
    {
    case HOOK_AFAIL:
        keylet = "AF";
        break;
    case HOOK_AFTER:
        keylet = "A";
        break;
    case HOOK_BEFORE:
        keylet = "B";
        break;
    case HOOK_IGNORE:
        keylet = "I";
        break;
    case HOOK_PERMIT:
        keylet = "P";
        break;
    default:
        return NULL;
    }

    const char *cmdName = pCommand;
    if (  pCommand[0]
       && !pCommand[1])
    {
        switch (pCommand[0])
        {
        case '"' : cmdName = "say";    break;
        case ':' :
        case ';' : cmdName = "pose";   break;
        case '\\': cmdName = "@emit";  break;
        case '#' : cmdName = "@force"; break;
        case '&' : cmdName = "@set";   break;
        case '-' : cmdName = "@mail";  break;
        case '~' : cmdName = "@mail";  break;
        }
    }

    char *s_uselock = alloc_sbuf("command_hook.hookname");
    mux_sprintf(s_uselock, SBUF_SIZE, "%s_%s", keylet, cmdName);
    return s_uselock;
}

/* ---------------------------------------------------------------------------
 * process_cmdent: Perform indicated command with passed args.
 */

static void process_cmdent(CMDENT *cmdp, char *switchp, dbref executor, dbref caller,
            dbref enactor, bool interactive, char *arg, char *unp_command,
            char *cargs[], int ncargs)
{
    // Perform object type checks.
    //
    if (Invalid_Objtype(executor))
    {
        notify(executor, "Command incompatible with executor type.");
        return;
    }

    // Check if we have permission to execute the command.
    //
    if (!check_access(executor, cmdp->perms))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    // Check global flags
    //
    if (  !Builder(executor)
       && Protect(CA_GBL_BUILD)
       && !(mudconf.control_flags & CF_BUILD))
    {
        notify(executor, "Sorry, building is not allowed now.");
        return;
    }
    if (Protect(CA_GBL_INTERP) && !(mudconf.control_flags & CF_INTERP))
    {
        notify(executor, "Sorry, queueing and triggering are not allowed now.");
        return;
    }

    char *buf1, *buf2, tchar, *bp, *str, *buff, *s, *j, *new0, *s_uselock;
    char *args[MAX_ARG];
    int nargs, i, interp, key, xkey, aflags;
    dbref aowner;
    char *aargs[NUM_ENV_VARS];
    ADDENT *add;
    ATTR *hk_ap2;

    key = cmdp->extra & ~SW_MULTIPLE;
    if (key & SW_GOT_UNIQUE)
    {
        i = 1;
        key = key & ~SW_GOT_UNIQUE;
    }
    else
    {
        i = 0;
    }

    // Check command switches.  Note that there may be more than one,
    // and that we OR all of them together along with the extra value
    // from the command table to produce the key value in the handler
    // call.
    //
    if (switchp && cmdp->switches)
    {
        do
        {
            buf1 = strchr(switchp, '/');
            if (buf1)
            {
                *buf1++ = '\0';
            }
            if (!search_nametab(executor, cmdp->switches, switchp, &xkey))
            {
                if (xkey == -1)
                {
                    notify(executor,
                       tprintf("Unrecognized switch '%s' for command '%s'.",
                       switchp, cmdp->cmdname));
                    return;
                }
                else if (xkey == -2)
                {
                    notify(executor, NOPERM_MESSAGE);
                    return;
                }
            }
            else if (!(xkey & SW_MULTIPLE))
            {
                if (i == 1)
                {
                    notify(executor, "Illegal combination of switches.");
                    return;
                }
                i = 1;
            }
            else
            {
                xkey &= ~SW_MULTIPLE;
            }
            key |= xkey;
            switchp = buf1;
        } while (buf1);
    }
    else if (switchp && !(cmdp->callseq & CS_ADDED))
    {
        notify(executor, tprintf("Command %s does not take switches.",
            cmdp->cmdname));
        return;
    }

    // 'Before' hooks.
    // @hook idea from TinyMUSH 3, code from RhostMUSH. Ported by Jake Nelson.
    //
    if (  (cmdp->hookmask & HOOK_BEFORE)
       && Good_obj(mudconf.hook_obj)
       && !Going(mudconf.hook_obj))
    {
        s_uselock = hook_name(cmdp->cmdname, HOOK_BEFORE);
        hk_ap2 = atr_str(s_uselock);
        process_hook(executor, mudconf.hook_obj, s_uselock, hk_ap2, false);
        free_sbuf(s_uselock);
    }

    // We are allowed to run the command.  Now, call the handler using
    // the appropriate calling sequence and arguments.
    //
    //if ((cmdp->callseq & CS_INTERP) && (key & SW_NOEVAL))
    if (key & SW_NOEVAL)
    {
        // The user specified /noeval on a @pemit or a @npemit,
        // just do EV_STRIP_CURLY and remove the SW_NOEVAL from the
        // 'key'.
        //
        interp = EV_STRIP_CURLY;
        key &= ~SW_NOEVAL;
    }
    else if (  (cmdp->callseq & CS_INTERP)
            || !( interactive
               || (cmdp->callseq & CS_NOINTERP)))
    {
        // If the command is interpreted, or we're interactive (and
        // the command isn't specified CS_NOINTERP), eval the args.
        //
        interp = EV_EVAL | EV_STRIP_CURLY;
    }
    else if (cmdp->callseq & CS_STRIP)
    {
        interp = EV_STRIP_CURLY;
    }
    else if (cmdp->callseq & CS_STRIP_AROUND)
    {
        interp = EV_STRIP_AROUND;
    }
    else
    {
        interp = 0;
    }

    int nargs2;
    switch (cmdp->callseq & CS_NARG_MASK)
    {
    case CS_NO_ARGS: // <cmd>   (no args)
        (*(((CMDENT_NO_ARG *)cmdp)->handler))(executor, caller, enactor, key);
        break;

    case CS_ONE_ARG:    // <cmd> <arg>

        // If an unparsed command, just give it to the handler
        //
#if 0
        // This never happens.
        //
        if (cmdp->callseq & CS_UNPARSE)
        {
            (*(((CMDENT_ONE_ARG *)cmdp)->handler))(executor, unp_command);
            break;
        }
#endif
        // Interpret if necessary, but not twice for CS_ADDED.
        //
        if ((interp & EV_EVAL) && !(cmdp->callseq & CS_ADDED))
        {
            buf1 = bp = alloc_lbuf("process_cmdent");
            str = arg;
            mux_exec(buf1, &bp, executor, caller, enactor,
                interp | EV_FCHECK | EV_TOP, &str, cargs, ncargs);
            *bp = '\0';
        }
        else
        {
            buf1 = parse_to(&arg, '\0', interp | EV_TOP);
        }


        // Call the correct handler.
        //
        if (cmdp->callseq & CS_CMDARG)
        {
            (*(((CMDENT_ONE_ARG_CMDARG *)cmdp)->handler))(executor, caller,
                enactor, key, buf1, cargs, ncargs);
        }
        else if (cmdp->callseq & CS_ADDED)
        {
            for (add = cmdp->addent; add != NULL; add = add->next)
            {
                buff = atr_get(add->thing, add->atr, &aowner, &aflags);

                // Skip the '$' character, and the next character.
                //
                for (s = buff + 2; *s && *s != ':'; s++)
                {
                    ; // Nothing.
                }
                if (!*s)
                {
                    break;
                }
                *s++ = '\0';

                if (!(cmdp->callseq & CS_LEADIN))
                {
                    for (j = unp_command; *j && (*j != ' '); j++) ;
                }
                else
                {
                    for (j = unp_command; *j; j++) ;
                }

                new0 = alloc_lbuf("process_cmdent.soft");
                bp = new0;
                if (!*j)
                {
                    // No args.
                    //
                    if (!(cmdp->callseq & CS_LEADIN))
                    {
                        safe_str(cmdp->cmdname, new0, &bp);
                    }
                    else
                    {
                        safe_str(unp_command, new0, &bp);
                    }
                    if (switchp)
                    {
                        safe_chr('/', new0, &bp);
                        safe_str(switchp, new0, &bp);
                    }
                    *bp = '\0';
                }
                else
                {
                    j++;
                    safe_str(cmdp->cmdname, new0, &bp);
                    if (switchp)
                    {
                        safe_chr('/', new0, &bp);
                        safe_str(switchp, new0, &bp);
                    }
                    safe_chr(' ', new0, &bp);
                    safe_str(j, new0, &bp);
                    *bp = '\0';
                }

                if (  (  (aflags & AF_REGEXP)
                      && regexp_match(buff + 1, new0,
                             ((aflags & AF_CASE) ? 0 : PCRE_CASELESS), aargs,
                             NUM_ENV_VARS))
                   || (  (aflags & AF_REGEXP) == 0
                      && wild(buff + 1, new0, aargs, NUM_ENV_VARS)))
                {
                    CLinearTimeAbsolute lta;
                    wait_que(add->thing, caller, executor, false, lta,
                        NOTHING, 0, s, aargs, NUM_ENV_VARS, mudstate.global_regs);
                    for (i = 0; i < NUM_ENV_VARS; i++)
                    {
                        if (aargs[i])
                        {
                            free_lbuf(aargs[i]);
                        }
                    }
                }
                free_lbuf(new0);
                free_lbuf(buff);
            }
        }
        else
        {
            (*(((CMDENT_ONE_ARG *)cmdp)->handler))(executor, caller,
                enactor, key, buf1);
        }

        // Free the buffer if one was allocated.
        //
        if ((interp & EV_EVAL) && !(cmdp->callseq & CS_ADDED))
        {
            free_lbuf(buf1);
        }
        break;

    case CS_TWO_ARG: // <cmd> <arg1> = <arg2>

        // Interpret ARG1
        //
        buf2 = parse_to(&arg, '=', EV_STRIP_TS);

        nargs2 = 0;
        if (buf2)
        {
            if (arg)
            {
                nargs2 = 2;
            }
            else
            {
                nargs2 = 1;
            }
        }

        // Handle when no '=' was specified.
        //
        if (!arg || (arg && !*arg))
        {
            arg = &tchar;
            *arg = '\0';
        }
        buf1 = bp = alloc_lbuf("process_cmdent.2");
        str = buf2;
        mux_exec(buf1, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL | EV_TOP, &str, cargs, ncargs);
        *bp = '\0';

        if (cmdp->callseq & CS_ARGV)
        {
            // Arg2 is ARGV style.  Go get the args.
            //
            parse_arglist(executor, caller, enactor, arg, '\0',
                interp | EV_STRIP_LS | EV_STRIP_TS, args, MAX_ARG, cargs,
                ncargs, &nargs);

            // Call the correct command handler.
            //
            if (cmdp->callseq & CS_CMDARG)
            {
                (*(((CMDENT_TWO_ARG_ARGV_CMDARG *)cmdp)->handler))(executor,
                    caller, enactor, key, buf1, args, nargs, cargs, ncargs);
            }
            else
            {
                (*(((CMDENT_TWO_ARG_ARGV *)cmdp)->handler))(executor, caller,
                    enactor, key, buf1, args, nargs);
            }

            // Free the argument buffers.
            //
            for (i = 0; i < nargs; i++)
            {
                free_lbuf(args[i]);
            }
        }
        else
        {
            // Arg2 is normal style.  Interpret if needed.
            //
            if (interp & EV_EVAL)
            {
                buf2 = bp = alloc_lbuf("process_cmdent.3");
                str = arg;
                mux_exec(buf2, &bp, executor, caller, enactor,
                    interp | EV_FCHECK | EV_TOP, &str, cargs, ncargs);
                *bp = '\0';
            }
            else if (cmdp->callseq & CS_UNPARSE)
            {
                buf2 = parse_to(&arg, '\0', interp | EV_TOP | EV_NO_COMPRESS);
            }
            else
            {
                buf2 = parse_to(&arg, '\0', interp | EV_STRIP_LS | EV_STRIP_TS | EV_TOP);
            }

            // Call the correct command handler.
            //
            if (cmdp->callseq & CS_CMDARG)
            {
                (*(((CMDENT_TWO_ARG_CMDARG *)cmdp)->handler))(executor,
                    caller, enactor, key, buf1, buf2, cargs, ncargs);
            }
            else
            {
                (*(((CMDENT_TWO_ARG *)cmdp)->handler))(executor, caller,
                    enactor, key, nargs2, buf1, buf2);
            }

            // Free the buffer, if needed.
            //
            if (interp & EV_EVAL)
            {
                free_lbuf(buf2);
            }
        }

        // Free the buffer obtained by evaluating Arg1.
        //
        free_lbuf(buf1);
        break;
    }

    // 'After' hooks.
    // @hook idea from TinyMUSH 3, code from RhostMUSH. Ported by Jake Nelson.
    //
    if (  (cmdp->hookmask & HOOK_AFTER)
        && Good_obj(mudconf.hook_obj)
        && !Going(mudconf.hook_obj))
    {
        s_uselock = hook_name(cmdp->cmdname, HOOK_AFTER);
        hk_ap2 = atr_str(s_uselock);
        (void)process_hook(executor, mudconf.hook_obj, s_uselock, hk_ap2, false);
        free_sbuf(s_uselock);
    }
    return;
}

static int cmdtest(dbref player, char *cmd)
{
    char *buff1, *pt1, *pt2;
    dbref aowner;
    int aflags, rval;

    rval = 0;
    buff1 = atr_get(player, A_CMDCHECK, &aowner, &aflags);
    pt1 = buff1;
    while (pt1 && *pt1)
    {
        pt2 = strchr(pt1, ':');
        if (!pt2 || (pt2 == pt1))
            break;
        if (!strncmp(pt2+1, cmd, strlen(cmd)))
        {
            if (*(pt2-1) == '1')
                rval = 1;
            else
                rval = 2;
            break;
        }
        pt1 = strchr(pt2+1,' ');
        if (pt1 && *pt1)
        {
            while (mux_isspace(*pt1))
            {
                pt1++;
            }
        }
    }
    free_lbuf(buff1);
    return rval;
}

static int zonecmdtest(dbref player, char *cmd)
{
    if (!Good_obj(player) || God(player))
    {
        return 0;
    }
    dbref loc = Location(player);

    int i_ret = 0;
    if (Good_obj(loc))
    {
        i_ret = cmdtest(loc, cmd);
        if (i_ret == 0)
        {
            dbref zone = Zone(loc);
            if (  Good_obj(zone)
               && (  isRoom(zone)
                  || isThing(zone)))
            {
                i_ret = cmdtest(zone, cmd);
            }
        }
    }
    return i_ret;
}

static int higcheck(dbref executor, dbref caller, dbref enactor, CMDENT *cmdp,
             char *pCommand)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(pCommand);

    if (  Good_obj(mudconf.hook_obj)
       && !Going(mudconf.hook_obj))
    {
        char *s_uselock;
        ATTR *checkattr;
        bool bResult;
        if (cmdp->hookmask & HOOK_IGNORE)
        {
            s_uselock = hook_name(cmdp->cmdname, HOOK_IGNORE);
            checkattr = atr_str(s_uselock);
            bResult = process_hook(executor, mudconf.hook_obj, s_uselock,
                checkattr, true);
            free_sbuf(s_uselock);
            if (!bResult)
            {
                return 2;
            }
        }
        if (cmdp->hookmask & HOOK_PERMIT)
        {
            s_uselock = hook_name(cmdp->cmdname, HOOK_PERMIT);
            checkattr = atr_str(s_uselock);
            bResult = process_hook(executor, mudconf.hook_obj, s_uselock,
                checkattr, true);
            free_sbuf(s_uselock);
            if (!bResult)
            {
                return 1;
            }
        }
    }
    return 0;
}

static void hook_fail(dbref executor, CMDENT *cmdp, char *pCommand)
{
    UNUSED_PARAMETER(pCommand);

    if (  Good_obj(mudconf.hook_obj)
       && !Going(mudconf.hook_obj))
    {
        char *s_uselock = hook_name(cmdp->cmdname, HOOK_AFAIL);
        ATTR *hk_ap2 = atr_str(s_uselock);
        process_hook(executor, mudconf.hook_obj, s_uselock, hk_ap2, false);
        free_sbuf(s_uselock);
    }
}

// ---------------------------------------------------------------------------
// process_command: Execute a command.
//
char *process_command
(
    dbref executor,
    dbref caller,
    dbref enactor,
    bool  interactive,
    char *arg_command,
    char *args[],
    int   nargs
)
{
    static char preserve_cmd[LBUF_SIZE];
    char *pOriginalCommand = arg_command;
    static char SpaceCompressCommand[LBUF_SIZE];
    static char LowerCaseCommand[LBUF_SIZE];
    char *pCommand;
    char *p, *q, *arg, *pSlash, *cmdsave, *bp, *str, check2[2];
    int aflags, i;
    dbref exit, aowner;
    CMDENT *cmdp;

    // Robustify player.
    //
    cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = (char *)"< process_command >";
    mudstate.nStackNest = 0;
    mudstate.bStackLimitReached = false;
    *(check2 + 1) = '\0';

    mux_assert(pOriginalCommand);

    if (!Good_obj(executor))
    {
        // We are using SpaceCompressCommand temporarily.
        //
        STARTLOG(LOG_BUGS, "CMD", "PLYR");
        mux_sprintf(SpaceCompressCommand, sizeof(SpaceCompressCommand),
            "Bad player in process_command: %d", executor);
        log_text(SpaceCompressCommand);
        ENDLOG;
        mudstate.debug_cmd = cmdsave;
        return pOriginalCommand;
    }

    // Make sure player isn't going or halted.
    //
    if (  Going(executor)
       || (  Halted(executor)
          && !(  isPlayer(executor)
              && interactive)))
    {
        notify(Owner(executor),
            tprintf("Attempt to execute command by halted object #%d", executor));
        mudstate.debug_cmd = cmdsave;
        return pOriginalCommand;
    }
    if (  Suspect(executor)
       && (mudconf.log_options & LOG_SUSPECTCMDS))
    {
        STARTLOG(LOG_SUSPECTCMDS, "CMD", "SUSP");
        log_name_and_loc(executor);
        log_text(" entered: ");
        log_text(pOriginalCommand);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALLCOMMANDS, "CMD", "ALL");
        log_name_and_loc(executor);
        log_text(" entered: ");
        log_text(pOriginalCommand);
        ENDLOG;
    }

    // Reset recursion limits.
    //
    mudstate.func_nest_lev = 0;
    mudstate.func_invk_ctr = 0;
    mudstate.ntfy_nest_lev = 0;
    mudstate.lock_nest_lev = 0;

    if (Verbose(executor))
    {
        notify(Owner(executor), tprintf("%s] %s", Name(executor), pOriginalCommand));
    }

    // Eat leading whitespace, and space-compress if configured.
    //
    while (mux_isspace(*pOriginalCommand))
    {
        pOriginalCommand++;
    }
    mux_strncpy(preserve_cmd, pOriginalCommand, LBUF_SIZE-1);
    mudstate.debug_cmd = pOriginalCommand;
    mudstate.curr_cmd = preserve_cmd;

    if (mudconf.space_compress)
    {
        // Compress out the spaces and use that as the command
        //
        pCommand = SpaceCompressCommand;

        p = pOriginalCommand;
        q = SpaceCompressCommand;
        while (*p)
        {
            while (  *p
                  && !mux_isspace(*p)
                  && q < SpaceCompressCommand + LBUF_SIZE)
            {
                *q++ = *p++;
            }
            while (mux_isspace(*p))
            {
                p++;
            }
            if (*p)
            {
                *q++ = ' ';
            }
        }
        *q = '\0';
    }
    else
    {
        // Don't compress the spaces. Use the original command
        // (without leading spaces) as the command to use.
        //
        pCommand = pOriginalCommand;
    }

    // Now comes the fun stuff.  First check for single-letter leadins.
    // We check these before checking HOME because they are among the
    // most frequently executed commands, and they can never be the
    // HOME command.
    //
    i = pCommand[0] & 0xff;
    int cval = 0;
    int hval = 0;
    if (i && (prefix_cmds[i] != NULL))
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore
        *check2 = (char)i;
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, check2);
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), check2);
        }
        if (cval == 0)
        {
            cval = zonecmdtest(executor, check2);
        }
        if (prefix_cmds[i]->hookmask & (HOOK_IGNORE|HOOK_PERMIT))
        {
            hval = higcheck(executor, caller, enactor, prefix_cmds[i], pCommand);
        }
        if ((cval != 2) && (hval != 2))
        {
            if (cval == 1 || hval == 1)
            {
                if (prefix_cmds[i]->hookmask & HOOK_AFAIL)
                {
                    hook_fail(executor, prefix_cmds[i], pCommand);
                }
                else
                {
                    notify(executor, NOPERM_MESSAGE);
                }
                return preserve_cmd;
            }
            process_cmdent(prefix_cmds[i], NULL, executor, caller, enactor,
                interactive, pCommand, pCommand, args, nargs);
            if (mudstate.bStackLimitReached)
            {
                STARTLOG(LOG_ALWAYS, "CMD", "SPAM");
                log_name_and_loc(executor);
                log_text(" entered: ");
                log_text(pOriginalCommand);
                ENDLOG;
            }
            mudstate.bStackLimitReached = false;

            mudstate.debug_cmd = cmdsave;
            return preserve_cmd;
        }
    }

    if (  mudconf.have_comsys
       && !Slave(executor)
       && !do_comsystem(executor, pCommand))
    {
        return preserve_cmd;
    }

    // Check for the HOME command.
    //
    if (  Has_location(executor)
       && string_compare(pCommand, "home") == 0)
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, "home");
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), "home");
        }
        else
        {
            cval = 0;
        }
        if (cval == 0)
        {
            cval = zonecmdtest(executor, "home");
        }
        if (cval != 2)
        {
            if (!check_access(executor, mudconf.restrict_home))
            {
                notify(executor, NOPERM_MESSAGE);
                return preserve_cmd;
            }
            if (cval == 1)
            {
                notify(executor, NOPERM_MESSAGE);
                return preserve_cmd;
            }
            if (  (  Fixed(executor)
                  || Fixed(Owner(executor)))
               && !WizRoy(executor))
            {
                notify(executor, mudconf.fixed_home_msg);
                return preserve_cmd;
            }
            do_move(executor, caller, enactor, 0, "home");
            mudstate.debug_cmd = cmdsave;
            return preserve_cmd;
        }
    }

    // Only check for exits if we may use the goto command.
    //
    if (check_access(executor, goto_cmdp->perms))
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore
        // Master room exits are not affected.
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, "goto");
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), "goto");
        }
        else
        {
            cval = 0;
        }
        if (cval == 0)
        {
            cval = zonecmdtest(executor, "goto");
        }
        if (goto_cmdp->hookmask & (HOOK_IGNORE|HOOK_PERMIT))
        {
            hval = higcheck(executor, caller, enactor, goto_cmdp, "goto");
        }
        if ((cval != 2) && (hval != 2))
        {
            // Check for an exit name.
            //
            init_match_check_keys(executor, pCommand, TYPE_EXIT);
            match_exit_with_parents();
            exit = last_match_result();
            if (exit != NOTHING)
            {
                if (cval || hval)
                {
                    if (goto_cmdp->hookmask & HOOK_AFAIL)
                    {
                        hook_fail(executor, goto_cmdp, "goto");
                    }
                    else
                    {
                        notify(executor, NOPERM_MESSAGE);
                    }
                    return preserve_cmd;
                }
                move_exit(executor, exit, false, "You can't go that way.", 0);
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }

            // Check for an exit in the master room.
            //
            init_match_check_keys(executor, pCommand, TYPE_EXIT);
            match_master_exit();
            exit = last_match_result();
            if (exit != NOTHING)
            {
                move_exit(executor, exit, true, NULL, 0);
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }
        }
    }

    // Set up a lowercase command and an arg pointer for the hashed
    // command check.  Since some types of argument processing destroy
    // the arguments, make a copy so that we keep the original command
    // line intact.  Store the edible copy in LowerCaseCommand after
    // the lower-cased command.
    //

    // Make lowercase command
    //
    char *LowerCaseCommandEnd = LowerCaseCommand + (LBUF_SIZE - 1);
    for (p = pCommand, q = LowerCaseCommand;
         *p && !mux_isspace(*p) && q < LowerCaseCommandEnd;
         p++, q++)
    {
        *q = mux_tolower(*p);
    }
    *q = '\0';
    int nLowerCaseCommand = q - LowerCaseCommand;

    // Skip spaces before arg
    //
    while (mux_isspace(*p))
    {
        p++;
    }

    // Remember where arg starts
    //
    arg = p;

    // Strip off any command switches and save them.
    //
    pSlash = strchr(LowerCaseCommand, '/');
    if (pSlash)
    {
        nLowerCaseCommand = pSlash - LowerCaseCommand;
        *pSlash++ = '\0';
    }

    // Check for a builtin command (or an alias of a builtin command)
    //
    cmdp = (CMDENT *)hashfindLEN(LowerCaseCommand, nLowerCaseCommand, &mudstate.command_htab);

    /* If command is checked to ignore NONMATCHING switches, fall through */
    if (cmdp)
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, cmdp->cmdname);
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), cmdp->cmdname);
        }
        else
        {
            cval = 0;
        }
        if (cval == 0)
        {
            cval = zonecmdtest(executor, cmdp->cmdname);
        }
        if (cmdp->hookmask & (HOOK_IGNORE|HOOK_PERMIT))
        {
            hval = higcheck(executor, caller, enactor, cmdp, LowerCaseCommand);
        }

        // If the command contains a switch, but the command doesn't support
        // any switches or the command contains one that isn't supported,
        // HOOK_IGSWITCH will allow us to treat the entire command as if it
        // weren't a built-in command.
        //
        int flagvalue;
        if (  (cmdp->hookmask & HOOK_IGSWITCH)
           && pSlash)
        {
            if (cmdp->switches)
            {
                search_nametab(executor, cmdp->switches, pSlash, &flagvalue);
                if (flagvalue & SW_MULTIPLE)
                {
                    MUX_STRTOK_STATE ttswitch;
                    // All the switches given a command shouldn't exceed 200 chars together
                    char switch_buff[200];
                    char *switch_ptr;
                    mux_strncpy(switch_buff, pSlash, sizeof(switch_buff)-1);
                    mux_strtok_src(&ttswitch, switch_buff);
                    mux_strtok_ctl(&ttswitch, "/");
                    switch_ptr = mux_strtok_parse(&ttswitch);
                    while (  switch_ptr
                          && *switch_ptr)
                    {
                        search_nametab(executor, cmdp->switches, switch_ptr, &flagvalue);
                        if (flagvalue == -1)
                        {
                            break;
                        }
                        switch_ptr = mux_strtok_parse(&ttswitch);
                    }
                }
                if (flagvalue == -1)
                {
                    cval = 2;
                }
            }
            else
            {
                // Switch exists but no switches allowed for command.
                //
                cval = 2;
            }
        }

        if (  cval != 2
           && hval != 2)
        {
            if (  cval == 1
               || hval == 1)
            {
                if (cmdp->hookmask & HOOK_AFAIL)
                {
                    hook_fail(executor, cmdp, LowerCaseCommand);
                }
                else
                {
                    notify(executor, NOPERM_MESSAGE);
                }
                return preserve_cmd;
            }
            if (  mudconf.space_compress
               && (cmdp->callseq & CS_NOSQUISH))
            {
                // We handle this specially -- there is no space compression
                // involved, so we must go back to the original command.
                // We skip over the command and a single space to position
                // arg at the arguments.
                //
                arg = pCommand = pOriginalCommand;
                while (*arg && !mux_isspace(*arg))
                {
                    arg++;
                }
                if (*arg)
                {
                    // We stopped on the space, advance to next.
                    //
                    arg++;
                }
            }
            process_cmdent(cmdp, pSlash, executor, caller, enactor, interactive,
                arg, pCommand, args, nargs);
            if (mudstate.bStackLimitReached)
            {
                STARTLOG(LOG_ALWAYS, "CMD", "SPAM");
                log_name_and_loc(executor);
                log_text(" entered: ");
                log_text(pOriginalCommand);
                ENDLOG;
            }
            mudstate.bStackLimitReached = false;
            mudstate.debug_cmd = cmdsave;
            return preserve_cmd;
        }
    }

    // Check for enter and leave aliases, user-defined commands on the
    // player, other objects where the player is, on objects in the
    // player's inventory, and on the room that holds the player. We
    // evaluate the command line here to allow chains of $-commands
    // to work.
    //
    bp = LowerCaseCommand;
    str = pCommand;
    mux_exec(LowerCaseCommand, &bp, executor, caller, enactor,
        EV_EVAL | EV_FCHECK | EV_STRIP_CURLY | EV_TOP, &str, args, nargs);
    *bp = '\0';
    bool succ = false;

    // Idea for enter/leave aliases from R'nice@TinyTIM
    //
    if (Has_location(executor) && Good_obj(Location(executor)))
    {
        // Check for a leave alias.
        //
        p = atr_pget(Location(executor), A_LALIAS, &aowner, &aflags);
        if (*p)
        {
            if (matches_exit_from_list(LowerCaseCommand, p))
            {
                free_lbuf(p);

                // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
                // Both from RhostMUSH.
                // cval/hval values: 0 normal, 1 disable, 2 ignore
                if (CmdCheck(executor))
                {
                    cval = cmdtest(executor, "leave");
                }
                else if (CmdCheck(Owner(executor)))
                {
                    cval = cmdtest(Owner(executor), "leave");
                }
                else
                {
                    cval = 0;
                }
                if (cval == 0)
                {
                    cval = zonecmdtest(executor, "leave");
                }
                cmdp = (CMDENT *)hashfindLEN((char *)"leave", strlen("leave"), &mudstate.command_htab);
                if (cmdp->hookmask & (HOOK_IGNORE|HOOK_PERMIT))
                {
                    hval = higcheck(executor, caller, enactor, cmdp, "leave");
                }
                if ((cval != 2) && (hval != 2))
                {
                    if (cval == 1 || hval == 1)
                    {
                        if (cmdp->hookmask & HOOK_AFAIL)
                        {
                            hook_fail(executor, cmdp, "leave");
                        }
                        else
                        {
                            notify(executor, NOPERM_MESSAGE);
                        }
                        return preserve_cmd;
                    }
                    do_leave(executor, caller, executor, 0);
                    return preserve_cmd;
                }
            }
        }
        free_lbuf(p);

        DOLIST(exit, Contents(Location(executor)))
        {
            p = atr_pget(exit, A_EALIAS, &aowner, &aflags);
            if (*p)
            {
                if (matches_exit_from_list(LowerCaseCommand, p))
                {
                    free_lbuf(p);

                    // Check for enter aliases.
                    //
                    // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
                    // Both from RhostMUSH.
                    // cval/hval values: 0 normal, 1 disable, 2 ignore
                    if (CmdCheck(executor))
                    {
                        cval = cmdtest(executor, "enter");
                    }
                    else if (CmdCheck(Owner(executor)))
                    {
                        cval = cmdtest(Owner(executor), "enter");
                    }
                    else
                    {
                        cval = 0;
                    }
                    if (cval == 0)
                    {
                        cval = zonecmdtest(executor, "enter");
                    }
                    cmdp = (CMDENT *)hashfindLEN((char *)"enter", strlen("enter"), &mudstate.command_htab);
                    if (cmdp->hookmask & (HOOK_IGNORE|HOOK_PERMIT))
                    {
                        hval = higcheck(executor, caller, enactor, cmdp, "enter");
                    }
                    if ((cval != 2) && (hval != 2))
                    {
                        if (cval == 1 || hval == 1)
                        {
                            if (cmdp->hookmask & HOOK_AFAIL)
                            {
                                hook_fail(executor, cmdp, "enter");
                            }
                            else
                            {
                                notify(executor, NOPERM_MESSAGE);
                            }
                            return preserve_cmd;
                        }
                        do_enter_internal(executor, exit, false);
                        return preserve_cmd;
                    }
                    else if (cval == 1)
                    {
                        notify_quiet(executor, NOPERM_MESSAGE);
                        return preserve_cmd;
                    }
                }
            }
            free_lbuf(p);
        }
    }

    // Check for $-command matches on me.
    //
    if (mudconf.match_mine && !No_Command(executor))
    {
        if (  (  !isPlayer(executor)
              || mudconf.match_mine_pl)
           && atr_match(executor, executor, AMATCH_CMD, LowerCaseCommand, preserve_cmd, true))
        {
            succ = true;
        }
    }

    // Check for $-command matches on nearby things and on my room.
    //
    if (Has_location(executor))
    {
        succ |= list_check(Contents(Location(executor)), executor, AMATCH_CMD, LowerCaseCommand, preserve_cmd, true);

        if (!No_Command(Location(executor)))
        {
            succ |= atr_match(Location(executor), executor, AMATCH_CMD, LowerCaseCommand, preserve_cmd, true);
        }
    }

    // Check for $-command matches in my inventory.
    //
    if (Has_contents(executor))
    {
        succ |= list_check(Contents(executor), executor, AMATCH_CMD, LowerCaseCommand, preserve_cmd, true);
    }

    if (  !succ
       && mudconf.have_zones)
    {
        // now do check on zones.
        //
        dbref zone = Zone(executor);
        dbref loc = Location(executor);
        dbref zone_loc = NOTHING;
        if (  Good_obj(loc)
           && Good_obj(zone_loc = Zone(loc)))
        {
            if (isRoom(zone_loc))
            {
                // zone of player's location is a parent room.
                //
                if (loc != zone)
                {
                    // check parent room exits.
                    //
                    init_match_check_keys(executor, pCommand, TYPE_EXIT);
                    match_zone_exit();
                    exit = last_match_result();
                    if (exit != NOTHING)
                    {
                        move_exit(executor, exit, true, NULL, 0);
                        mudstate.debug_cmd = cmdsave;
                        return preserve_cmd;
                    }
                    succ |= list_check(Contents(zone_loc), executor,
                               AMATCH_CMD, LowerCaseCommand, preserve_cmd,
                               true);

                    // end of parent room checks.
                    //
                }
            }
            else
            {
                // try matching commands on area zone object.
                //
                if (!No_Command(zone_loc))
                {
                    succ |= atr_match(zone_loc, executor, AMATCH_CMD,
                       LowerCaseCommand, preserve_cmd, true);
                }
            }
        }

        // End of matching on zone of player's location.
        //

        // if nothing matched with parent room/zone object, try matching
        // zone commands on the player's personal zone.
        //
        if (  !succ
           && Good_obj(zone)
           && !No_Command(zone)
           && zone_loc != zone)
        {
            succ |= atr_match(zone, executor, AMATCH_CMD, LowerCaseCommand, preserve_cmd, true);
        }
    }

    // If we didn't find anything, try in the master room.
    //
    if (!succ)
    {
        if (  Good_obj(mudconf.master_room)
           && Has_contents(mudconf.master_room))
        {
            succ |= list_check(Contents(mudconf.master_room), executor,
                AMATCH_CMD, LowerCaseCommand, preserve_cmd, false);

            if (!No_Command(mudconf.master_room))
            {
                succ |= atr_match(mudconf.master_room, executor, AMATCH_CMD,
                    LowerCaseCommand, preserve_cmd, false);
            }
        }
    }

    // If we still didn't find anything, tell how to get help.
    //
    if (!succ)
    {
        if (  Good_obj(mudconf.global_error_obj)
           && !Going(mudconf.global_error_obj))
        {
            char *errtext = atr_get(mudconf.global_error_obj, A_VA, &aowner, &aflags);
            char *errbuff = alloc_lbuf("process_command.error_msg");
            char *errbufc = errbuff;
            str = errtext;
            mux_exec(errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                EV_EVAL | EV_FCHECK | EV_STRIP_CURLY | EV_TOP, &str,
                &pCommand, 1);
            notify(executor, errbuff);
            free_lbuf(errtext);
            free_lbuf(errbuff);
        }
        else
        {
            // We use LowerCaseCommand for another purpose.
            //
            notify(executor, "Huh?  (Type \"help\" for help.)");
            STARTLOG(LOG_BADCOMMANDS, "CMD", "BAD");
            log_name_and_loc(executor);
            log_text(" entered: ");
            log_text(pCommand);
            ENDLOG;
        }
    }
    mudstate.debug_cmd = cmdsave;
    return preserve_cmd;
}

// ---------------------------------------------------------------------------
// list_cmdtable: List internal commands.
//
static void list_cmdtable(dbref player)
{
    char *buf = alloc_lbuf("list_cmdtable");
    char *bp = buf;
    ITL itl;
    ItemToList_Init(&itl, buf, &bp);
    ItemToList_AddString(&itl, (char *)"Commands:");

    {
        CMDENT_NO_ARG *cmdp;
        for (cmdp = command_table_no_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_ONE_ARG *cmdp;
        for (cmdp = command_table_one_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_ONE_ARG_CMDARG *cmdp;
        for (cmdp = command_table_one_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_TWO_ARG *cmdp;
        for (cmdp = command_table_two_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV *cmdp;
        for (cmdp = command_table_two_arg_argv; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_TWO_ARG_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_argv_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
                && !(cmdp->perms & CF_DARK))
            {
                ItemToList_AddString(&itl, cmdp->cmdname);
            }
        }
    }
    ItemToList_Final(&itl);
    *bp = '\0';

    // Players get the list of logged-out cmds too
    //
    if (isPlayer(player))
    {
        display_nametab(player, logout_cmdtable, buf, true);
    }
    else
    {
        notify(player, buf);
    }
    free_lbuf(buf);
}

// ---------------------------------------------------------------------------
// list_attrtable: List available attributes.
//
static void list_attrtable(dbref player)
{
    ATTR *ap;

    char *buf = alloc_lbuf("list_attrtable");
    char *bp = buf;
    ITL itl;
    ItemToList_Init(&itl, buf, &bp);
    ItemToList_AddString(&itl, (char *)"Attributes:");
    for (ap = attr; ap->name; ap++)
    {
        if (See_attr(player, player, ap))
        {
            ItemToList_AddString(&itl, (char *)ap->name);
        }
    }
    ItemToList_Final(&itl);
    *bp = '\0';
    raw_notify(player, buf);
    free_lbuf(buf);
}

// ---------------------------------------------------------------------------
// list_cmdaccess: List access commands.
//
NAMETAB access_nametab[] =
{
    {"builder",               6, CA_WIZARD, CA_BUILDER},
    {"dark",                  4, CA_GOD,    CF_DARK},
    {"disabled",              4, CA_GOD,    CA_DISABLED},
    {"global_build",          8, CA_PUBLIC, CA_GBL_BUILD},
    {"global_interp",         8, CA_PUBLIC, CA_GBL_INTERP},
    {"god",                   2, CA_GOD,    CA_GOD},
    {"head",                  2, CA_WIZARD, CA_HEAD},
    {"immortal",              3, CA_WIZARD, CA_IMMORTAL},
    {"need_location",         6, CA_PUBLIC, CA_LOCATION},
    {"need_contents",         6, CA_PUBLIC, CA_CONTENTS},
    {"need_player",           6, CA_PUBLIC, CA_PLAYER},
    {"no_haven",              4, CA_PUBLIC, CA_NO_HAVEN},
    {"no_robot",              4, CA_WIZARD, CA_NO_ROBOT},
    {"no_slave",              5, CA_PUBLIC, CA_NO_SLAVE},
    {"no_suspect",            5, CA_WIZARD, CA_NO_SUSPECT},
    {"no_guest",              5, CA_WIZARD, CA_NO_GUEST},
    {"no_uninspected",        5, CA_WIZARD, CA_NO_UNINS},
    {"robot",                 2, CA_WIZARD, CA_ROBOT},
    {"staff",                 4, CA_WIZARD, CA_STAFF},
    {"static",                4, CA_GOD,    CA_STATIC},
    {"uninspected",           5, CA_WIZARD, CA_UNINS},
    {"wizard",                3, CA_WIZARD, CA_WIZARD},
    {NULL,                    0, 0,         0}
};

static void list_cmdaccess(dbref player)
{
    ATTR *ap;

    char *buff = alloc_sbuf("list_cmdaccess");
    {
        CMDENT_NO_ARG *cmdp;
        for (cmdp = command_table_no_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_ONE_ARG *cmdp;
        for (cmdp = command_table_one_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_ONE_ARG_CMDARG *cmdp;
        for (cmdp = command_table_one_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_TWO_ARG *cmdp;
        for (cmdp = command_table_two_arg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV *cmdp;
        for (cmdp = command_table_two_arg_argv; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_TWO_ARG_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_argv_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (  check_access(player, cmdp->perms)
               && !(cmdp->perms & CF_DARK))
            {
                mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                listset_nametab(player, access_nametab, cmdp->perms, buff, true);
            }
        }
    }
    free_sbuf(buff);
    for (ap = attr; ap->name; ap++)
    {
        if (ap->flags & AF_NOCMD)
        {
            continue;
        }

        int nBuffer;
        bool bValid;
        buff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        CMDENT *cmdp = (CMDENT *)hashfindLEN(buff, nBuffer, &mudstate.command_htab);
        if (cmdp == NULL)
        {
            continue;
        }

        if (!check_access(player, cmdp->perms))
        {
            continue;
        }

        if (!(cmdp->perms & CF_DARK))
        {
            mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
            listset_nametab(player, access_nametab, cmdp->perms, buff, true);
        }
    }
}

// ---------------------------------------------------------------------------
// list_cmdswitches: List switches for commands.
//
static void list_cmdswitches(dbref player)
{
    char *buff = alloc_sbuf("list_cmdswitches");
    {
        CMDENT_NO_ARG *cmdp;
        for (cmdp = command_table_no_arg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG *cmdp;
        for (cmdp = command_table_one_arg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG_CMDARG *cmdp;
        for (cmdp = command_table_one_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG *cmdp;
        for (cmdp = command_table_two_arg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV *cmdp;
        for (cmdp = command_table_two_arg_argv; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_argv_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (cmdp->switches)
            {
                if (check_access(player, cmdp->perms))
                {
                    if (!(cmdp->perms & CF_DARK))
                    {
                        mux_sprintf(buff, SBUF_SIZE, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, false);
                    }
                }
            }
        }
    }
    free_sbuf(buff);
}

// ---------------------------------------------------------------------------
// list_attraccess: List access to attributes.
//
NAMETAB attraccess_nametab[] =
{
    {"const",       1,  CA_PUBLIC,  AF_CONST},
    {"dark",        2,  CA_WIZARD,  AF_DARK},
    {"deleted",     2,  CA_WIZARD,  AF_DELETED},
    {"god",         1,  CA_PUBLIC,  AF_GOD},
    {"hidden",      1,  CA_WIZARD,  AF_MDARK},
    {"ignore",      2,  CA_WIZARD,  AF_NOCMD},
    {"internal",    2,  CA_WIZARD,  AF_INTERNAL},
    {"is_lock",     4,  CA_PUBLIC,  AF_IS_LOCK},
    {"locked",      1,  CA_PUBLIC,  AF_LOCK},
    {"no_command",  4,  CA_PUBLIC,  AF_NOPROG},
    {"no_inherit",  4,  CA_PUBLIC,  AF_PRIVATE},
    {"private",     1,  CA_PUBLIC,  AF_ODARK},
    {"regexp",      1,  CA_PUBLIC,  AF_REGEXP},
    {"visual",      1,  CA_PUBLIC,  AF_VISUAL},
    {"wizard",      1,  CA_PUBLIC,  AF_WIZARD},
    { NULL,         0,          0,          0}
};

NAMETAB indiv_attraccess_nametab[] =
{
    {"case",                1,  CA_PUBLIC,  AF_CASE},
    {"hidden",              1,  CA_WIZARD,  AF_MDARK},
    {"html",                2,  CA_PUBLIC,  AF_HTML},
    {"no_parse",            4,  CA_PUBLIC,  AF_NOPARSE},
    {"no_command",          4,  CA_PUBLIC,  AF_NOPROG},
    {"no_inherit",          4,  CA_PUBLIC,  AF_PRIVATE},
    {"regexp",              1,  CA_PUBLIC,  AF_REGEXP},
    {"visual",              1,  CA_PUBLIC,  AF_VISUAL},
    {"wizard",              1,  CA_WIZARD,  AF_WIZARD},
    { NULL,                 0,          0,          0}
};

static void list_attraccess(dbref player)
{
    ATTR *ap;

    char *buff = alloc_sbuf("list_attraccess");
    for (ap = attr; ap->name; ap++)
    {
        if (bCanReadAttr(player, player, ap, false))
        {
            mux_sprintf(buff, SBUF_SIZE, "%s:", ap->name);
            listset_nametab(player, attraccess_nametab, ap->flags, buff, true);
        }
    }
    free_sbuf(buff);
}

// ---------------------------------------------------------------------------
// cf_access: Change command or switch permissions.
//
CF_HAND(cf_access)
{
    UNUSED_PARAMETER(vp);

    CMDENT *cmdp;
    char *ap;
    bool set_switch;

    for (ap = str; *ap && !mux_isspace(*ap) && (*ap != '/'); ap++) ;
    if (*ap == '/')
    {
        set_switch = true;
        *ap++ = '\0';
    }
    else
    {
        set_switch = false;
        if (*ap)
        {
            *ap++ = '\0';
        }
        while (mux_isspace(*ap))
        {
            ap++;
        }
    }

    cmdp = (CMDENT *)hashfindLEN(str, strlen(str), &mudstate.command_htab);
    if (cmdp != NULL)
    {
        if (set_switch)
        {
            return cf_ntab_access((int *)cmdp->switches, ap, pExtra, nExtra,
                                  player, cmd);
        }
        else
        {
            return cf_modify_bits(&(cmdp->perms), ap, pExtra, nExtra, player,
                                  cmd);
        }
    }
    else
    {
        if (!mux_stricmp(str, "home"))
        {
            return cf_modify_bits(&(mudconf.restrict_home), ap, pExtra,
                                  nExtra, player, cmd);
        }
        cf_log_notfound(player, cmd, "Command", str);
        return -1;
    }
}

// ---------------------------------------------------------------------------
// cf_acmd_access: Change command permissions for all attr-setting cmds.
//
CF_HAND(cf_acmd_access)
{
    UNUSED_PARAMETER(vp);

    ATTR *ap;

    for (ap = attr; ap->name; ap++)
    {
        int nBuffer;
        bool bValid;
        char *buff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        CMDENT *cmdp = (CMDENT *)hashfindLEN(buff, nBuffer, &mudstate.command_htab);
        if (cmdp != NULL)
        {
            int save = cmdp->perms;
            int failure = cf_modify_bits(&(cmdp->perms), str, pExtra, nExtra,
                 player, cmd);
            if (failure != 0)
            {
                cmdp->perms = save;
                return -1;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cf_attr_access: Change access on an attribute.
//
CF_HAND(cf_attr_access)
{
    UNUSED_PARAMETER(vp);

    ATTR *ap;
    char *sp;

    for (sp = str; *sp && !mux_isspace(*sp); sp++)
    {
        ; // Nothing
    }
    if (*sp)
    {
        *sp++ = '\0';
    }
    while (mux_isspace(*sp))
    {
        sp++;
    }

    ap = atr_str(str);
    if (ap)
    {
        return cf_modify_bits(&(ap->flags), sp, pExtra, nExtra, player, cmd);
    }
    else
    {
        cf_log_notfound(player, cmd, "Attribute", str);
        return -1;
    }
}

// ---------------------------------------------------------------------------
// cf_cmd_alias: Add a command alias.
//
CF_HAND(cf_cmd_alias)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    char *ap;
    CMDENT *cmdp, *cmd2;
    NAMETAB *nt;

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, " \t=,");
    char *alias = mux_strtok_parse(&tts);
    char *orig = mux_strtok_parse(&tts);

    if (!orig)
    {
        // We only got one argument to @alias. Bad.
        //
        return -1;
    }

    for (ap = orig; *ap && (*ap != '/'); ap++) ;
    if (*ap == '/')
    {
        // Switch form of command aliasing: create an alias for
        // a command + a switch
        //
        *ap++ = '\0';

        // Look up the command
        //
        cmdp = (CMDENT *) hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
        if (cmdp == NULL || cmdp->switches == NULL)
        {
            cf_log_notfound(player, cmd, "Command", orig);
            return -1;
        }

        // Look up the switch
        //
        nt = find_nametab_ent(player, (NAMETAB *) cmdp->switches, ap);
        if (!nt)
        {
            cf_log_notfound(player, cmd, "Switch", ap);
            return -1;
        }

        if (!hashfindLEN(alias, strlen(alias), (CHashTable *)vp))
        {
            // Create the new command table entry.
            //
            cmd2 = (CMDENT *)MEMALLOC(sizeof(CMDENT));
            ISOUTOFMEMORY(cmd2);
            cmd2->cmdname = StringClone(alias);
            cmd2->switches = cmdp->switches;
            cmd2->perms = cmdp->perms | nt->perm;
            cmd2->extra = (cmdp->extra | nt->flag) & ~SW_MULTIPLE;
            if (!(nt->flag & SW_MULTIPLE))
            {
                cmd2->extra |= SW_GOT_UNIQUE;
            }
            cmd2->callseq = cmdp->callseq;
            cmd2->handler = cmdp->handler;

            hashaddLEN(cmd2->cmdname, strlen(cmd2->cmdname), cmd2, (CHashTable *) vp);
        }
    }
    else
    {
        // A normal (non-switch) alias
        //
        void *hp = hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
        if (hp == NULL)
        {
            cf_log_notfound(player, cmd, "Entry", orig);
            return -1;
        }
        hashaddLEN(alias, strlen(alias), hp, (CHashTable *) vp);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// list_df_flags: List default flags at create time.
//
static void list_df_flags(dbref player)
{
    FLAGSET fs;

    fs = mudconf.player_flags;
    fs.word[FLAG_WORD1] |= TYPE_PLAYER;
    char *playerb = decode_flags(player, &fs);

    fs = mudconf.room_flags;
    fs.word[FLAG_WORD1] |= TYPE_ROOM;
    char *roomb = decode_flags(player, &fs);

    fs = mudconf.exit_flags;
    fs.word[FLAG_WORD1] |= TYPE_EXIT;
    char *exitb = decode_flags(player, &fs);

    fs = mudconf.thing_flags;
    fs.word[FLAG_WORD1] |= TYPE_THING;
    char *thingb = decode_flags(player, &fs);

    fs = mudconf.robot_flags;
    fs.word[FLAG_WORD1] |= TYPE_PLAYER;
    char *robotb = decode_flags(player, &fs);

    char *buff = alloc_lbuf("list_df_flags");
    mux_sprintf(buff, LBUF_SIZE,
        "Default flags: Players...%s Rooms...%s Exits...%s Things...%s Robots...%s",
        playerb, roomb, exitb, thingb, robotb);

    free_sbuf(playerb);
    free_sbuf(roomb);
    free_sbuf(exitb);
    free_sbuf(thingb);
    free_sbuf(robotb);

    raw_notify(player, buff);
    free_lbuf(buff);
}

// ---------------------------------------------------------------------------
// list_costs: List the costs of things.
//
#define coin_name(s)    (((s)==1) ? mudconf.one_coin : mudconf.many_coins)

static void list_costs(dbref player)
{
    char *buff = alloc_mbuf("list_costs");

    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, " and %d quota", mudconf.room_quota);
    }
    notify(player,
           tprintf("Digging a room costs %d %s%s.",
               mudconf.digcost, coin_name(mudconf.digcost), buff));
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, " and %d quota", mudconf.exit_quota);
    }
    notify(player,
           tprintf("Opening a new exit costs %d %s%s.",
               mudconf.opencost, coin_name(mudconf.opencost), buff));
    notify(player,
           tprintf("Linking an exit, home, or dropto costs %d %s.",
               mudconf.linkcost, coin_name(mudconf.linkcost)));
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, " and %d quota", mudconf.thing_quota);
    }
    if (mudconf.createmin == mudconf.createmax)
    {
        raw_notify(player,
               tprintf("Creating a new thing costs %d %s%s.",
                   mudconf.createmin,
                   coin_name(mudconf.createmin), buff));
    }
    else
    {
        raw_notify(player,
        tprintf("Creating a new thing costs between %d and %d %s%s.",
            mudconf.createmin, mudconf.createmax,
            mudconf.many_coins, buff));
    }
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, " and %d quota", mudconf.player_quota);
    }
    notify(player,
           tprintf("Creating a robot costs %d %s%s.",
               mudconf.robotcost, coin_name(mudconf.robotcost), buff));
    if (mudconf.killmin == mudconf.killmax)
    {
        int chance = 100;
        if (0 < mudconf.killguarantee)
        {
            chance = (mudconf.killmin * 100) / mudconf.killguarantee;
        }
        raw_notify(player, tprintf("Killing costs %d %s, with a %d%% chance of success.",
            mudconf.killmin, coin_name(mudconf.digcost), chance));
    }
    else
    {
        int cost_surething;
        raw_notify(player, tprintf("Killing costs between %d and %d %s.",
            mudconf.killmin, mudconf.killmax, mudconf.many_coins));
        if (0 < mudconf.killguarantee)
        {
            cost_surething = mudconf.killguarantee;
        }
        else
        {
            cost_surething = mudconf.killmin;
        }
        raw_notify(player, tprintf("You must spend %d %s to guarantee success.",
            cost_surething, coin_name(cost_surething)));
    }
    raw_notify(player,
           tprintf("Computationally expensive commands and functions (ie: @entrances, @find, @search, @stats (with an argument or switch), search(), and stats()) cost %d %s.",
            mudconf.searchcost, coin_name(mudconf.searchcost)));
    if (mudconf.machinecost > 0)
        raw_notify(player,
           tprintf("Each command run from the queue costs 1/%d %s.",
               mudconf.machinecost, mudconf.one_coin));
    if (mudconf.waitcost > 0)
    {
        raw_notify(player,
               tprintf("A %d %s deposit is charged for putting a command on the queue.",
                   mudconf.waitcost, mudconf.one_coin));
        raw_notify(player, "The deposit is refunded when the command is run or canceled.");
    }
    if (mudconf.sacfactor == 0)
    {
        mux_ltoa(mudconf.sacadjust, buff);
    }
    else if (mudconf.sacfactor == 1)
    {
        if (mudconf.sacadjust < 0)
            mux_sprintf(buff, MBUF_SIZE, "<create cost> - %d", -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            mux_sprintf(buff, MBUF_SIZE, "<create cost> + %d", mudconf.sacadjust);
        else
            mux_sprintf(buff, MBUF_SIZE, "<create cost>");
    }
    else
    {
        if (mudconf.sacadjust < 0)
            mux_sprintf(buff, MBUF_SIZE, "(<create cost> / %d) - %d", mudconf.sacfactor, -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            mux_sprintf(buff, MBUF_SIZE, "(<create cost> / %d) + %d", mudconf.sacfactor, mudconf.sacadjust);
        else
            mux_sprintf(buff, MBUF_SIZE, "<create cost> / %d", mudconf.sacfactor);
    }
    raw_notify(player, tprintf("The value of an object is %s.", buff));
    if (mudconf.clone_copy_cost)
        raw_notify(player, "The default value of cloned objects is the value of the original object.");
    else
        raw_notify(player, tprintf("The default value of cloned objects is %d %s.",
                mudconf.createmin, coin_name(mudconf.createmin)));

    free_mbuf(buff);
}

// ---------------------------------------------------------------------------
// list_options: List more game options from mudconf.
//
static const char *switchd[] =
{"/first", "/all"};
static const char *examd[] =
{"/brief", "/full"};
static const char *ed[] =
{"Disabled", "Enabled"};

static void list_options(dbref player)
{
    char *buff;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    if (mudconf.quotas)
        raw_notify(player, "Building quotas are enforced.");
    if (mudconf.name_spaces)
        raw_notify(player, "Player names may contain spaces.");
    else
        raw_notify(player, "Player names may not contain spaces.");
    if (!mudconf.robot_speak)
        raw_notify(player, "Robots are not allowed to speak in public areas.");
    if (mudconf.player_listen)
        raw_notify(player, "The @Listen/@Ahear attribute set works on player objects.");
    if (mudconf.ex_flags)
        raw_notify(player, "The 'examine' command lists the flag names for the object's flags.");
    if (!mudconf.quiet_look)
        raw_notify(player, "The 'look' command shows visible attributes in addition to the description.");
    if (mudconf.see_own_dark)
        raw_notify(player, "The 'look' command lists DARK objects owned by you.");
    if (!mudconf.dark_sleepers)
        raw_notify(player, "The 'look' command shows disconnected players.");
    if (mudconf.terse_look)
        raw_notify(player, "The 'look' command obeys the TERSE flag.");
    if (mudconf.trace_topdown)
    {
        raw_notify(player, "Trace output is presented top-down (whole expression first, then sub-exprs).");
        raw_notify(player, tprintf("Only %d lines of trace output are displayed.", mudconf.trace_limit));
    }
    else
    {
        raw_notify(player, "Trace output is presented bottom-up (subexpressions first).");
    }
    if (!mudconf.quiet_whisper)
        raw_notify(player, "The 'whisper' command lets others in the room with you know you whispered.");
    if (mudconf.pemit_players)
        raw_notify(player, "The '@pemit' command may be used to emit to faraway players.");
    if (!mudconf.terse_contents)
        raw_notify(player, "The TERSE flag suppresses listing the contents of a location.");
    if (!mudconf.terse_exits)
        raw_notify(player, "The TERSE flag suppresses listing obvious exits in a location.");
    if (!mudconf.terse_movemsg)
        raw_notify(player, "The TERSE flag suppresses enter/leave/succ/drop messages generated by moving.");
    if (mudconf.pub_flags)
        raw_notify(player, "The 'flags()' function will return the flags of any object.");
    if (mudconf.read_rem_desc)
        raw_notify(player, "The 'get()' function will return the description of faraway objects,");
    if (mudconf.read_rem_name)
        raw_notify(player, "The 'name()' function will return the name of faraway objects.");
    raw_notify(player, tprintf("The default switch for the '@switch' command is %s.", switchd[mudconf.switch_df_all]));
    raw_notify(player, tprintf("The default switch for the 'examine' command is %s.", examd[mudconf.exam_public]));
    if (mudconf.sweep_dark)
        raw_notify(player, "Players may @sweep dark locations.");
    if (mudconf.fascist_tport)
        raw_notify(player, "You may only @teleport out of locations that are JUMP_OK or that you control.");
    raw_notify(player,
           tprintf("Players may have at most %d commands in the queue at one time.",
               mudconf.queuemax));
    if (mudconf.match_mine)
    {
        if (mudconf.match_mine_pl)
            raw_notify(player, "All objects search themselves for $-commands.");
        else
            raw_notify(player, "Objects other than players search themselves for $-commands.");
    }
    if (!Wizard(player))
        return;
    buff = alloc_mbuf("list_options");

    raw_notify(player,
           tprintf("%d commands are run from the queue when there is no net activity.",
               mudconf.queue_chunk));
    raw_notify(player,
           tprintf("%d commands are run from the queue when there is net activity.",
               mudconf.active_q_chunk));
    if (mudconf.idle_wiz_dark)
        raw_notify(player, "Wizards idle for longer than the default timeout are automatically set DARK.");
    if (mudconf.safe_unowned)
        raw_notify(player, "Objects not owned by you are automatically considered SAFE.");
    if (mudconf.paranoid_alloc)
        raw_notify(player, "The buffer pools are checked for consistency on each allocate or free.");
    if (mudconf.cache_names)
        raw_notify(player, "A separate name cache is used.");
#ifndef WIN32
    if (mudconf.fork_dump)
    {
        raw_notify(player, "Database dumps are performed by a fork()ed process.");
    }
#endif
    if (mudconf.max_players >= 0)
        raw_notify(player,
        tprintf("There may be at most %d players logged in at once.",
            mudconf.max_players));
    if (mudconf.quotas)
        mux_sprintf(buff, MBUF_SIZE, " and %d quota", mudconf.start_quota);
    else
        *buff = '\0';
    raw_notify(player,
           tprintf("New players are given %d %s to start with.",
               mudconf.paystart, mudconf.many_coins));
    raw_notify(player,
           tprintf("Players are given %d %s each day they connect.",
               mudconf.paycheck, mudconf.many_coins));
    raw_notify(player,
      tprintf("Earning money is difficult if you have more than %d %s.",
          mudconf.paylimit, mudconf.many_coins));
    if (mudconf.payfind > 0)
        raw_notify(player,
               tprintf("Players have a 1 in %d chance of finding a %s each time they move.",
                   mudconf.payfind, mudconf.one_coin));
    raw_notify(player,
           tprintf("The head of the object freelist is #%d.",
               mudstate.freelist));

    mux_sprintf(buff, MBUF_SIZE, "Intervals: Dump...%d  Clean...%d  Idlecheck...%d",
        mudconf.dump_interval, mudconf.check_interval,
        mudconf.idle_interval);
    raw_notify(player, buff);

    CLinearTimeDelta ltdDump = mudstate.dump_counter - ltaNow;
    CLinearTimeDelta ltdCheck = mudstate.check_counter - ltaNow;
    CLinearTimeDelta ltdIdle = mudstate.idle_counter - ltaNow;

    long lDump  = ltdDump.ReturnSeconds();
    long lCheck = ltdCheck.ReturnSeconds();
    long lIdle  = ltdIdle.ReturnSeconds();
    mux_sprintf(buff, MBUF_SIZE, "Timers: Dump...%ld  Clean...%ld  Idlecheck...%ld",
        lDump, lCheck, lIdle);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, "Timeouts: Idle...%d  Connect...%d  Tries...%d",
        mudconf.idle_timeout, mudconf.conn_timeout,
        mudconf.retry_limit);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, "Scheduling: Timeslice...%s  Max_Quota...%d  Increment...%d",
        mudconf.timeslice.ReturnSecondsString(3),mudconf.cmd_quota_max,
        mudconf.cmd_quota_incr);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, "Spaces...%s  Savefiles...%s",
        ed[mudconf.space_compress], ed[mudconf.compress_db]);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, "New characters: Room...#%d  Home...#%d  DefaultHome...#%d  Quota...%d",
        mudconf.start_room, mudconf.start_home, mudconf.default_home,
        mudconf.start_quota);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, "Misc: GuestChar...#%d  IdleQueueChunk...%d  ActiveQueueChunk...%d  Master_room...#%d",
        mudconf.guest_char, mudconf.queue_chunk,
        mudconf.active_q_chunk, mudconf.master_room);
    raw_notify(player, buff);

    free_mbuf(buff);
}

// ---------------------------------------------------------------------------
// list_vattrs: List user-defined attributes
//
static void list_vattrs(dbref player, char *s_mask)
{
    bool wild_mtch =  s_mask
                   && s_mask[0] != '\0';

    char *buff = alloc_lbuf("list_vattrs");

    // If wild_match, then only list attributes that match wildcard(s)
    //
    char *p = tprintf("--- User-Defined Attributes %s---",
        wild_mtch ? "(wildmatched) " : "");
    raw_notify(player, p);

    ATTR *va;
    int na;
    int wna = 0;

    for (va = vattr_first(), na = 0; va; va = vattr_next(va), na++)
    {
        if (!(va->flags & AF_DELETED))
        {
            // We need to be extremely careful that s_mask is !null and valid
            //
            if (wild_mtch)
            {
                mudstate.wild_invk_ctr = 0;
                if (!quick_wild(s_mask, va->name))
                {
                    continue;
                }
                wna++;
            }
            mux_sprintf(buff, LBUF_SIZE, "%s(%d):", va->name, va->number);
            listset_nametab(player, attraccess_nametab, va->flags, buff, true);
        }
    }

    if (wild_mtch)
    {
        p = tprintf("%d attributes matched, %d attributes total, next=%d", wna,
            na, mudstate.attr_next);
    }
    else
    {
        p = tprintf("%d attributes, next=%d", na, mudstate.attr_next);
    }
    raw_notify(player, p);
    free_lbuf(buff);
}

static int LeftJustifyString(char *field, int nWidth, const char *value)
{
    int n = strlen(value);
    if (n > nWidth)
    {
        n = nWidth;
    }
    memcpy(field, value, n);
    memset(field+n, ' ', nWidth-n);
    return nWidth;
}

static size_t RightJustifyNumber(char *field, size_t nWidth, INT64 value)
{
    char   buffer[22];
    size_t nReturn = 0;
    if (nWidth < sizeof(buffer))
    {
        size_t n = mux_i64toa(value, buffer);
        if (n < sizeof(buffer))
        {
            nReturn = n;
            if (n < nWidth)
            {
                memset(field, ' ', nWidth-n);
                field += nWidth-n;
                nReturn = nWidth;
            }
            memcpy(field, buffer, n);
        }
    }
    return nReturn;
}

// list_hashstats: List information from hash tables
//
static void list_hashstat(dbref player, const char *tab_name, CHashTable *htab)
{
    unsigned int hashsize;
    int          entries, max_scan;
    INT64        deletes, scans, hits, checks;

    htab->GetStats(&hashsize, &entries, &deletes, &scans, &hits, &checks,
        &max_scan);

    char buff[MBUF_SIZE];
    char *p = buff;

    p += LeftJustifyString(p,  15, tab_name); *p++ = ' ';
    p += RightJustifyNumber(p,  4, hashsize); *p++ = ' ';
    p += RightJustifyNumber(p,  6, entries);  *p++ = ' ';
    p += RightJustifyNumber(p,  9, deletes);  *p++ = ' ';
    p += RightJustifyNumber(p, 11, scans);    *p++ = ' ';
    p += RightJustifyNumber(p, 11, hits);     *p++ = ' ';
    p += RightJustifyNumber(p, 11, checks);   *p++ = ' ';
    p += RightJustifyNumber(p,  4, max_scan); *p = '\0';
    raw_notify(player, buff);
}

static void list_hashstats(dbref player)
{
    raw_notify(player, "Hash Stats      Size Entries Deleted      Lookups        Hits     Checks Longest");
    list_hashstat(player, "Commands", &mudstate.command_htab);
    list_hashstat(player, "Logged-out Cmds", &mudstate.logout_cmd_htab);
    list_hashstat(player, "Functions", &mudstate.func_htab);
    list_hashstat(player, "Flags", &mudstate.flags_htab);
    list_hashstat(player, "Powers", &mudstate.powers_htab);
    list_hashstat(player, "Attr names", &mudstate.attr_name_htab);
    list_hashstat(player, "Vattr names", &mudstate.vattr_name_htab);
    list_hashstat(player, "Player Names", &mudstate.player_htab);
    list_hashstat(player, "Net Descriptors", &mudstate.desc_htab);
    list_hashstat(player, "Forwardlists", &mudstate.fwdlist_htab);
    list_hashstat(player, "Overlaid $-cmds", &mudstate.parent_htab);
    list_hashstat(player, "Mail messages", &mudstate.mail_htab);
    list_hashstat(player, "Channel names", &mudstate.channel_htab);
    list_hashstat(player, "Attribute Cache", &mudstate.acache_htab);
    for (int i = 0; i < mudstate.nHelpDesc; i++)
    {
        list_hashstat(player, mudstate.aHelpDesc[i].pBaseFilename,
            mudstate.aHelpDesc[i].ht);
    }
}


// ---------------------------------------------------------------------------
// list_db_stats: Get useful info from the DB layer about hash stats, etc.
//
static void list_db_stats(dbref player)
{
#ifdef MEMORY_BASED
    raw_notify(player, "Database is memory based.");
#else // MEMORY_BASED
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    CLinearTimeDelta ltd = lsaNow - cs_ltime;
    raw_notify(player, tprintf("DB Cache Stats   Writes       Reads  (over %d seconds)", ltd.ReturnSeconds()));
    raw_notify(player, tprintf("Calls      %12d%12d", cs_writes, cs_reads));
    raw_notify(player, tprintf("\nDeletes    %12d", cs_dels));
    raw_notify(player, tprintf("Syncs      %12d", cs_syncs));
    raw_notify(player, tprintf("I/O        %12d%12d", cs_dbwrites, cs_dbreads));
    raw_notify(player, tprintf("Cache Hits %12d%12d", cs_whits, cs_rhits));
#endif // MEMORY_BASED
}

// ---------------------------------------------------------------------------
// list_process: List local resource usage stats of the mux process.
// Adapted from code by Claudius@PythonMUCK,
//     posted to the net by Howard/Dark_Lord.
//
static void list_process(dbref player)
{
#ifdef HAVE_GETRUSAGE
    struct rusage usage;
    int ixrss, idrss, isrss, curr, last, dur;

    getrusage(RUSAGE_SELF, &usage);

    // Calculate memory use from the aggregate totals.
    //
    curr = mudstate.mstat_curr;
    last = 1 - curr;
    dur = mudstate.mstat_secs[curr] - mudstate.mstat_secs[last];
    if (dur > 0)
    {
        ixrss = (mudstate.mstat_ixrss[curr] -
             mudstate.mstat_ixrss[last]) / dur;
        idrss = (mudstate.mstat_idrss[curr] -
             mudstate.mstat_idrss[last]) / dur;
        isrss = (mudstate.mstat_isrss[curr] -
             mudstate.mstat_isrss[last]) / dur;
    }
    else
    {
        ixrss = 0;
        idrss = 0;
        isrss = 0;
    }
#endif // HAVE_GETRUSAGE

#ifdef WIN32
#ifdef HAVE_GETRUSAGE
    int maxfds = FD_SETSIZE;
#endif // HAVE_GETRUSAGE
#else // WIN32
#ifdef HAVE_GETDTABLESIZE
    int maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    int maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE
    int psize = getpagesize();
#endif // WIN32

    // Go display everything
    //
#ifdef WIN32
    raw_notify(player, tprintf("Process ID:  %10d", game_pid));
#else // WIN32
    raw_notify(player, tprintf("Process ID:  %10d        %10d bytes per page", game_pid, psize));
#endif // WIN32

#ifdef HAVE_GETRUSAGE
    raw_notify(player, tprintf("Time used:   %10d user   %10d sys",
               usage.ru_utime.tv_sec, usage.ru_stime.tv_sec));
    raw_notify(player, tprintf("Resident mem:%10d shared %10d private%10d stack",
           ixrss, idrss, isrss));
    raw_notify(player,
           tprintf("Integral mem:%10d shared %10d private%10d stack",
               usage.ru_ixrss, usage.ru_idrss, usage.ru_isrss));
    raw_notify(player,
           tprintf("Max res mem: %10d pages  %10d bytes",
               usage.ru_maxrss, (usage.ru_maxrss * psize)));
    raw_notify(player,
           tprintf("Page faults: %10d hard   %10d soft   %10d swapouts",
               usage.ru_majflt, usage.ru_minflt, usage.ru_nswap));
    raw_notify(player,
           tprintf("Disk I/O:    %10d reads  %10d writes",
               usage.ru_inblock, usage.ru_oublock));
    raw_notify(player,
           tprintf("Network I/O: %10d in     %10d out",
               usage.ru_msgrcv, usage.ru_msgsnd));
    raw_notify(player,
           tprintf("Context swi: %10d vol    %10d forced %10d sigs",
               usage.ru_nvcsw, usage.ru_nivcsw, usage.ru_nsignals));
    raw_notify(player,
           tprintf("Descs avail: %10d", maxfds));
#endif // HAVE_GETRUSAGE
}

//----------------------------------------------------------------------------
// list_rlevels
//


#ifdef REALITY_LVLS
static void list_rlevels(dbref player)
{
    int i;
    raw_notify(player, "Reality levels:");
    for(i = 0; i < mudconf.no_levels; ++i)
        raw_notify(player, tprintf("    Level: %-20.20s    Value: 0x%08x     Desc: %s",
            mudconf.reality_level[i].name, mudconf.reality_level[i].value,
                mudconf.reality_level[i].attr));
    raw_notify(player, "--Completed.");
}
#endif /* REALITY_LVLS */

// ---------------------------------------------------------------------------
// do_list: List information stored in internal structures.
//
#define LIST_ATTRIBUTES 1
#define LIST_COMMANDS   2
#define LIST_COSTS      3
#define LIST_FLAGS      4
#define LIST_FUNCTIONS  5
#define LIST_GLOBALS    6
#define LIST_ALLOCATOR  7
#define LIST_LOGGING    8
#define LIST_DF_FLAGS   9
#define LIST_PERMS      10
#define LIST_ATTRPERMS  11
#define LIST_OPTIONS    12
#define LIST_HASHSTATS  13
#define LIST_BUFTRACE   14
#define LIST_CONF_PERMS 15
#define LIST_SITEINFO   16
#define LIST_POWERS     17
#define LIST_SWITCHES   18
#define LIST_VATTRS     19
#define LIST_DB_STATS   20
#define LIST_PROCESS    21
#define LIST_BADNAMES   22
#define LIST_RESOURCES  23
#define LIST_GUESTS     24
#ifdef REALITY_LVLS
#define LIST_RLEVELS    25
#endif

NAMETAB list_names[] =
{
    {"allocations",        2,  CA_WIZARD,  LIST_ALLOCATOR},
    {"attr_permissions",   5,  CA_WIZARD,  LIST_ATTRPERMS},
    {"attributes",         2,  CA_PUBLIC,  LIST_ATTRIBUTES},
    {"bad_names",          2,  CA_WIZARD,  LIST_BADNAMES},
    {"buffers",            2,  CA_WIZARD,  LIST_BUFTRACE},
    {"commands",           3,  CA_PUBLIC,  LIST_COMMANDS},
    {"config_permissions", 3,  CA_GOD,     LIST_CONF_PERMS},
    {"costs",              3,  CA_PUBLIC,  LIST_COSTS},
    {"db_stats",           2,  CA_WIZARD,  LIST_DB_STATS},
    {"default_flags",      1,  CA_PUBLIC,  LIST_DF_FLAGS},
    {"flags",              2,  CA_PUBLIC,  LIST_FLAGS},
    {"functions",          2,  CA_PUBLIC,  LIST_FUNCTIONS},
    {"globals",            2,  CA_WIZARD,  LIST_GLOBALS},
    {"hashstats",          1,  CA_WIZARD,  LIST_HASHSTATS},
    {"logging",            1,  CA_GOD,     LIST_LOGGING},
    {"options",            1,  CA_PUBLIC,  LIST_OPTIONS},
    {"permissions",        2,  CA_WIZARD,  LIST_PERMS},
    {"powers",             2,  CA_WIZARD,  LIST_POWERS},
    {"process",            2,  CA_WIZARD,  LIST_PROCESS},
    {"resources",          1,  CA_WIZARD,  LIST_RESOURCES},
    {"site_information",   2,  CA_WIZARD,  LIST_SITEINFO},
    {"switches",           2,  CA_PUBLIC,  LIST_SWITCHES},
    {"user_attributes",    1,  CA_WIZARD,  LIST_VATTRS},
    {"guests",             2,  CA_WIZARD,  LIST_GUESTS},
#ifdef REALITY_LVLS
    {"rlevels",            3,  CA_PUBLIC,  LIST_RLEVELS},
#endif
    { NULL,                0,  0,          0}
};

void do_list(dbref executor, dbref caller, dbref enactor, int extra,
             char *arg)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(extra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, arg);
    mux_strtok_ctl(&tts, " \t=,");
    char *s_option = mux_strtok_parse(&tts);

    int flagvalue;
    if (!search_nametab(executor, list_names, arg, &flagvalue))
    {
        if (flagvalue == -1)
        {
            display_nametab(executor, list_names, "Unknown option.  Use one of:", true);
        }
        else
        {
            notify(executor, "Permission denied");
        }
        return;
    }

    switch (flagvalue)
    {
    case LIST_ALLOCATOR:
        list_bufstats(executor);
        break;
    case LIST_BUFTRACE:
        list_buftrace(executor);
        break;
    case LIST_ATTRIBUTES:
        list_attrtable(executor);
        break;
    case LIST_COMMANDS:
        list_cmdtable(executor);
        break;
    case LIST_SWITCHES:
        list_cmdswitches(executor);
        break;
    case LIST_COSTS:
        list_costs(executor);
        break;
    case LIST_OPTIONS:
        list_options(executor);
        break;
    case LIST_HASHSTATS:
        list_hashstats(executor);
        break;
    case LIST_SITEINFO:
        list_siteinfo(executor);
        break;
    case LIST_FLAGS:
        display_flagtab(executor);
        break;
    case LIST_FUNCTIONS:
        list_functable(executor);
        break;
    case LIST_GLOBALS:
        interp_nametab(executor, enable_names, mudconf.control_flags,
                "Global parameters:", "enabled", "disabled");
        break;
    case LIST_DF_FLAGS:
        list_df_flags(executor);
        break;
    case LIST_PERMS:
        list_cmdaccess(executor);
        break;
    case LIST_CONF_PERMS:
        list_cf_access(executor);
        break;
    case LIST_POWERS:
        display_powertab(executor);
        break;
    case LIST_ATTRPERMS:
        list_attraccess(executor);
        break;
    case LIST_VATTRS:
        s_option = mux_strtok_parse(&tts);
        list_vattrs(executor, s_option);
        break;
    case LIST_LOGGING:
        interp_nametab(executor, logoptions_nametab, mudconf.log_options,
                   "Events Logged:", "enabled", "disabled");
        interp_nametab(executor, logdata_nametab, mudconf.log_info,
                   "Information Logged:", "yes", "no");
        break;
    case LIST_DB_STATS:
        list_db_stats(executor);
        break;
    case LIST_PROCESS:
        list_process(executor);
        break;
    case LIST_BADNAMES:
        badname_list(executor, "Disallowed names:");
        break;
    case LIST_RESOURCES:
        list_system_resources(executor);
        break;
    case LIST_GUESTS:
        Guest.ListAll(executor);
        break;
#ifdef REALITY_LVLS
    case LIST_RLEVELS:
        list_rlevels(executor);
        break;
#endif
    }
}

void do_break(dbref executor, dbref caller, dbref enactor, int key, char *arg1)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);

    break_called = xlate(arg1);
}

// do_icmd: Ignore or disable commands on a per-player or per-room basis.
// Used with express permission of RhostMUSH developers.
// Bludgeoned into MUX by Jake Nelson 7/2002.
//
void do_icmd(dbref player, dbref cause, dbref enactor, int key, char *name,
             char *args[], int nargs)
{
    UNUSED_PARAMETER(cause);
    UNUSED_PARAMETER(enactor);

    CMDENT *cmdp;
    char *buff1, *pt1, *pt2, *pt3, *atrpt, *pt5;
    int x, aflags, y;
    dbref target = NOTHING, aowner, zone;
    bool bFound, set;

    int loc_set = -1;
    if (  key == ICMD_IROOM
       || key == ICMD_DROOM
       || key == ICMD_CROOM
       || key == ICMD_LROOM
       || key == ICMD_LALLROOM)
    {
        if (key != ICMD_LALLROOM)
        {
            target = match_thing_quiet(player, name);
        }
        if (  key != ICMD_LALLROOM
           && (  !Good_obj(target)
              || !( isRoom(target)
                 || isThing(target))))
        {
            notify(player, "@icmd: Bad Location.");
            return;
        }
        if (key == ICMD_CROOM)
        {
            atr_clr(target, A_CMDCHECK);
            notify(player, "@icmd: Location - All cleared.");
            notify(player, "@icmd: Done.");
            return;
        }
        else if (key == ICMD_LROOM)
        {
            atrpt = atr_get(target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player,"Location CmdCheck attribute is:");
                notify(player, atrpt);
            }
            else
            {
                notify(player, "Location CmdCheck attribute is empty.");
            }
            free_lbuf(atrpt);
            notify(player, "@icmd: Done.");
            return;
        }
        else if (key == ICMD_LALLROOM)
        {
            target = Location(player);
            if (  !Good_obj(target)
               || Going(target)
               || isPlayer(target))
            {
                notify(player, "@icmd: Bad Location.");
                return;
            }
            notify(player, "Scanning all locations and zones from your current location:");
            bFound = false;
            atrpt = atr_get(target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player, tprintf("%c     --- At %s(#%d) :",
                    (Zone(target) == target ? '*' : ' '), Name(target), target));
                notify(player, atrpt);
                bFound = true;
            }
            free_lbuf(atrpt);
            if (Zone(target) != target)
            {
                zone = Zone(target);
                if (  Good_obj(zone)
                   && (  isRoom(zone)
                      || isThing(zone)))
                {
                    atrpt = atr_get(zone, A_CMDCHECK, &aowner, &aflags);
                    if (*atrpt)
                    {
                        notify(player,tprintf("%c     z-- At %s(#%d) :",
                            '*', Name(zone), zone));
                        notify(player, atrpt);
                        bFound = true;
                    }
                    free_lbuf(atrpt);
                }
            }
            if (!bFound)
            {
                notify(player, "@icmd: Location - No icmd's found at current location.");
            }
            notify(player, "@icmd: Done.");
            return;
        }
        else if (key == ICMD_IROOM)
        {
            loc_set = 1;
        }
        else if (key == ICMD_DROOM)
        {
            loc_set = 0;
        }
    }

    if (loc_set == -1 )
    {
        target = lookup_player(player, name, false);
        if (!Good_obj(target) || God(target))
        {
            notify(player, "@icmd: Bad player.");
            return;
        }
        if ((key == ICMD_OFF) || (key == ICMD_CLEAR))
        {
            s_Flags(target, FLAG_WORD3, Flags3(target) & ~CMDCHECK);
            if (key == ICMD_CLEAR)
            {
                atr_clr(target, A_CMDCHECK);
            }
            notify(player, "@icmd: All cleared.");
            notify(player, "@icmd: Done.");
            return;
        }
        else if (key == ICMD_ON)
        {
            s_Flags(target, FLAG_WORD3, Flags3(target) | CMDCHECK);
            notify(player, "@icmd: Activated.");
            notify(player, "@icmd: Done.");
            return;
        }
        else if (key == ICMD_CHECK)
        {
            if (CmdCheck(target))
            {
                notify(player, "CmdCheck is active.");
            }
            else
            {
                notify(player, "CmdCheck is not active.");
            }
            atrpt = atr_get(target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player, "CmdCheck attribute is:");
                notify(player, atrpt);
            }
            else
            {
                notify(player, "CmdCheck attribute is empty.");
            }
            free_lbuf(atrpt);
            notify(player, "@icmd: Done.");
            return;
        }
    }
    else
    {
        key = loc_set;
    }
    char *message = "";
    buff1 = alloc_lbuf("do_icmd");
    for (x = 0; x < nargs; x++)
    {
        pt1 = args[x];
        pt2 = buff1;
        while (  *pt1
              && pt2 < buff1 + LBUF_SIZE)
        {
            *pt2++ = mux_tolower(*pt1++);
        }
        *pt2 = '\0';
        if (  buff1[0] == '!'
           && buff1[1] != '\0')
        {
            pt1 = buff1 + 1;
            set = false;
        }
        else
        {
            pt1 = buff1;
            set = true;
        }
        if (*pt1)
        {
            bool bHome, bColon = false;
            if (!string_compare(pt1, "home"))
            {
                bHome = true;
                cmdp = NULL;
            }
            else
            {
                bHome = false;
                cmdp = (CMDENT *) hashfindLEN(pt1, strlen(pt1), &mudstate.command_htab);
            }
            if (cmdp || bHome)
            {
                atrpt = atr_get(target, A_CMDCHECK, &aowner, &aflags);
                if (cmdp)
                {
                    aflags = strlen(cmdp->cmdname);
                    bColon = (  aflags == 1
                             && *(cmdp->cmdname) == ':');
                }
                else
                {
                    aflags = 4;
                }
                pt5 = atrpt;
                while (pt1)
                {
                    if (cmdp)
                    {
                        if (bColon)
                        {
                            pt1 = strstr(pt5, "::");
                            if (pt1)
                            {
                                pt1++;
                            }
                        }
                        else
                        {
                            pt1 = strstr(pt5, cmdp->cmdname);
                        }
                    }
                    else
                    {
                        pt1 = strstr(pt5, "home");
                    }
                    if (  pt1
                       && (pt1 > atrpt)
                       && (*(pt1 - 1) == ':')
                       && (  mux_isspace(*(pt1 + aflags))
                          || !*(pt1 + aflags)))
                    {
                        break;
                    }
                    else if (pt1)
                    {
                        if (*pt1)
                        {
                            pt5 = pt1 + 1;
                        }
                        else
                        {
                            pt1 = NULL;
                            break;
                        }
                    }
                }
                if (set)
                {
                    if (!pt1)
                    {
                        if (*atrpt && (strlen(atrpt) < LBUF_SIZE - 2))
                        {
                            strcat(atrpt, " ");
                        }

                        if (cmdp)
                        {
                            pt3 = tprintf("%d:%s", key + 1, cmdp->cmdname);
                        }
                        else
                        {
                            pt3 = tprintf("%d:home", key + 1);
                        }

                        size_t natrpt = strlen(atrpt);
                        size_t npt3 = strlen(pt3);
                        if ((natrpt + npt3) < LBUF_SIZE - 1)
                        {
                            strcat(atrpt, pt3);
                            atr_add_raw(target, A_CMDCHECK, atrpt);
                            if ( loc_set == -1 )
                            {
                                s_Flags(target, FLAG_WORD3, Flags3(target) | CMDCHECK);
                            }
                            message = "Set";
                        }
                    }
                    else
                    {
                        message = "Command already present";
                    }
                }
                else
                {
                    if (pt1)
                    {
                        pt2 = pt1 - 1;
                        while ((pt2 > atrpt) && !mux_isspace(*pt2))
                        {
                            pt2--;
                        }
                        y = pt2 - atrpt + 1;
                        strncpy(buff1, atrpt, y);
                        if (y == 1)
                        {
                            *atrpt = '\0';
                        }
                        *(atrpt + y) = '\0';
                        pt2 = pt1 + aflags;
                        if (*pt2)
                        {
                            while (*pt2 && mux_isspace(*pt2))
                            {
                                pt2++;
                            }
                            if (*pt2)
                            {
                                strcat(atrpt,pt2);
                            }
                        }
                        if ((y > 1) && !*pt2)
                        {
                            pt2 = atrpt + y;
                            while ((pt2 > atrpt) && mux_isspace(*pt2))
                            {
                                pt2--;
                            }
                            *(pt2 + 1) = '\0';
                        }
                        if ((y == 1) && !*pt2)
                        {
                            atr_clr(target, A_CMDCHECK);
                            if (loc_set == -1)
                            {
                                s_Flags(target, FLAG_WORD3, Flags3(target) & ~CMDCHECK);
                            }
                            message = "Cleared";
                        }
                        else
                        {
                            atr_add_raw(target, A_CMDCHECK, atrpt);
                            message = "Cleared";
                        }
                    }
                    else
                    {
                        message = "Command not present";
                    }
                }
                free_lbuf(atrpt);
            }
            else
            {
                message = "Bad command";
            }
            notify(player, tprintf("@icmd:%s %s.",(loc_set == -1) ? "" : " Location -", message));
        }
    }
    free_lbuf(buff1);
    notify(player,"@icmd: Done.");
}

// do_train: show someone else in the same room what code you're entering and the result
// From RhostMUSH, changed to use notify_all_from_inside.
//
void do_train(dbref executor, dbref caller, dbref enactor, int key, char *string)
{
    UNUSED_PARAMETER(key);

    dbref loc = Location(executor);
    if (!Good_obj(loc))
    {
        notify(executor, "Bad location.");
        return;
    }
    if (  !string
       || !*string)
    {
        notify(executor, "Train requires an argument.");
        return;
    }

    notify_all_from_inside(loc, executor, tprintf("%s types -=> %s",
        Moniker(executor), string));
    process_command(executor, caller, enactor, true, string, (char **)NULL, 0);
}

void do_moniker(dbref executor, dbref caller, dbref enactor, int key,
                 int nfargs, char *name, char *instr)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nfargs);

    dbref thing = match_thing(executor, name);
    if ( !(  Good_obj(thing)
          && Controls(executor, thing)))
    {
        notify(executor, "Permission denied.");
        return;
    }

    if (  instr == NULL
       || instr[0] == '\0')
    {
        notify_quiet(executor, "Moniker cleared.");
        s_Moniker(thing, NULL);
    }
    else
    {
        s_Moniker(thing, instr);
        if (  !Quiet(executor)
           && !Quiet(thing))
        {
            notify_quiet(executor, "Moniker set.");
        }
    }
    set_modified(thing);
}

// do_hook: run softcode before or after running a hardcode command, or
// softcode access. Original idea from TinyMUSH 3, code from RhostMUSH.
// Used with express permission of RhostMUSH developers.
// Bludgeoned into MUX by Jake Nelson 7/2002.
//
static void show_hook(char *bf, char *bfptr, int key)
{
    if (key & HOOK_BEFORE)
        safe_str("before ", bf, &bfptr);
    if (key & HOOK_AFTER)
        safe_str("after ", bf, &bfptr);
    if (key & HOOK_PERMIT)
        safe_str("permit ", bf, &bfptr);
    if (key & HOOK_IGNORE)
        safe_str("ignore ", bf, &bfptr);
    if (key & HOOK_IGSWITCH)
        safe_str("igswitch ", bf, &bfptr);
    if (key & HOOK_AFAIL)
        safe_str("afail ", bf, &bfptr);
    *bfptr = '\0';
}

static void hook_loop(dbref executor, CMDENT *cmdp, char *s_ptr, char *s_ptrbuff)
{
    show_hook(s_ptrbuff, s_ptr, cmdp->hookmask);
    const char *pFmt = "%-32.32s | %s";
    const char *pCmd = cmdp->cmdname;
    if (  pCmd[0] != '\0'
       && pCmd[1] == '\0')
    {
        switch (pCmd[0])
        {
        case '"':
            pFmt = "S %-30.30s | %s";
            pCmd = "('\"' hook on 'say')";
            break;
        case ':':
            pFmt = "P %-30.30s | %s";
            pCmd = "(':' hook on 'pose')";
            break;
        case ';':
            pFmt = "P %-30.30s | %s";
            pCmd = "(';' hook on 'pose')";
            break;
        case '\\':
            pFmt = "E %-30.30s | %s";
            pCmd = "('\\\\' hook on '@emit')";
            break;
        case '#':
            pFmt = "F %-30.30s | %s";
            pCmd = "('#' hook on '@force')";
            break;
        case '&':
            pFmt = "V %-30.30s | %s";
            pCmd = "('&' hook on '@set')";
            break;
        case '-':
            pFmt = "M %-30.30s | %s";
            pCmd = "('-' hook on '@mail')";
            break;
        case '~':
            pFmt = "M %-30.30s | %s";
            pCmd = "('~' hook on '@mail')";
            break;
        }
    }
    notify(executor, tprintf(pFmt, pCmd, s_ptrbuff));
}

void do_hook(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    bool negate, found;
    char *s_ptr, *s_ptrbuff, *cbuff, *p;
    const char *q;
    CMDENT *cmdp = (CMDENT *)NULL;

    if (  (  key
          && !(key & HOOK_LIST))
       || (  (  !key
             || (key & HOOK_LIST))
          && *name))
    {
        cmdp = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);
        if (!cmdp)
        {
            notify(executor, "@hook: Non-existent command name given.");
            return;
        }
    }
    if (  (key & HOOK_CLEAR)
       && (key & HOOK_LIST))
    {
        notify(executor, "@hook: Incompatible switches.");
        return;
    }

    if (key & HOOK_CLEAR)
    {
        negate = true;
        key = key & ~HOOK_CLEAR;
        key = key & ~SW_MULTIPLE;
    }
    else
    {
        negate = false;
    }

    if (key & (HOOK_BEFORE|HOOK_AFTER|HOOK_PERMIT|HOOK_IGNORE|HOOK_IGSWITCH|HOOK_AFAIL))
    {
        if (negate)
        {
            cmdp->hookmask = cmdp->hookmask & ~key;
        }
        else
        {
            cmdp->hookmask = cmdp->hookmask | key;
        }
        if (cmdp->hookmask)
        {
            s_ptr = s_ptrbuff = alloc_lbuf("@hook");
            show_hook(s_ptrbuff, s_ptr, cmdp->hookmask);
            notify(executor, tprintf("@hook: New mask for '%s' -> %s", cmdp->cmdname, s_ptrbuff));
            free_lbuf(s_ptrbuff);
        }
        else
        {
            notify(executor, tprintf("@hook: New mask for '%s' is empty.", cmdp->cmdname));
        }
    }
    if (  (key & HOOK_LIST)
       || !key)
    {
        if (cmdp)
        {
            if (cmdp->hookmask)
            {
                s_ptr = s_ptrbuff = alloc_lbuf("@hook");
                show_hook(s_ptrbuff, s_ptr, cmdp->hookmask);
                notify(executor, tprintf("@hook: Mask for hashed command '%s' -> %s", cmdp->cmdname, s_ptrbuff));
                free_lbuf(s_ptrbuff);
            }
            else
            {
                notify(executor, tprintf("@hook: Mask for hashed command '%s' is empty.", cmdp->cmdname));
            }
        }
        else
        {
            notify(executor, tprintf("%.32s-+-%s",
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf("%-32s | %s", "Built-in Command", "Hook Mask Values"));
            notify(executor, tprintf("%.32s-+-%s",
                "--------------------------------",
                "--------------------------------------------"));
            found = false;
            s_ptr = s_ptrbuff = alloc_lbuf("@hook");
            {
                CMDENT_NO_ARG *cmdp2;
                for (cmdp2 = command_table_no_arg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_ONE_ARG *cmdp2;
                for (cmdp2 = command_table_one_arg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_ONE_ARG_CMDARG *cmdp2;
                for (cmdp2 = command_table_one_arg_cmdarg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_TWO_ARG *cmdp2;
                for (cmdp2 = command_table_two_arg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_TWO_ARG_ARGV *cmdp2;
                for (cmdp2 = command_table_two_arg_argv; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_TWO_ARG_CMDARG *cmdp2;
                for (cmdp2 = command_table_two_arg_cmdarg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            {
                CMDENT_TWO_ARG_ARGV_CMDARG *cmdp2;
                for (cmdp2 = command_table_two_arg_argv_cmdarg; cmdp2->cmdname; cmdp2++)
                {
                    s_ptrbuff[0] = '\0';
                    s_ptr = s_ptrbuff;
                    if (cmdp2->hookmask)
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            if (!found)
            {
                notify(executor, tprintf("%26s -- No @hooks defined --", " "));
            }
            found = false;
            /* We need to search the attribute table as well */
            notify(executor, tprintf("%.32s-+-%s",
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf("%-32s | %s", "Built-in Attribute", "Hook Mask Values"));
            notify(executor, tprintf("%.32s-+-%s",
                "--------------------------------",
                "--------------------------------------------"));
            cbuff = alloc_sbuf("cbuff_hook");
            for (ATTR *ap = attr; ap->name; ap++)
            {
                if (ap->flags & AF_NOCMD)
                {
                    continue;
                }
                s_ptrbuff[0] = '\0';
                s_ptr = s_ptrbuff;

                p = cbuff;
                safe_sb_chr('@', cbuff, &p);
                for (q = ap->name; *q; q++)
                {
                    safe_sb_chr(mux_tolower(*q), cbuff, &p);
                }
                *p = '\0';
                size_t ncbuff = p - cbuff;
                cmdp = (CMDENT *)hashfindLEN(cbuff, ncbuff, &mudstate.command_htab);
                if (  cmdp
                   && cmdp->hookmask)
                {
                    found = true;
                    show_hook(s_ptrbuff, s_ptr, cmdp->hookmask);
                    notify(executor, tprintf("%-32.32s | %s", cmdp->cmdname, s_ptrbuff));
                }
            }
            free_sbuf(cbuff);
            if (!found)
            {
                notify(executor, tprintf("%26s -- No @hooks defined --", " "));
            }
            free_lbuf(s_ptrbuff);
            notify(executor, tprintf("%.32s-+-%s",
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf("The hook object is currently: #%d (%s)",
                mudconf.hook_obj,
                (  (  Good_obj(mudconf.hook_obj)
                   && !Going(mudconf.hook_obj))
                ? "VALID" : "INVALID")));
        }
    }
}
