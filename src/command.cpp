// command.cpp - command parser and support routines.
// 
// $Id: command.cpp,v 1.38 2001-03-31 04:15:52 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "interface.h"
#include "mudconf.h"
#include "command.h"
#include "functions.h"
#include "match.h"
#include "attrs.h"
#include "flags.h"
#include "powers.h"
#include "alloc.h"
#include "vattr.h"
#include "mail.h"
#include "comsys.h"

extern void FDECL(list_cf_access, (dbref));
extern void FDECL(list_siteinfo, (dbref));
extern void logged_out0(dbref player, dbref cause, int key);
extern void logged_out1(dbref player, dbref cause, int key, char *arg);
extern void boot_slave(dbref, dbref, int);
extern void NDECL(vattr_clean_db);

// Switch tables for the various commands.
//
NAMETAB attrib_sw[] =
{
    {(char *)"access",  1,  CA_GOD,     ATTRIB_ACCESS},
    {(char *)"delete",  1,  CA_GOD,     ATTRIB_DELETE},
    {(char *)"rename",  1,  CA_GOD,     ATTRIB_RENAME},
    { NULL,         0,  0,      0}
};

NAMETAB boot_sw[] =
{
    {(char *)"port",    1,  CA_WIZARD,  BOOT_PORT|SW_MULTIPLE},
    {(char *)"quiet",   1,  CA_WIZARD,  BOOT_QUIET|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB comtitle_sw[] =
  {
    {(char *)"off",      2,  CA_PUBLIC,  1},
    {(char *)"on",       2,  CA_PUBLIC,  2},
  };

NAMETAB cemit_sw[] =
{
    {(char *)"noheader",    1,  CA_PUBLIC,  CEMIT_NOHEADER},
    { NULL,         0,  0,      0}
};

NAMETAB clone_sw[] =
{
    {(char *)"cost",    1,  CA_PUBLIC,  CLONE_SET_COST},
    {(char *)"inherit", 3,  CA_PUBLIC,  CLONE_INHERIT|SW_MULTIPLE},
    {(char *)"inventory",   3,  CA_PUBLIC,  CLONE_INVENTORY},
    {(char *)"location",    1,  CA_PUBLIC,  CLONE_LOCATION},
    {(char *)"parent",  2,  CA_PUBLIC,  CLONE_PARENT|SW_MULTIPLE},
    {(char *)"preserve",    2,  CA_WIZARD,  CLONE_PRESERVE|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB clist_sw[] =
{
    {(char *)"full",        0,      CA_PUBLIC,      CLIST_FULL},
    {(char *)"headers",     0,      CA_PUBLIC,      CLIST_HEADERS},
    { NULL,                 0,      0,              0}
};

NAMETAB cset_sw[] =
{
    {(char *)"anon",        1,      CA_PUBLIC,      CSET_SPOOF},
    {(char *)"list",        2,      CA_PUBLIC,      CSET_LIST},
    {(char *)"loud",        2,      CA_PUBLIC,      CSET_LOUD},
    {(char *)"mute",        1,      CA_PUBLIC,      CSET_QUIET},
    {(char *)"nospoof",     1,      CA_PUBLIC,      CSET_NOSPOOF},
    {(char *)"object",      1,      CA_PUBLIC,      CSET_OBJECT},
    {(char *)"private",     2,      CA_PUBLIC,      CSET_PRIVATE},
    {(char *)"public",      2,      CA_PUBLIC,      CSET_PUBLIC},
    {(char *)"quiet",       1,      CA_PUBLIC,      CSET_QUIET},
    {(char *)"spoof",       1,      CA_PUBLIC,      CSET_SPOOF},
    {(char *)"header",      2,      CA_PUBLIC,      CSET_HEADER},
    { NULL,                 0,      0,              0}
}; 

NAMETAB decomp_sw[] =
{
    {(char *)"dbref",   1,  CA_PUBLIC,  DECOMP_DBREF},
    { NULL,         0,  0,      0}
};

NAMETAB destroy_sw[] =
{
    {(char *)"override",    8,  CA_PUBLIC,  DEST_OVERRIDE},
    { NULL,         0,  0,      0}
};

NAMETAB dig_sw[] =
{
    {(char *)"teleport",    1,  CA_PUBLIC,  DIG_TELEPORT},
    { NULL,         0,  0,      0}
};

NAMETAB doing_sw[] =
{
    {(char *)"header",  1,  CA_PUBLIC,  DOING_HEADER},
    {(char *)"message", 1,  CA_PUBLIC,  DOING_MESSAGE},
    {(char *)"poll",    1,  CA_PUBLIC,  DOING_POLL},
    { NULL,         0,  0,      0}
};

NAMETAB dolist_sw[] =
{
    {(char *)"delimit",     1,      CA_PUBLIC,      DOLIST_DELIMIT},
    {(char *)"space",       1,      CA_PUBLIC,      DOLIST_SPACE},
    { NULL,                 0,      0,              0,}
};

NAMETAB drop_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  DROP_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB dump_sw[] =
{
    {(char *)"structure",   1,  CA_WIZARD,  DUMP_STRUCT|SW_MULTIPLE},
    {(char *)"text",        1,  CA_WIZARD,  DUMP_TEXT|SW_MULTIPLE},
    {(char *)"flatfile",    1,  CA_WIZARD,  DUMP_FLATFILE|SW_MULTIPLE},
    { NULL,                 0,  0,          0}
};

NAMETAB emit_sw[] =
{
    {(char *)"here",    2,  CA_PUBLIC,  SAY_HERE|SW_MULTIPLE},
    {(char *)"html",    2,  CA_PUBLIC,  SAY_HTML|SW_MULTIPLE},
    {(char *)"room",    1,  CA_PUBLIC,  SAY_ROOM|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB enter_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB examine_sw[] =
{
    {(char *)"brief",   1,  CA_PUBLIC,  EXAM_BRIEF},
    {(char *)"debug",   1,  CA_WIZARD,  EXAM_DEBUG},
    {(char *)"full",    1,  CA_PUBLIC,  EXAM_LONG},
    {(char *)"parent",  1,  CA_PUBLIC,  EXAM_PARENT},
    { NULL,         0,  0,      0}
};

NAMETAB femit_sw[] =
{
    {(char *)"here",    1,  CA_PUBLIC,  PEMIT_HERE|SW_MULTIPLE},
    {(char *)"room",    1,  CA_PUBLIC,  PEMIT_ROOM|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB fixdb_sw[] =
{
    // {(char *)"add_pname",1,  CA_GOD,     FIXDB_ADD_PN},
    {(char *)"contents",    1,  CA_GOD,     FIXDB_CON},
    {(char *)"exits",   1,  CA_GOD,     FIXDB_EXITS},
    {(char *)"location",    1,  CA_GOD,     FIXDB_LOC},
    {(char *)"next",    1,  CA_GOD,     FIXDB_NEXT},
    {(char *)"owner",   1,  CA_GOD,     FIXDB_OWNER},
    {(char *)"pennies", 1,  CA_GOD,     FIXDB_PENNIES},
    {(char *)"rename",  1,  CA_GOD,     FIXDB_NAME},
    // {(char *)"rm_pname", 1,  CA_GOD,     FIXDB_DEL_PN},
    { NULL,         0,  0,      0}
};

NAMETAB fpose_sw[] =
{
    {(char *)"default", 1,  CA_PUBLIC,  0},
    {(char *)"nospace", 1,  CA_PUBLIC,  SAY_NOSPACE},
    { NULL,         0,  0,      0}
};

NAMETAB function_sw[] =
{
    {(char *)"list",        1,  CA_WIZARD,  FN_LIST},
    {(char *)"preserve",    3,  CA_WIZARD,  FN_PRES|SW_MULTIPLE},
    {(char *)"privileged",  3,  CA_WIZARD,  FN_PRIV|SW_MULTIPLE},
    { NULL,                 0,          0,        0}
};

NAMETAB get_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  GET_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB give_sw[] =
{
    {(char *)"quiet",   1,  CA_WIZARD,  GIVE_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB goto_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB halt_sw[] =
{
    {(char *)"all",     1,  CA_PUBLIC,  HALT_ALL},
    { NULL,         0,  0,      0}
};

NAMETAB leave_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  MOVE_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB listmotd_sw[] =
{
    {(char *)"brief",   1,  CA_WIZARD,  MOTD_BRIEF},
    { NULL,         0,  0,      0}
};

NAMETAB lock_sw[] =
{
    {(char *)"defaultlock", 1,  CA_PUBLIC,  A_LOCK},
    {(char *)"droplock",    1,  CA_PUBLIC,  A_LDROP},
    {(char *)"enterlock",   1,  CA_PUBLIC,  A_LENTER},
    {(char *)"givelock",    1,  CA_PUBLIC,  A_LGIVE},
    {(char *)"leavelock",   2,  CA_PUBLIC,  A_LLEAVE},
    {(char *)"linklock",    2,  CA_PUBLIC,  A_LLINK},
    {(char *)"pagelock",    3,  CA_PUBLIC,  A_LPAGE},
    {(char *)"parentlock",  3,  CA_PUBLIC,  A_LPARENT},
    {(char *)"receivelock", 1,  CA_PUBLIC,  A_LRECEIVE},
    {(char *)"teloutlock",  2,  CA_PUBLIC,  A_LTELOUT},
    {(char *)"tportlock",   2,  CA_PUBLIC,  A_LTPORT},
    {(char *)"uselock", 1,  CA_PUBLIC,  A_LUSE},
    {(char *)"userlock",    4,  CA_PUBLIC,  A_LUSER},
    {(char *)"speechlock",  1,  CA_PUBLIC,  A_LSPEECH},
    { NULL,         0,  0,      0}
};

NAMETAB look_sw[] =
{
    {(char *)"outside",     1,      CA_PUBLIC,      LOOK_OUTSIDE},
    { NULL,                 0,      0,              0}
};

NAMETAB mail_sw[] =
{
    {(char *)"abort",       0,      CA_PUBLIC,      MAIL_ABORT},
    {(char *)"alias",       1,      CA_PUBLIC,      MAIL_ALIAS},
    {(char *)"alist",       1,      CA_PUBLIC,      MAIL_ALIST},
    {(char *)"cc",          2,      CA_PUBLIC,      MAIL_CC},
    {(char *)"clear",       1,      CA_PUBLIC,      MAIL_CLEAR},
    {(char *)"debug",       1,      CA_PUBLIC,      MAIL_DEBUG},
    {(char *)"dstats",      1,      CA_PUBLIC,      MAIL_DSTATS},
    {(char *)"edit",        2,      CA_PUBLIC,      MAIL_EDIT},
    {(char *)"file",        1,      CA_PUBLIC,      MAIL_FILE},
    {(char *)"folder",      1,      CA_PUBLIC,      MAIL_FOLDER},
    {(char *)"forward",     2,      CA_PUBLIC,      MAIL_FORWARD},
    {(char *)"fstats",      1,      CA_PUBLIC,      MAIL_FSTATS},
    {(char *)"fwd",         2,      CA_PUBLIC,      MAIL_FORWARD},
    {(char *)"list",        1,      CA_PUBLIC,      MAIL_LIST},
    {(char *)"nuke",        1,      CA_PUBLIC,      MAIL_NUKE},
    {(char *)"proof",       1,      CA_PUBLIC,      MAIL_PROOF},
    {(char *)"purge",       1,      CA_PUBLIC,      MAIL_PURGE},
    {(char *)"quick",       0,      CA_PUBLIC,      MAIL_QUICK},
    {(char *)"quote",       3,      CA_PUBLIC,      MAIL_QUOTE|SW_MULTIPLE},
    {(char *)"read",        1,      CA_PUBLIC,      MAIL_READ},
    {(char *)"reply",       3,      CA_PUBLIC,      MAIL_REPLY},
    {(char *)"replyall",    6,      CA_PUBLIC,      MAIL_REPLYALL},
    {(char *)"retract",     2,      CA_PUBLIC,      MAIL_RETRACT},
    {(char *)"review",      2,      CA_PUBLIC,      MAIL_REVIEW},
    {(char *)"safe",        2,      CA_PUBLIC,      MAIL_SAFE},
    {(char *)"send",        0,      CA_PUBLIC,      MAIL_SEND},
    {(char *)"stats",       1,      CA_PUBLIC,      MAIL_STATS},
    {(char *)"tag",         1,      CA_PUBLIC,      MAIL_TAG},
    {(char *)"unclear",     1,      CA_PUBLIC,      MAIL_UNCLEAR},
    {(char *)"untag",       1,      CA_PUBLIC,      MAIL_UNTAG},
    {(char *)"urgent",      1,      CA_PUBLIC,      MAIL_URGENT},
    { NULL,                 0,              0,      0}
};

NAMETAB malias_sw[] =
{
    {(char *)"desc",        1,      CA_PUBLIC,      MALIAS_DESC},
    {(char *)"chown",       1,      CA_PUBLIC,      MALIAS_CHOWN},
    {(char *)"add",         1,      CA_PUBLIC,      MALIAS_ADD},
    {(char *)"remove",      1,      CA_PUBLIC,      MALIAS_REMOVE},
    {(char *)"delete",      1,      CA_PUBLIC,      MALIAS_DELETE},
    {(char *)"rename",      1,      CA_PUBLIC,      MALIAS_RENAME},
    {(char *)"list",        1,      CA_PUBLIC,      MALIAS_LIST},
    {(char *)"status",      1,      CA_PUBLIC,      MALIAS_STATUS},
    { NULL,                 0,              0,      0}
};

NAMETAB mark_sw[] =
{
    {(char *)"set",     1,  CA_PUBLIC,  MARK_SET},
    {(char *)"clear",   1,  CA_PUBLIC,  MARK_CLEAR},
    { NULL,         0,  0,      0}
};

NAMETAB markall_sw[] =
{
    {(char *)"set",     1,  CA_PUBLIC,  MARK_SET},
    {(char *)"clear",   1,  CA_PUBLIC,  MARK_CLEAR},
    { NULL,         0,  0,      0}
};

NAMETAB motd_sw[] =
{
    {(char *)"brief",   1,  CA_WIZARD,  MOTD_BRIEF|SW_MULTIPLE},
    {(char *)"connect", 1,  CA_WIZARD,  MOTD_ALL},
    {(char *)"down",    1,  CA_WIZARD,  MOTD_DOWN},
    {(char *)"full",    1,  CA_WIZARD,  MOTD_FULL},
    {(char *)"list",    1,  CA_PUBLIC,  MOTD_LIST},
    {(char *)"wizard",  1,  CA_WIZARD,  MOTD_WIZ},
    { NULL,         0,  0,      0}
};

NAMETAB notify_sw[] =
{
    {(char *)"all",     1,  CA_PUBLIC,  NFY_NFYALL},
    {(char *)"first",   1,  CA_PUBLIC,  NFY_NFY},
    { NULL,         0,  0,      0}
};

NAMETAB open_sw[] =
{
    {(char *)"inventory",   1,  CA_PUBLIC,  OPEN_INVENTORY},
    {(char *)"location",    1,  CA_PUBLIC,  OPEN_LOCATION},
    { NULL,         0,  0,      0}
};

NAMETAB pemit_sw[] =
{
    {(char *)"contents", 1,  CA_PUBLIC,  PEMIT_CONTENTS|SW_MULTIPLE},
    {(char *)"html",     1,  CA_PUBLIC,  PEMIT_HTML|SW_MULTIPLE},
    {(char *)"list",     1,  CA_PUBLIC,  PEMIT_LIST|SW_MULTIPLE},
    {(char *)"noeval",   1,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {(char *)"object",   1,  CA_PUBLIC,  0},
    {(char *)"silent",   1,  CA_PUBLIC,  0},
    { NULL,              0,          0,  0}
};

NAMETAB pose_sw[] =
{
    {(char *)"default", 1,  CA_PUBLIC,  0},
    {(char *)"nospace", 1,  CA_PUBLIC,  SAY_NOSPACE},
    { NULL,         0,  0,      0}
};

NAMETAB ps_sw[] =
{
    {(char *)"all",     1,  CA_PUBLIC,  PS_ALL|SW_MULTIPLE},
    {(char *)"brief",   1,  CA_PUBLIC,  PS_BRIEF},
    {(char *)"long",    1,  CA_PUBLIC,  PS_LONG},
    {(char *)"summary", 1,  CA_PUBLIC,  PS_SUMM},
    { NULL,         0,  0,      0}
};

NAMETAB quota_sw[] =
{
    {(char *)"all",     1,  CA_GOD,     QUOTA_ALL|SW_MULTIPLE},
    {(char *)"fix",     1,  CA_WIZARD,  QUOTA_FIX},
    {(char *)"remaining",   1,  CA_WIZARD,  QUOTA_REM|SW_MULTIPLE},
    {(char *)"set",     1,  CA_WIZARD,  QUOTA_SET},
    {(char *)"total",   1,  CA_WIZARD,  QUOTA_TOT|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB set_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  SET_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB stats_sw[] =
{
    {(char *)"all",     1,  CA_PUBLIC,  STAT_ALL},
    {(char *)"me",      1,  CA_PUBLIC,  STAT_ME},
    {(char *)"player",  1,  CA_PUBLIC,  STAT_PLAYER},
    { NULL,         0,  0,      0}
};

NAMETAB sweep_sw[] =
{
    {(char *)"commands",    3,  CA_PUBLIC,  SWEEP_COMMANDS|SW_MULTIPLE},
    {(char *)"connected",   3,  CA_PUBLIC,  SWEEP_CONNECT|SW_MULTIPLE},
    {(char *)"exits",   1,  CA_PUBLIC,  SWEEP_EXITS|SW_MULTIPLE},
    {(char *)"here",    1,  CA_PUBLIC,  SWEEP_HERE|SW_MULTIPLE},
    {(char *)"inventory",   1,  CA_PUBLIC,  SWEEP_ME|SW_MULTIPLE},
    {(char *)"listeners",   1,  CA_PUBLIC,  SWEEP_LISTEN|SW_MULTIPLE},
    {(char *)"players", 1,  CA_PUBLIC,  SWEEP_PLAYER|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB switch_sw[] =
{
    {(char *)"all",     1,  CA_PUBLIC,  SWITCH_ANY},
    {(char *)"default", 1,  CA_PUBLIC,  SWITCH_DEFAULT},
    {(char *)"first",   1,  CA_PUBLIC,  SWITCH_ONE},
    { NULL,         0,  0,      0}
};

NAMETAB teleport_sw[] =
{
    {(char *)"loud",    1,  CA_PUBLIC,  TELEPORT_DEFAULT},
    {(char *)"quiet",   1,  CA_PUBLIC,  TELEPORT_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB timecheck_sw[] =
{
    {(char *)"log",     1,  CA_WIZARD,  TIMECHK_LOG | SW_MULTIPLE},
    {(char *)"reset",   1,  CA_WIZARD,  TIMECHK_RESET | SW_MULTIPLE},
    {(char *)"screen",  1,  CA_WIZARD,  TIMECHK_SCREEN | SW_MULTIPLE},
    { NULL,             0,  0,          0}
};

NAMETAB toad_sw[] =
{
    {(char *)"no_chown",    1,  CA_WIZARD,  TOAD_NO_CHOWN|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};

NAMETAB trig_sw[] =
{
    {(char *)"quiet",   1,  CA_PUBLIC,  TRIG_QUIET},
    { NULL,         0,  0,      0}
};

NAMETAB wait_sw[] =
{
    {"until",           1,  CA_PUBLIC, WAIT_UNTIL},
    { NULL,             0,  0,         0}
};

NAMETAB wall_sw[] =
{
    {"emit",        1,  CA_ANNOUNCE,    SAY_WALLEMIT},
    {"no_prefix",   1,  CA_ANNOUNCE,    SAY_NOTAG|SW_MULTIPLE},
    {"pose",        1,  CA_ANNOUNCE,    SAY_WALLPOSE},
    {"wizard",      1,  CA_ANNOUNCE,    SAY_WIZSHOUT|SW_MULTIPLE},
    {"admin",       1,  CA_ADMIN,       SAY_ADMINSHOUT},
    { NULL,         0,  0,              0}
};

NAMETAB warp_sw[] =
{
    {(char *)"check",   1,  CA_WIZARD,  TWARP_CLEAN|SW_MULTIPLE},
    {(char *)"dump",    1,  CA_WIZARD,  TWARP_DUMP|SW_MULTIPLE},
    {(char *)"idle",    1,  CA_WIZARD,  TWARP_IDLE|SW_MULTIPLE},
    {(char *)"queue",   1,  CA_WIZARD,  TWARP_QUEUE|SW_MULTIPLE},
    {(char *)"events",  1,  CA_WIZARD,  TWARP_EVENTS|SW_MULTIPLE},
    { NULL,         0,  0,      0}
};


/* ---------------------------------------------------------------------------
 * Command table: Definitions for builtin commands, used to build the command
 * hash table.
 *
 * Format:  Name        Switches    Permissions Needed
 *  Key (if any)    Calling Seq         Handler
 */
CMDENT_NO_ARG command_table_no_arg[] =
{
    {(char *)"@@",            NULL,       0,  0,      CS_NO_ARGS,         do_comment},
    {(char *)"@clist",        clist_sw,   CA_NO_SLAVE,        0,              CS_NO_ARGS,                     do_chanlist},
    {(char *)"@dbck",         NULL,       CA_WIZARD,    0,      CS_NO_ARGS,         do_dbck},
    {(char *)"@dbclean",      NULL,       CA_GOD,       0,      CS_NO_ARGS,         do_dbclean},
    {(char *)"@dump",         dump_sw,    CA_WIZARD,    0,      CS_NO_ARGS,         do_dump},
    {(char *)"@mark_all",     markall_sw, CA_WIZARD,    MARK_SET,   CS_NO_ARGS,         do_markall},
    {(char *)"@readcache",    NULL,       CA_WIZARD,    0,      CS_NO_ARGS,         do_readcache},
    {(char *)"@restart",      NULL,       CA_WIZARD,    0,      CS_NO_ARGS,         do_restart},
#ifndef WIN32
    {(char *)"@startslave",   NULL,       CA_WIZARD,    0,      CS_NO_ARGS,         boot_slave},
#endif
    {(char *)"@timecheck",    timecheck_sw, CA_WIZARD,  0,        CS_NO_ARGS,       do_timecheck},
    {(char *)"comlist",       NULL,       CA_NO_SLAVE,        0,              CS_NO_ARGS,                     do_comlist},
    {(char *)"clearcom",      NULL,       CA_NO_SLAVE,        0,              CS_NO_ARGS,                     do_clearcom},
    {(char *)"inventory",     NULL,       0,    LOOK_INVENTORY, CS_NO_ARGS,         do_inventory},
    {(char *)"leave",         leave_sw,   CA_LOCATION,    0,      CS_NO_ARGS|CS_INTERP,       do_leave},
    {(char *)"score",         NULL,       0,    LOOK_SCORE, CS_NO_ARGS,         do_score},
    {(char *)"version",       NULL,       0,    0,      CS_NO_ARGS,         do_version},
    {(char *)"quit",          NULL,       CA_PUBLIC,    CMD_QUIT,   CS_NO_ARGS,         logged_out0},
    {(char *)"logout",        NULL,       CA_PUBLIC,    CMD_LOGOUT, CS_NO_ARGS,         logged_out0},
    {(char *)"report",        NULL,       0,    0,      CS_NO_ARGS,   do_report},
    {(char *)"info",          NULL,       CA_PUBLIC,    CMD_INFO,   CS_NO_ARGS,         logged_out0},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_ONE_ARG command_table_one_arg[] =
{
    {(char *)"+help",         NULL,       0,    HELP_PLUSHELP,  CS_ONE_ARG,         do_help},
    {(char *)"+shelp",        NULL,       CA_STAFF,    HELP_STAFFHELP, CS_ONE_ARG,         do_help},
    {(char *)"@boot",         boot_sw,    CA_NO_GUEST|CA_NO_SLAVE,    0,      CS_ONE_ARG|CS_INTERP,       do_boot},
    {(char *)"@ccreate",      NULL,       CA_NO_SLAVE|CA_NO_GUEST,        0,               CS_ONE_ARG,                    do_createchannel},
    {(char *)"@cdestroy",     NULL,       CA_NO_SLAVE|CA_NO_GUEST,        0,               CS_ONE_ARG,                    do_destroychannel},
    {(char *)"@cut",          NULL,       CA_WIZARD|CA_LOCATION,  0,      CS_ONE_ARG|CS_INTERP,       do_cut},
    {(char *)"@cwho",         NULL,       CA_NO_SLAVE,        0,              CS_ONE_ARG,                     do_channelwho},
    {(char *)"@destroy",      destroy_sw, CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,    DEST_ONE,   CS_ONE_ARG|CS_INTERP,       do_destroy},
    //{(char *)"@destroyall",   NULL,       CA_WIZARD|CA_GBL_BUILD,     DEST_ALL,   CS_ONE_ARG,         do_destroy},
    {(char *)"@disable",      NULL,       CA_WIZARD,    GLOB_DISABLE,   CS_ONE_ARG,         do_global},
    {(char *)"@doing",        doing_sw,   CA_PUBLIC,    0,      CS_ONE_ARG,         do_doing},
    {(char *)"@emit",         emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,     SAY_EMIT,   CS_ONE_ARG|CS_INTERP,       do_say},
    {(char *)"@enable",       NULL,       CA_WIZARD,     GLOB_ENABLE,    CS_ONE_ARG,         do_global},
    {(char *)"@entrances",    NULL,       CA_NO_GUEST,     0,      CS_ONE_ARG|CS_INTERP,   do_entrances},
    {(char *)"@find",         NULL,       0,    0,      CS_ONE_ARG|CS_INTERP,       do_find},
    {(char *)"@halt",         halt_sw,    CA_NO_SLAVE,    0,      CS_ONE_ARG|CS_INTERP,       do_halt},
    {(char *)"@kick",         NULL,       CA_WIZARD,    QUEUE_KICK, CS_ONE_ARG|CS_INTERP,       do_queue},
    {(char *)"@last",         NULL,       CA_NO_GUEST,    0,      CS_ONE_ARG|CS_INTERP,       do_last},
    {(char *)"@list",         NULL,       0,    0,      CS_ONE_ARG|CS_INTERP,       do_list},
    {(char *)"@listcommands", NULL,       CA_GOD,    0,      CS_ONE_ARG,         do_listcommands},
    {(char *)"@list_file",    NULL,       CA_WIZARD,    0,      CS_ONE_ARG|CS_INTERP,       do_list_file},
    {(char *)"@listmotd",     listmotd_sw,0,    MOTD_LIST,  CS_ONE_ARG,         do_motd},
    {(char *)"@mark",         mark_sw,    CA_WIZARD,    SRCH_MARK,  CS_ONE_ARG|CS_NOINTERP,     do_search},
    {(char *)"@motd",         motd_sw,    CA_WIZARD,    0,      CS_ONE_ARG,         do_motd},
    {(char *)"@nemit",        emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE, SAY_EMIT, CS_ONE_ARG|CS_UNPARSE|CS_NOSQUISH, do_say},
    {(char *)"@poor",         NULL,       CA_GOD,    0,      CS_ONE_ARG|CS_INTERP,       do_poor},
    {(char *)"@ps",           ps_sw,      0,    0,      CS_ONE_ARG|CS_INTERP,       do_ps},
    {(char *)"@quitprogram",  NULL,       CA_PUBLIC,    0,      CS_ONE_ARG|CS_INTERP,       do_quitprog},
    {(char *)"@search",       NULL,       0,    SRCH_SEARCH,    CS_ONE_ARG|CS_NOINTERP,     do_search},
    {(char *)"@shutdown",     NULL,       CA_WIZARD,    0,      CS_ONE_ARG,         do_shutdown},
    {(char *)"@stats",        stats_sw,   0,    0,      CS_ONE_ARG|CS_INTERP,       do_stats},
    {(char *)"@sweep",        sweep_sw,   0,    0,      CS_ONE_ARG,         do_sweep},
    {(char *)"@timewarp",     warp_sw,    CA_WIZARD,    0,      CS_ONE_ARG|CS_INTERP,       do_timewarp},
    {(char *)"@unlink",       NULL,       CA_NO_SLAVE|CA_GBL_BUILD,    0,      CS_ONE_ARG|CS_INTERP,       do_unlink},
    {(char *)"@unlock",       lock_sw,    CA_NO_SLAVE,    0,      CS_ONE_ARG|CS_INTERP,       do_unlock},
    {(char *)"@wall",         wall_sw,    CA_ANNOUNCE,    SAY_SHOUT,  CS_ONE_ARG|CS_INTERP,       do_say},
    {(char *)"@wipe",         wall_sw,    CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,    0,      CS_ONE_ARG|CS_INTERP,       do_wipe},
    {(char *)"allcom",        NULL,       CA_NO_SLAVE,        0,              CS_ONE_ARG,                     do_allcom},
    {(char *)"delcom",        NULL,       CA_NO_SLAVE,        0,              CS_ONE_ARG,                     do_delcom},
    {(char *)"drop",          drop_sw,    CA_NO_SLAVE|CA_CONTENTS|CA_LOCATION|CA_NO_GUEST,    0,      CS_ONE_ARG|CS_INTERP,       do_drop},
    {(char *)"enter",         enter_sw,   CA_LOCATION,    0,      CS_ONE_ARG|CS_INTERP,       do_enter},
    {(char *)"examine",       examine_sw, 0,    0,      CS_ONE_ARG|CS_INTERP,       do_examine},
    {(char *)"get",           get_sw,     CA_LOCATION|CA_NO_GUEST,    0,      CS_ONE_ARG|CS_INTERP,       do_get},
    {(char *)"goto",          goto_sw,    CA_LOCATION,    0,      CS_ONE_ARG|CS_INTERP,       do_move},
    {(char *)"help",          NULL,       0,    HELP_HELP,  CS_ONE_ARG,         do_help},
    {(char *)"look",          look_sw,    CA_LOCATION,    LOOK_LOOK,  CS_ONE_ARG|CS_INTERP,       do_look},
    {(char *)"news",          NULL,       0,    HELP_NEWS,  CS_ONE_ARG,         do_help},
    {(char *)"outputprefix",  NULL,       CA_PUBLIC,    CMD_PREFIX, CS_ONE_ARG,         logged_out1},
    {(char *)"outputsuffix",  NULL,       CA_PUBLIC,    CMD_SUFFIX, CS_ONE_ARG,         logged_out1},
    {(char *)"pose",          pose_sw,    CA_LOCATION|CA_NO_SLAVE,    SAY_POSE,   CS_ONE_ARG|CS_INTERP,       do_say},
    {(char *)"say",           NULL,       CA_LOCATION|CA_NO_SLAVE,    SAY_SAY,    CS_ONE_ARG|CS_INTERP,       do_say},
    {(char *)"session",       NULL,       CA_PUBLIC,    CMD_SESSION,    CS_ONE_ARG,         logged_out1},
    {(char *)"think",         NULL,       CA_NO_SLAVE,        0,              CS_ONE_ARG,                     do_think},
    {(char *)"use",           NULL,       CA_NO_SLAVE|CA_GBL_INTERP,    0,      CS_ONE_ARG|CS_INTERP,       do_use},
    {(char *)"wizhelp",       NULL,       CA_WIZARD,    HELP_WIZHELP,   CS_ONE_ARG,         do_help},
    {(char *)"wiznews",       NULL,       CA_WIZARD,        HELP_WIZNEWS,   CS_ONE_ARG,                     do_help},
    {(char *)"doing",         NULL,       CA_PUBLIC,    CMD_DOING,  CS_ONE_ARG,         logged_out1},
    {(char *)"who",           NULL,       CA_PUBLIC,    CMD_WHO,    CS_ONE_ARG,         logged_out1},
    {(char *)"puebloclient",  NULL,       CA_PUBLIC,      CMD_PUEBLOCLIENT,CS_ONE_ARG,                    logged_out1},
    {(char *)"\\",            NULL,       CA_NO_GUEST|CA_LOCATION|CF_DARK|CA_NO_SLAVE,    SAY_PREFIX, CS_ONE_ARG|CS_INTERP,       do_say},
    {(char *)":",             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,    SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, do_say},
    {(char *)";",             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,    SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, do_say},
    {(char *)"\"",            NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,     SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, do_say},
    {(char *)"-",             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,     0,      CS_ONE_ARG|CS_LEADIN,       do_postpend},
    {(char *)"~",             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,     0,      CS_ONE_ARG|CS_LEADIN,       do_prepend},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_ONE_ARG_CMDARG command_table_one_arg_cmdarg[] =
{
    {(char *)"@apply_marked", NULL,       CA_WIZARD|CA_GBL_INTERP,    0,      CS_ONE_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND,   do_apply_marked},
    {(char *)"#",             NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CF_DARK,    0,      CS_ONE_ARG|CS_INTERP|CS_CMDARG, do_force_prefixed},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_TWO_ARG command_table_two_arg[] =
{
    {(char *)"@addcommand",   NULL,       CA_GOD,                                0,         CS_TWO_ARG,           do_addcommand},
    {(char *)"@admin",        NULL,       CA_WIZARD,                             0,         CS_TWO_ARG|CS_INTERP, do_admin},
    {(char *)"@alias",        NULL,       CA_NO_GUEST|CA_NO_SLAVE,               0,         CS_TWO_ARG,           do_alias},
    {(char *)"@attribute",    attrib_sw,  CA_GOD,                                0,         CS_TWO_ARG|CS_INTERP, do_attribute},
    {(char *)"@cboot",        NULL,       CA_NO_SLAVE|CA_NO_GUEST,               0,         CS_TWO_ARG,           do_chboot},
    {(char *)"@ccharge",      NULL,       CA_NO_SLAVE|CA_NO_GUEST,               1,         CS_TWO_ARG,           do_editchannel},
    {(char *)"@cchown",       NULL,       CA_NO_SLAVE|CA_NO_GUEST,               0,         CS_TWO_ARG,           do_editchannel},
    {(char *)"@cemit",        cemit_sw,   CA_NO_SLAVE|CA_NO_GUEST,               0,         CS_TWO_ARG,           do_cemit},
    {(char *)"@chown",        NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,  CHOWN_ONE, CS_TWO_ARG|CS_INTERP, do_chown},
    {(char *)"@chownall",     NULL,       CA_WIZARD|CA_GBL_BUILD,                CHOWN_ALL, CS_TWO_ARG|CS_INTERP, do_chownall},
    {(char *)"@chzone",       NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,  0,         CS_TWO_ARG|CS_INTERP, do_chzone},
    {(char *)"@clone",        clone_sw,   CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST,   0,      CS_TWO_ARG|CS_INTERP,       do_clone},
    {(char *)"@coflags",      NULL,       CA_NO_SLAVE,        4,              CS_TWO_ARG,                     do_editchannel},
    {(char *)"@cpflags",      NULL,       CA_NO_SLAVE,        3,              CS_TWO_ARG,                     do_editchannel},
    {(char *)"@create",       NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST,   0,      CS_TWO_ARG|CS_INTERP,       do_create},
    {(char *)"@cset",         cset_sw,    CA_NO_SLAVE,        0,              CS_TWO_ARG|CS_INTERP,           do_chopen},
    {(char *)"@decompile",    decomp_sw,  0,    0,      CS_TWO_ARG|CS_INTERP,       do_decomp},
    {(char *)"@delcommand",   NULL,       CA_GOD,    0,      CS_TWO_ARG,         do_delcommand},
    {(char *)"@drain",        NULL,       CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,    NFY_DRAIN,  CS_TWO_ARG,         do_notify},
    {(char *)"@femit",        femit_sw,   CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,    PEMIT_FEMIT,    CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"@fixdb",        fixdb_sw,   CA_GOD,    0,      CS_TWO_ARG|CS_INTERP,       do_fixdb},
    {(char *)"@fpose",        fpose_sw,   CA_LOCATION|CA_NO_SLAVE,    PEMIT_FPOSE,    CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"@fsay",         NULL,       CA_LOCATION|CA_NO_SLAVE,    PEMIT_FSAY, CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"@function",     function_sw,CA_GOD,    0,      CS_TWO_ARG|CS_INTERP,       do_function},
    {(char *)"@link",         NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,    0,      CS_TWO_ARG|CS_INTERP,       do_link},
    {(char *)"@lock",         lock_sw,    CA_NO_SLAVE,    0,      CS_TWO_ARG|CS_INTERP,       do_lock},
    {(char *)"@log",          NULL,       CA_WIZARD,      0,      CS_TWO_ARG, do_log},
    {(char *)"@mail",         mail_sw,    CA_NO_SLAVE|CA_NO_GUEST,        0,              CS_TWO_ARG|CS_INTERP,          do_mail},
    {(char *)"@malias",       malias_sw,  CA_NO_SLAVE|CA_NO_GUEST,        0,              CS_TWO_ARG|CS_INTERP,          do_malias},
    {(char *)"@name",         NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,    0,      CS_TWO_ARG|CS_INTERP,       do_name},
    {(char *)"@newpassword",  NULL,       CA_WIZARD,    PASS_ANY,   CS_TWO_ARG,         do_newpassword},
    {(char *)"@notify",       notify_sw,  CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,    0,      CS_TWO_ARG,         do_notify},
    {(char *)"@oemit",        NULL,       CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,    PEMIT_OEMIT,    CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"@parent",       NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,    0,      CS_TWO_ARG,         do_parent},
    {(char *)"@password",     NULL,       CA_NO_GUEST,    PASS_MINE,  CS_TWO_ARG,         do_password},
    {(char *)"@pcreate",      NULL,       CA_WIZARD|CA_GBL_BUILD,    PCRE_PLAYER,    CS_TWO_ARG,         do_pcreate},
    {(char *)"@pemit",        pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,    PEMIT_PEMIT,    CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"@npemit",       pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,    PEMIT_PEMIT,    CS_TWO_ARG|CS_UNPARSE|CS_NOSQUISH,      do_pemit},
    {(char *)"@power",        NULL,       0,    0,      CS_TWO_ARG,         do_power},
    {(char *)"@program",      NULL,       CA_PUBLIC,    0,      CS_TWO_ARG|CS_INTERP,           do_prog},
    {(char *)"@quota",        quota_sw,   0,    0,      CS_TWO_ARG|CS_INTERP,       do_quota},
    {(char *)"@robot",        NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST|CA_PLAYER,    PCRE_ROBOT, CS_TWO_ARG,         do_pcreate},
    {(char *)"@set",          set_sw,     CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,    0,      CS_TWO_ARG, do_set},
    {(char *)"@teleport",     teleport_sw,CA_NO_GUEST,    TELEPORT_DEFAULT, CS_TWO_ARG|CS_INTERP,     do_teleport},
    {(char *)"@toad",         toad_sw,    CA_WIZARD,    0,      CS_TWO_ARG|CS_INTERP,       do_toad},
    {(char *)"addcom",        NULL,       CA_NO_SLAVE,        0,              CS_TWO_ARG,                     do_addcom},
    {(char *)"comtitle",      comtitle_sw, CA_NO_SLAVE,       0,              CS_TWO_ARG,                    do_comtitle},
    {(char *)"give",          give_sw,    CA_LOCATION|CA_NO_GUEST,    0,      CS_TWO_ARG|CS_INTERP,       do_give},
    {(char *)"kill",          NULL,       CA_NO_GUEST|CA_NO_SLAVE,    KILL_KILL,  CS_TWO_ARG|CS_INTERP,       do_kill},
    {(char *)"page",          NULL,       CA_NO_SLAVE,    0,      CS_TWO_ARG|CS_INTERP,       do_page},
    {(char *)"slay",          NULL,       CA_WIZARD,    KILL_SLAY,  CS_TWO_ARG|CS_INTERP,       do_kill},
    {(char *)"whisper",       NULL,       CA_LOCATION|CA_NO_SLAVE,    PEMIT_WHISPER,  CS_TWO_ARG|CS_INTERP,       do_pemit},
    {(char *)"&",             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,     0,      CS_TWO_ARG|CS_LEADIN,       do_setvattr},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_TWO_ARG_ARGV command_table_two_arg_argv[] =
{
    {(char *)"@cpattr",       NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,         0,             CS_TWO_ARG|CS_ARGV,             do_cpattr},
    {(char *)"@dig",          dig_sw,     CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,     0,      CS_TWO_ARG|CS_ARGV|CS_INTERP,   do_dig},
    {(char *)"@edit",         NULL,       CA_NO_SLAVE|CA_NO_GUEST,    0,      CS_TWO_ARG|CS_ARGV|CS_STRIP_AROUND, do_edit},
    {(char *)"@mvattr",       NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,    0,      CS_TWO_ARG|CS_ARGV,     do_mvattr},
    {(char *)"@open",         open_sw,    CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,    0,      CS_TWO_ARG|CS_ARGV|CS_INTERP,   do_open},
    {(char *)"@trigger",      trig_sw,    CA_GBL_INTERP,    0,      CS_TWO_ARG|CS_ARGV,     do_trigger},
    {(char *)"@verb",         NULL,       CA_GBL_INTERP|CA_NO_SLAVE,    0,      CS_TWO_ARG|CS_ARGV|CS_INTERP|CS_STRIP_AROUND, do_verb},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_TWO_ARG_CMDARG command_table_two_arg_cmdarg[] =
{
    {(char *)"@dolist",       dolist_sw,  CA_GBL_INTERP,    0,      CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, do_dolist},
    {(char *)"@force",        NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CA_NO_GUEST,    FRC_COMMAND,    CS_TWO_ARG|CS_INTERP|CS_CMDARG, do_force},
    {(char *)"@wait",         wait_sw,    CA_GBL_INTERP,    0,      CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, do_wait},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT_TWO_ARG_ARGV_CMDARG command_table_two_arg_argv_cmdarg[] =
{
    {(char *)"@switch",       switch_sw,  CA_GBL_INTERP,    0,      CS_TWO_ARG|CS_ARGV|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, do_switch},
    {(char *)NULL,            NULL,       0,     0,      0,              NULL}
};

CMDENT *prefix_cmds[256];

CMDENT *goto_cmdp;

void NDECL(init_cmdtab)
{
    CMDENT_NO_ARG               *cp0a;
    CMDENT_ONE_ARG              *cp1a;
    CMDENT_ONE_ARG_CMDARG       *cp1ac;
    CMDENT_TWO_ARG              *cp2a;
    CMDENT_TWO_ARG_ARGV         *cp2aa;
    CMDENT_TWO_ARG_CMDARG       *cp2ac;
    CMDENT_TWO_ARG_ARGV_CMDARG  *cp2aac;

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
        BOOL bValid;
        char *cbuff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        cp2a = (CMDENT_TWO_ARG *)MEMALLOC(sizeof(CMDENT_TWO_ARG));
        ISOUTOFMEMORY(cp2a);
        cp2a->cmdname = (char *)StringClone(cbuff);
        cp2a->perms = CA_NO_GUEST | CA_NO_SLAVE;
        cp2a->switches = NULL;
        if (ap->flags & (AF_WIZARD | AF_MDARK))
        {
            cp2a->perms |= CA_WIZARD;
        }
        cp2a->extra = ap->number;
        cp2a->callseq = CS_TWO_ARG;
        cp2a->handler = do_setattr;
        hashaddLEN(cp2a->cmdname, nBuffer, (int *)cp2a, &mudstate.command_htab);
    }

    /*
     * Load the builtin commands 
     */
    for (cp0a = command_table_no_arg; cp0a->cmdname; cp0a++)
        hashaddLEN(cp0a->cmdname, strlen(cp0a->cmdname), (int *)cp0a, &mudstate.command_htab);

    for (cp1a = command_table_one_arg; cp1a->cmdname; cp1a++)
        hashaddLEN(cp1a->cmdname, strlen(cp1a->cmdname), (int *)cp1a, &mudstate.command_htab);

    for (cp1ac = command_table_one_arg_cmdarg; cp1ac->cmdname; cp1ac++)
        hashaddLEN(cp1ac->cmdname, strlen(cp1ac->cmdname), (int *)cp1ac, &mudstate.command_htab);

    for (cp2a = command_table_two_arg; cp2a->cmdname; cp2a++)
        hashaddLEN(cp2a->cmdname, strlen(cp2a->cmdname), (int *)cp2a, &mudstate.command_htab);

    for (cp2aa = command_table_two_arg_argv; cp2aa->cmdname; cp2aa++)
        hashaddLEN(cp2aa->cmdname, strlen(cp2aa->cmdname), (int *)cp2aa, &mudstate.command_htab);

    for (cp2ac = command_table_two_arg_cmdarg; cp2ac->cmdname; cp2ac++)
        hashaddLEN(cp2ac->cmdname, strlen(cp2ac->cmdname), (int *)cp2ac, &mudstate.command_htab);

    for (cp2aac = command_table_two_arg_argv_cmdarg; cp2aac->cmdname; cp2aac++)
        hashaddLEN(cp2aac->cmdname, strlen(cp2aac->cmdname), (int *)cp2aac, &mudstate.command_htab);

    set_prefix_cmds();

    goto_cmdp = (CMDENT *) hashfindLEN((char *)"goto", strlen("goto"), &mudstate.command_htab);
}

void set_prefix_cmds()
{
    int i;

    // Load the command prefix table. Note - these commands can never
    // be typed in by a user because commands are lowercased before
    // the hash table is checked. The names are abbreviated to
    // minimise name checking time.
    //
    for (i = 0; i < 256; i++)
    {
        prefix_cmds[i] = NULL;
    }
    prefix_cmds['"']  = (CMDENT *) hashfindLEN((char *)"\"", 1, &mudstate.command_htab);
    prefix_cmds[':']  = (CMDENT *) hashfindLEN((char *)":",  1, &mudstate.command_htab);
    prefix_cmds[';']  = (CMDENT *) hashfindLEN((char *)";",  1, &mudstate.command_htab);
    prefix_cmds['\\'] = (CMDENT *) hashfindLEN((char *)"\\", 1, &mudstate.command_htab);
    prefix_cmds['#']  = (CMDENT *) hashfindLEN((char *)"#",  1, &mudstate.command_htab);
    prefix_cmds['&']  = (CMDENT *) hashfindLEN((char *)"&",  1, &mudstate.command_htab);
    prefix_cmds['-']  = (CMDENT *) hashfindLEN((char *)"-",  1, &mudstate.command_htab);
    prefix_cmds['~']  = (CMDENT *) hashfindLEN((char *)"~",  1, &mudstate.command_htab);
}

/*
 * ---------------------------------------------------------------------------
 * * check_access: Check if player has access to function.  
 */

int check_access(dbref player, int mask)
{
    int succ, fail;

    if (mask & (CA_DISABLED|CA_STATIC))
    {
        return 0;
    }
    if (God(player) || mudstate.bReadingConfiguration)
    {
        return 1;
    }

    succ = fail = 0;
    if (mask & CA_GOD)
        fail++;
    if (mask & CA_WIZARD)
    {
        if (Wizard(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_ADMIN))
    {
        if (WizRoy(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_BUILDER))
    {
        if (Builder(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_STAFF))
    {
        if (Staff(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_HEAD))
    {
        if (Head(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_ANNOUNCE))
    {
        if (Announce(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_IMMORTAL))
    {
        if (Immortal(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_UNINS))
    {
        if (Uninspected(player))
            succ++;
        else
            fail++;
    }
    if ((succ == 0) && (mask & CA_ROBOT))
    {
        if (Robot(player))
            succ++;
        else
            fail++;
    }
    if (succ > 0)
        fail = 0;
    if (fail > 0)
        return 0;

    // Check for forbidden flags.
    //
    if (  !Wizard(player)
       && (  ((mask & CA_NO_HAVEN)   && Player_haven(player))
          || ((mask & CA_NO_ROBOT)   && Robot(player))
          || ((mask & CA_NO_SLAVE)   && Slave(player))
          || ((mask & CA_NO_SUSPECT) && Suspect(player))
          || ((mask & CA_NO_GUEST)   && Guest(player))
          || ((mask & CA_NO_UNINS)   && Uninspected(player))))
    {
        return 0;
    }
    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * process_cmdent: Perform indicated command with passed args.
 */

void process_cmdent(CMDENT *cmdp, char *switchp, dbref player, dbref cause, int interactive, char *arg,
            char *unp_command, char *cargs[], int ncargs)
{
    char *buf1, *buf2, tchar, *bp, *str, *buff, *s, *j, *new0;
    char *args[MAX_ARG];
    int nargs, i, interp, key, xkey, aflags;
    int hasswitch = 0;
    dbref aowner;
    char *aargs[10];
    ADDENT *add;
    
    // Perform object type checks.
    //
    if (Invalid_Objtype(player))
    {
        notify(player, "Command incompatible with invoker type.");
        return;
    }

    // Check if we have permission to execute the command.
    //
    if (!check_access(player, cmdp->perms))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }

    // Check global flags
    //
    if (  !Builder(player)
       && Protect(CA_GBL_BUILD)
       && !(mudconf.control_flags & CF_BUILD))
    {
        notify(player, "Sorry, building is not allowed now.");
        return;
    }
    if (Protect(CA_GBL_INTERP) && !(mudconf.control_flags & CF_INTERP))
    {
        notify(player, "Sorry, queueing and triggering are not allowed now.");
        return;
    }
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
            buf1 = (char *)strchr(switchp, '/');
            if (buf1)
                *buf1++ = '\0';
            xkey = search_nametab(player, cmdp->switches, switchp);
            if (xkey == -1)
            {
                notify(player,
                       tprintf("Unrecognized switch '%s' for command '%s'.",
                           switchp, cmdp->cmdname));
                return;
            }
            else if (xkey == -2)
            {
                notify(player, NOPERM_MESSAGE);
                return;
            }
            else if (!(xkey & SW_MULTIPLE))
            {
                if (i == 1)
                {
                    notify(player, "Illegal combination of switches.");
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
            hasswitch = 1;
        } while (buf1);
    }
    else if (switchp && !(cmdp->callseq & CS_ADDED))
    {
        notify(player, tprintf("Command %s does not take switches.",
            cmdp->cmdname));
        return;
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
            || !(interactive
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

    switch (cmdp->callseq & CS_NARG_MASK)
    {
    case CS_NO_ARGS: // <cmd>   (no args)
        (*(((CMDENT_NO_ARG *)cmdp)->handler)) (player, cause, key);
        break;

    case CS_ONE_ARG:    /*
                 * <cmd> <arg> 
                 */

        /*
         * If an unparsed command, just give it to the handler 
         */

#if 0 // This never happens.
        if (cmdp->callseq & CS_UNPARSE)
        {
            (*(((CMDENT_ONE_ARG *)cmdp)->handler)) (player, unp_command);
            break;
        }
#endif
        // Interpret if necessary, but not twice for CS_ADDED.
        //
        if ((interp & EV_EVAL) && !(cmdp->callseq & CS_ADDED))
        {
            buf1 = bp = alloc_lbuf("process_cmdent");
            str = arg;
            TinyExec(buf1, &bp, 0, player, cause, interp | EV_FCHECK | EV_TOP, &str, cargs, ncargs);
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
            (*(((CMDENT_ONE_ARG_CMDARG *)cmdp)->handler)) (player, cause, key, buf1, cargs, ncargs);
        }
        else
        {
            if (cmdp->callseq & CS_ADDED)
            {
                for (add = (ADDENT *)cmdp->handler; add != NULL; add = add->next)
                {
                    buff = atr_get(add->thing, add->atr, &aowner, &aflags);

                    // Skip the '$' character, and the next character.
                    //
                    for (s = buff + 2; *s && (*s != ':'); s++)
                    {
                        // nothing
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
                
                    if (wild(buff + 1, new0, aargs, 10))
                    {
                        CLinearTimeAbsolute lta;
                        wait_que(add->thing, player, FALSE, lta, NOTHING, 0, s, aargs, 10, mudstate.global_regs);
                        for (i = 0; i < 10; i++)
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
                (*(((CMDENT_ONE_ARG *)cmdp)->handler)) (player, cause, key, buf1);
            }
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

        // Handle when no '=' was specified.
        //
        if (!arg || (arg && !*arg))
        {
            arg = &tchar;
            *arg = '\0';
        }
        buf1 = bp = alloc_lbuf("process_cmdent.2");
        str = buf2;
        TinyExec(buf1, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL | EV_TOP, &str, cargs, ncargs);
        *bp = '\0';

        if (cmdp->callseq & CS_ARGV)
        {
            // Arg2 is ARGV style.  Go get the args.
            //
            parse_arglist(player, cause, arg, '\0', interp | EV_STRIP_LS | EV_STRIP_TS, args, MAX_ARG, cargs, ncargs, &nargs);

            // Call the correct command handler.
            //
            if (cmdp->callseq & CS_CMDARG)
            {
                (*(((CMDENT_TWO_ARG_ARGV_CMDARG *)cmdp)->handler)) (player, cause, key, buf1, args, nargs, cargs, ncargs);
            }
            else
            {
                (*(((CMDENT_TWO_ARG_ARGV *)cmdp)->handler)) (player, cause, key, buf1, args, nargs);
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
                TinyExec(buf2, &bp, 0, player, cause, interp | EV_FCHECK | EV_TOP, &str, cargs, ncargs);
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
                (*(((CMDENT_TWO_ARG_CMDARG *)cmdp)->handler)) (player, cause, key, buf1, buf2, cargs, ncargs);
            }
            else
            {
                (*(((CMDENT_TWO_ARG *)cmdp)->handler)) (player, cause, key, buf1, buf2);
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
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * process_command: Execute a command.
 */
char *process_command
(
    dbref player,
    dbref cause,
    int   interactive,
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
    char *p, *q, *arg, *pSlash, *cmdsave, *bp, *str;
    int succ, aflags, i;
    dbref exit, aowner;
    CMDENT *cmdp;

    // Robustify player.
    //
    cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = (char *)"< process_command >";

    Tiny_Assert(pOriginalCommand);

    if (!Good_obj(player))
    {
        // We are using SpaceCompressCommand temporarily.
        //
        STARTLOG(LOG_BUGS, "CMD", "PLYR");
        sprintf(SpaceCompressCommand, "Bad player in process_command: %d", player);
        log_text(SpaceCompressCommand);
        ENDLOG;
        mudstate.debug_cmd = cmdsave;
        return pOriginalCommand;
    }

    // Make sure player isn't going or halted.
    //
    if (  Going(player)
       || (Halted(player) && !((Typeof(player) == TYPE_PLAYER) && interactive)))
    {
        notify(Owner(player), tprintf("Attempt to execute command by halted object #%d", player));
        mudstate.debug_cmd = cmdsave;
        return pOriginalCommand;
    }
    if (Suspect(player))
    {
        STARTLOG(LOG_SUSPECTCMDS, "CMD", "SUSP");
        log_name_and_loc(player);
        log_text(" entered: ");
        log_text(pOriginalCommand);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALLCOMMANDS, "CMD", "ALL");
        log_name_and_loc(player);
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

    if (Verbose(player))
    {
        notify(Owner(player), tprintf("%s] %s", Name(player), pOriginalCommand));
    }

    // Eat leading whitespace, and space-compress if configured.
    //
    while (Tiny_IsSpace[(unsigned char)*pOriginalCommand])
    {
        pOriginalCommand++;
    }
    strcpy(preserve_cmd, pOriginalCommand);
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
            while (*p && !Tiny_IsSpace[(unsigned char)*p])
            {
                *q++ = *p++;
            }
            while (Tiny_IsSpace[(unsigned char)*p])
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
    if (i && (prefix_cmds[i] != NULL))
    {
        process_cmdent(prefix_cmds[i], NULL, player, cause, interactive, pCommand, pCommand, args, nargs);
        mudstate.debug_cmd = cmdsave;
        return preserve_cmd;
    }

    if (  mudconf.have_comsys
       && !(Slave(player))
       && !do_comsystem(player, pCommand))
    {
        return preserve_cmd;
    }

    // Check for the HOME command.
    //
    if (string_compare(pCommand, "home") == 0)
    {
        if (((Fixed(player)) || (Fixed(Owner(player)))) &&
            !(WizRoy(player)))
        {
            notify(player, mudconf.fixed_home_msg);
            return preserve_cmd;
        }
        do_move(player, cause, 0, "home");
        mudstate.debug_cmd = cmdsave;
        return preserve_cmd;
    }

    // Only check for exits if we may use the goto command.
    //
    if (check_access(player, goto_cmdp->perms))
    {
        // Check for an exit name.
        //
        init_match_check_keys(player, pCommand, TYPE_EXIT);
        match_exit_with_parents();
        exit = last_match_result();
        if (exit != NOTHING)
        {
            move_exit(player, exit, 0, "You can't go that way.", 0);
            mudstate.debug_cmd = cmdsave;
            return preserve_cmd;
        }

        // Check for an exit in the master room.
        //
        init_match_check_keys(player, pCommand, TYPE_EXIT);
        match_master_exit();
        exit = last_match_result();
        if (exit != NOTHING)
        {
            move_exit(player, exit, 1, NULL, 0);
            mudstate.debug_cmd = cmdsave;
            return preserve_cmd;
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
    for (p = pCommand, q = LowerCaseCommand;
         *p && !Tiny_IsSpace[(unsigned char)*p];
         p++, q++)
    {
        *q = Tiny_ToLower[(unsigned char)*p];
    }
    *q = '\0';
    int nLowerCaseCommand = q - LowerCaseCommand;

    // Skip spaces before arg
    //
    while (Tiny_IsSpace[(unsigned char)*p])
    {
        p++;
    }

    // Remember where arg starts
    //
    arg = p;

    // Strip off any command switches and save them.
    //
    pSlash = (char *)strchr(LowerCaseCommand, '/');
    if (pSlash)
    {
        nLowerCaseCommand = pSlash - LowerCaseCommand;
        *pSlash++ = '\0';
    }

    // Check for a builtin command (or an alias of a builtin command)
    //
    cmdp = (CMDENT *)hashfindLEN(LowerCaseCommand, nLowerCaseCommand, &mudstate.command_htab);
    if (cmdp != NULL)
    {
        if (mudconf.space_compress && (cmdp->callseq & CS_NOSQUISH))
        {
            // We handle this specially -- there is no space compression
            // involved, so we must go back to the original command.
            // We skip over the command and a single space to position
            // arg at the arguments.
            //
            arg = pCommand = pOriginalCommand;
            while (*arg && !Tiny_IsSpace[(unsigned char)*arg])
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
        process_cmdent(cmdp, pSlash, player, cause, interactive, arg, pCommand, args, nargs);
        mudstate.debug_cmd = cmdsave;
        return preserve_cmd;
    }

    // Check for enter and leave aliases, user-defined commands on the
    // player, other objects where the player is, on objects in the
    // player's inventory, and on the room that holds the player. We
    // evaluate the command line here to allow chains of $-commands
    // to work.
    //
    bp = LowerCaseCommand;
    str = pCommand;
    TinyExec(LowerCaseCommand, &bp, 0, player, cause, EV_EVAL | EV_FCHECK | EV_STRIP_CURLY | EV_TOP, &str, args, nargs);
    *bp = '\0';
    succ = 0;

    // Idea for enter/leave aliases from R'nice@TinyTIM
    //
    if (Has_location(player) && Good_obj(Location(player)))
    {
        // Check for a leave alias.
        //
        p = atr_pget(Location(player), A_LALIAS, &aowner, &aflags);
        if (p && *p)
        {
            if (matches_exit_from_list(LowerCaseCommand, p))
            {
                free_lbuf(p);
                do_leave(player, player, 0);
                return preserve_cmd;
            }
        }
        free_lbuf(p);

        // Check for enter aliases.
        //
        DOLIST(exit, Contents(Location(player)))
        {
            p = atr_pget(exit, A_EALIAS, &aowner, &aflags);
            if (p && *p)
            {
                if (matches_exit_from_list(LowerCaseCommand, p))
                {
                    free_lbuf(p);
                    do_enter_internal(player, exit, 0);
                    return preserve_cmd;
                }
            }
            free_lbuf(p);
        }
    }

    // Check for $-command matches on me.
    //
    if (mudconf.match_mine && (!(No_Command(player))))
    {
        if (((Typeof(player) != TYPE_PLAYER) || mudconf.match_mine_pl)
            && (atr_match(player, player, AMATCH_CMD, LowerCaseCommand, 1) > 0))
        {
            succ++;
        }
    }

    // Check for $-command matches on nearby things and on my room.
    //
    if (Has_location(player))
    {
        succ += list_check(Contents(Location(player)), player, AMATCH_CMD, LowerCaseCommand, 1);

        if (!(No_Command(Location(player))))
        {
            if (atr_match(Location(player), player, AMATCH_CMD, LowerCaseCommand, 1) > 0)
            {
                succ++;
            }
        }
    }

    // Check for $-command matches in my inventory.
    //
    if (Has_contents(player))
    {
        succ += list_check(Contents(player), player, AMATCH_CMD, LowerCaseCommand, 1);
    }

    if (  !succ
       && mudconf.have_zones)
    {
        // now do check on zones.
        //
        dbref zone = Zone(player);
        dbref loc = Location(player);
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
                    init_match_check_keys(player, pCommand, TYPE_EXIT);
                    match_zone_exit();
                    exit = last_match_result();
                    if (exit != NOTHING)
                    {
                        move_exit(player, exit, 1, NULL, 0);
                        mudstate.debug_cmd = cmdsave;
                        return preserve_cmd;
                    }
                    succ += list_check(Contents(zone_loc), player,
                               AMATCH_CMD, LowerCaseCommand, 1);

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
                   succ += atr_match(zone_loc, player, AMATCH_CMD,
                       LowerCaseCommand, 1);
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
            succ += atr_match(zone, player, AMATCH_CMD, LowerCaseCommand, 1);
        }
    }

    // If we didn't find anything, try in the master room.
    //
    if (!succ)
    {
        if (  Good_obj(mudconf.master_room)
           && Has_contents(mudconf.master_room))
        {
            succ += list_check(Contents(mudconf.master_room),
                       player, AMATCH_CMD, LowerCaseCommand, 0);
            if (!(No_Command(mudconf.master_room)))
            {
                if (atr_match(mudconf.master_room, player, AMATCH_CMD, LowerCaseCommand, 0) > 0)
                {
                    succ++;
                }
            }
        }
    }

    // If we still didn't find anything, tell how to get help.
    //
    if (!succ)
    {
        // We use LowerCaseCommand for another purpose.
        //
        notify(player, "Huh?  (Type \"help\" for help.)");
        STARTLOG(LOG_BADCOMMANDS, "CMD", "BAD");
        log_name_and_loc(player);
        log_text(" entered: ");
        log_text(pCommand);
        ENDLOG;
    }
    mudstate.debug_cmd = cmdsave;
    return preserve_cmd;
}

/*
 * ---------------------------------------------------------------------------
 * * list_cmdtable: List internal commands.
 */

static void list_cmdtable(dbref player)
{
    char *buf, *bp, *cp;

    buf = alloc_lbuf("list_cmdtable");
    bp = buf;
    for (cp = (char *)"Commands:"; *cp; cp++)
        *bp++ = *cp;

    {
        CMDENT_NO_ARG *cmdp;
        for (cmdp = command_table_no_arg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG *cmdp;
        for (cmdp = command_table_one_arg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG_CMDARG *cmdp;
        for (cmdp = command_table_one_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG *cmdp;
        for (cmdp = command_table_two_arg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV *cmdp;
        for (cmdp = command_table_two_arg_argv; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_argv_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    *bp++ = ' ';
                    for (cp = cmdp->cmdname; *cp; cp++)
                        *bp++ = *cp;
                }
            }
        }
    }
    *bp = '\0';

    /*
     * Players get the list of logged-out cmds too 
     */

    if (Typeof(player) == TYPE_PLAYER)
        display_nametab(player, logout_cmdtable, buf, 1);
    else
        notify(player, buf);
    free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * list_attrtable: List available attributes.
 */

static void list_attrtable(dbref player)
{
    ATTR *ap;
    char *buf, *bp, *cp;

    buf = alloc_lbuf("list_attrtable");
    bp = buf;
    for (cp = (char *)"Attributes:"; *cp; cp++)
        *bp++ = *cp;
    for (ap = attr; ap->name; ap++) {
        if (See_attr(player, player, ap, player, 0)) {
            *bp++ = ' ';
            for (cp = (char *)(ap->name); *cp; cp++)
                *bp++ = *cp;
        }
    }
    *bp = '\0';
    raw_notify(player, buf);
    free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * list_cmdaccess: List access commands.
 */

NAMETAB access_nametab[] =
{
    {(char *)"builder",       6, CA_WIZARD, CA_BUILDER},
    {(char *)"dark",          4, CA_GOD,    CF_DARK},
    {(char *)"disabled",      4, CA_GOD,    CA_DISABLED},
    {(char *)"global_build",  8, CA_PUBLIC, CA_GBL_BUILD},
    {(char *)"global_interp", 8, CA_PUBLIC, CA_GBL_INTERP},
    {(char *)"god",           2, CA_GOD,    CA_GOD},
    {(char *)"head",          2, CA_WIZARD, CA_HEAD},
    {(char *)"immortal",      3, CA_WIZARD, CA_IMMORTAL},
    {(char *)"need_location", 6, CA_PUBLIC, CA_LOCATION},
    {(char *)"need_contents", 6, CA_PUBLIC, CA_CONTENTS},
    {(char *)"need_player",   6, CA_PUBLIC, CA_PLAYER},
    {(char *)"no_haven",      4, CA_PUBLIC, CA_NO_HAVEN},
    {(char *)"no_robot",      4, CA_WIZARD, CA_NO_ROBOT},
    {(char *)"no_slave",      5, CA_PUBLIC, CA_NO_SLAVE},
    {(char *)"no_suspect",    5, CA_WIZARD, CA_NO_SUSPECT},
    {(char *)"no_guest",      5, CA_WIZARD, CA_NO_GUEST},
    {(char *)"no_uninspected",5, CA_WIZARD, CA_NO_UNINS},
    {(char *)"robot",         2, CA_WIZARD, CA_ROBOT},
    {(char *)"staff",         4, CA_WIZARD, CA_STAFF},
    {(char *)"static",        4, CA_GOD,    CA_STATIC},
    {(char *)"uninspected",   5, CA_WIZARD, CA_UNINS},
    {(char *)"wizard",        3, CA_WIZARD, CA_WIZARD},
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
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG *cmdp;
        for (cmdp = command_table_one_arg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_ONE_ARG_CMDARG *cmdp;
        for (cmdp = command_table_one_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG *cmdp;
        for (cmdp = command_table_two_arg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV *cmdp;
        for (cmdp = command_table_two_arg_argv; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
            }
        }
    }
    {
        CMDENT_TWO_ARG_ARGV_CMDARG *cmdp;
        for (cmdp = command_table_two_arg_argv_cmdarg; cmdp->cmdname; cmdp++)
        {
            if (check_access(player, cmdp->perms))
            {
                if (!(cmdp->perms & CF_DARK))
                {
                    sprintf(buff, "%.60s:", cmdp->cmdname);
                    listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
                }
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
        BOOL bValid;
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
            sprintf(buff, "%.60s:", cmdp->cmdname);
            listset_nametab(player, access_nametab, cmdp->perms, buff, 1);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * list_cmdswitches: List switches for commands.
 */

static void list_cmdswitches(dbref player)
{
    char *buff;

    buff = alloc_sbuf("list_cmdswitches");
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
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
                        sprintf(buff, "%.60s:", cmdp->cmdname);
                        display_nametab(player, cmdp->switches, buff, 0);
                    }
                }
            }
        }
    }
    free_sbuf(buff);
}

/* ---------------------------------------------------------------------------
 * list_attraccess: List access to attributes.
 */

NAMETAB attraccess_nametab[] =
{
    {(char *)"dark",        2,  CA_WIZARD,  AF_DARK},
    {(char *)"deleted",     2,  CA_WIZARD,  AF_DELETED},
    {(char *)"god",         1,  CA_PUBLIC,  AF_GOD},
    {(char *)"hidden",      1,  CA_WIZARD,  AF_MDARK},
    {(char *)"ignore",      2,  CA_WIZARD,  AF_NOCMD},
    {(char *)"internal",        2,  CA_WIZARD,  AF_INTERNAL},
    {(char *)"is_lock",     4,  CA_PUBLIC,  AF_IS_LOCK},
    {(char *)"locked",      1,  CA_PUBLIC,  AF_LOCK},
    {(char *)"no_command",      4,  CA_PUBLIC,  AF_NOPROG},
    {(char *)"no_inherit",      4,  CA_PUBLIC,  AF_PRIVATE},
    {(char *)"private",     1,  CA_PUBLIC,  AF_ODARK},
    {(char *)"regexp",              1,      CA_PUBLIC,      AF_REGEXP},
    {(char *)"visual",      1,  CA_PUBLIC,  AF_VISUAL},
    {(char *)"wizard",      1,  CA_PUBLIC,  AF_WIZARD},
    { NULL,             0,  0,      0}
};

NAMETAB indiv_attraccess_nametab[] =
{
    {(char *)"hidden",      1,  CA_WIZARD,  AF_MDARK},
    {(char *)"wizard",      1,  CA_WIZARD,  AF_WIZARD},
    {(char *)"no_command",      4,  CA_PUBLIC,  AF_NOPROG},
    {(char *)"no_inherit",      4,  CA_PUBLIC,  AF_PRIVATE},
    {(char *)"visual",      1,  CA_PUBLIC,  AF_VISUAL},
    {(char *)"regexp",              1,      CA_PUBLIC,      AF_REGEXP},
    { NULL,             0,  0,      0}
};

static void list_attraccess(dbref player)
{
    char *buff;
    ATTR *ap;

    buff = alloc_sbuf("list_attraccess");
    for (ap = attr; ap->name; ap++)
    {
        if (Read_attr(player, player, ap, player, 0))
        {
            sprintf(buff, "%s:", ap->name);
            listset_nametab(player, attraccess_nametab, ap->flags, buff, 1);
        }
    }
    free_sbuf(buff);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_access: Change command or switch permissions.
 */

extern void FDECL(cf_log_notfound, (dbref, char *, const char *, char *));

CF_HAND(cf_access)
{
    CMDENT *cmdp;
    char *ap;
    int set_switch;

    for (ap = str; *ap && !Tiny_IsSpace[(unsigned char)*ap] && (*ap != '/'); ap++) ;
    if (*ap == '/') {
        set_switch = 1;
        *ap++ = '\0';
    } else {
        set_switch = 0;
        if (*ap)
            *ap++ = '\0';
        while (Tiny_IsSpace[(unsigned char)*ap])
            ap++;
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
        cf_log_notfound(player, cmd, "Command", str);
        return -1;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * cf_acmd_access: Change command permissions for all attr-setting cmds.
 */

CF_HAND(cf_acmd_access)
{
    ATTR *ap;

    for (ap = attr; ap->name; ap++)
    {
        int nBuffer;
        int bValid;
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
                free_sbuf(buff);
                return -1;
            }
        }
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_attr_access: Change access on an attribute.
 */

CF_HAND(cf_attr_access)
{
    ATTR *ap;
    char *sp;

    for (sp = str; *sp && !Tiny_IsSpace[(unsigned char)*sp]; sp++) 
    {
        ; // Nothing
    }
    if (*sp)
    {
        *sp++ = '\0';
    }
    while (Tiny_IsSpace[(unsigned char)*sp])
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

/*
 * ---------------------------------------------------------------------------
 * * cf_cmd_alias: Add a command alias.
 */

CF_HAND(cf_cmd_alias)
{
    char *ap;
    CMDENT *cmdp, *cmd2;
    NAMETAB *nt;
    int *hp;

    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t=,");
    char *alias = Tiny_StrTokParse(&tts);
    char *orig = Tiny_StrTokParse(&tts);

    if (!orig)
    {
        // We only got one argument to @alias. Bad.
        //
        return -1;
    }

    for (ap = orig; *ap && (*ap != '/'); ap++) ;
    if (*ap == '/') {

        /*
         * Switch form of command aliasing: create an alias for a  *
         * * * * * * command + a switch 
         */

        *ap++ = '\0';

        /*
         * Look up the command 
         */

        cmdp = (CMDENT *) hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
        if (cmdp == NULL || cmdp->switches == NULL)
        {
            cf_log_notfound(player, cmd, "Command", orig);
            return -1;
        }
        /*
         * Look up the switch 
         */

        nt = find_nametab_ent(player, (NAMETAB *) cmdp->switches, ap);
        if (!nt)
        {
            cf_log_notfound(player, cmd, "Switch", ap);
            return -1;
        }

        // Got it, create the new command table entry.
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
        if (hashaddLEN(cmd2->cmdname, strlen(cmd2->cmdname), (int *)cmd2, (CHashTable *) vp))
        {
            MEMFREE(cmd2->cmdname);
            MEMFREE(cmd2);
        }
    }
    else
    {

        /*
         * A normal (non-switch) alias 
         */

        hp = hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
        if (hp == NULL)
        {
            cf_log_notfound(player, cmd, "Entry", orig);
            return -1;
        }
        hashaddLEN(alias, strlen(alias), hp, (CHashTable *) vp);
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * list_df_flags: List default flags at create time.
 */

static void list_df_flags(dbref player)
{
    char *playerb, *roomb, *thingb, *exitb, *robotb, *buff;

    playerb = decode_flags(player,
                   (mudconf.player_flags.word1 | TYPE_PLAYER),
                   mudconf.player_flags.word2,
                   mudconf.player_flags.word3);
    roomb = decode_flags(player,
                 (mudconf.room_flags.word1 | TYPE_ROOM),
                 mudconf.room_flags.word2,
                 mudconf.room_flags.word3);
    exitb = decode_flags(player,
                 (mudconf.exit_flags.word1 | TYPE_EXIT),
                 mudconf.exit_flags.word2,
                 mudconf.exit_flags.word3);
    thingb = decode_flags(player,
                  (mudconf.thing_flags.word1 | TYPE_THING),
                  mudconf.thing_flags.word2,
                  mudconf.thing_flags.word3);
    robotb = decode_flags(player,
                  (mudconf.robot_flags.word1 | TYPE_PLAYER),
                  mudconf.robot_flags.word2,
                  mudconf.robot_flags.word3);
    buff = alloc_lbuf("list_df_flags");
    sprintf(buff,
        "Default flags: Players...%s Rooms...%s Exits...%s Things...%s Robots...%s",
        playerb, roomb, exitb, thingb, robotb);
    raw_notify(player, buff);
    free_lbuf(buff);
    free_sbuf(playerb);
    free_sbuf(roomb);
    free_sbuf(exitb);
    free_sbuf(thingb);
    free_sbuf(robotb);
}

/*
 * ---------------------------------------------------------------------------
 * * list_costs: List the costs of things.
 */

#define coin_name(s)    (((s)==1) ? mudconf.one_coin : mudconf.many_coins)

static void list_costs(dbref player)
{
    char *buff;

    buff = alloc_mbuf("list_costs");
    *buff = '\0';
    if (mudconf.quotas)
        sprintf(buff, " and %d quota", mudconf.room_quota);
    notify(player,
           tprintf("Digging a room costs %d %s%s.",
               mudconf.digcost, coin_name(mudconf.digcost), buff));
    if (mudconf.quotas)
        sprintf(buff, " and %d quota", mudconf.exit_quota);
    notify(player,
           tprintf("Opening a new exit costs %d %s%s.",
               mudconf.opencost, coin_name(mudconf.opencost), buff));
    notify(player,
           tprintf("Linking an exit, home, or dropto costs %d %s.",
               mudconf.linkcost, coin_name(mudconf.linkcost)));
    if (mudconf.quotas)
        sprintf(buff, " and %d quota", mudconf.thing_quota);
    if (mudconf.createmin == mudconf.createmax)
        raw_notify(player,
               tprintf("Creating a new thing costs %d %s%s.",
                   mudconf.createmin,
                   coin_name(mudconf.createmin), buff));
    else
        raw_notify(player,
        tprintf("Creating a new thing costs between %d and %d %s%s.",
            mudconf.createmin, mudconf.createmax,
            mudconf.many_coins, buff));
    if (mudconf.quotas)
        sprintf(buff, " and %d quota", mudconf.player_quota);
    notify(player,
           tprintf("Creating a robot costs %d %s%s.",
               mudconf.robotcost, coin_name(mudconf.robotcost),
               buff));
    if (mudconf.killmin == mudconf.killmax) {
        raw_notify(player,
               tprintf("Killing costs %d %s, with a %d%% chance of success.",
                mudconf.killmin, coin_name(mudconf.digcost),
                   (mudconf.killmin * 100) /
                   mudconf.killguarantee));
    } else {
        raw_notify(player,
               tprintf("Killing costs between %d and %d %s.",
                   mudconf.killmin, mudconf.killmax,
                   mudconf.many_coins));
        raw_notify(player,
               tprintf("You must spend %d %s to guarantee success.",
                   mudconf.killguarantee,
                   coin_name(mudconf.killguarantee)));
    }
    raw_notify(player,
           tprintf("Computationally expensive commands and functions (ie: @entrances, @find, @search, @stats (with an argument or switch), search(), and stats()) cost %d %s.",
            mudconf.searchcost, coin_name(mudconf.searchcost)));
    if (mudconf.machinecost > 0)
        raw_notify(player,
           tprintf("Each command run from the queue costs 1/%d %s.",
               mudconf.machinecost, mudconf.one_coin));
    if (mudconf.waitcost > 0) {
        raw_notify(player,
               tprintf("A %d %s deposit is charged for putting a command on the queue.",
                   mudconf.waitcost, mudconf.one_coin));
        raw_notify(player, "The deposit is refunded when the command is run or canceled.");
    }
    if (mudconf.sacfactor == 0)
    {
        Tiny_ltoa(mudconf.sacadjust, buff);
    }
    else if (mudconf.sacfactor == 1)
    {
        if (mudconf.sacadjust < 0)
            sprintf(buff, "<create cost> - %d", -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            sprintf(buff, "<create cost> + %d", mudconf.sacadjust);
        else
            sprintf(buff, "<create cost>");
    }
    else
    {
        if (mudconf.sacadjust < 0)
            sprintf(buff, "(<create cost> / %d) - %d", mudconf.sacfactor, -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            sprintf(buff, "(<create cost> / %d) + %d", mudconf.sacfactor, mudconf.sacadjust);
        else
            sprintf(buff, "<create cost> / %d", mudconf.sacfactor);
    }
    raw_notify(player, tprintf("The value of an object is %s.", buff));
    if (mudconf.clone_copy_cost)
        raw_notify(player, "The default value of cloned objects is the value of the original object.");
    else
        raw_notify(player, tprintf("The default value of cloned objects is %d %s.",
                mudconf.createmin, coin_name(mudconf.createmin)));

    free_mbuf(buff);
}

/*
 * ---------------------------------------------------------------------------
 * * list_options: List more game options from mudconf.
 */

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
    if (mudconf.match_mine) {
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
        raw_notify(player, "A seperate name cache is used.");
#if !defined(VMS) && !defined(WIN32)
    if (mudconf.fork_dump)
    {
        raw_notify(player, "Database dumps are performed by a fork()ed process.");
    }
#endif // WIN32
    if (mudconf.max_players >= 0)
        raw_notify(player,
        tprintf("There may be at most %d players logged in at once.",
            mudconf.max_players));
    if (mudconf.quotas)
        sprintf(buff, " and %d quota", mudconf.start_quota);
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

    sprintf(buff, "Intervals: Dump...%d  Clean...%d  Idlecheck...%d",
        mudconf.dump_interval, mudconf.check_interval,
        mudconf.idle_interval);
    raw_notify(player, buff);

    CLinearTimeDelta ltdDump = mudstate.dump_counter - ltaNow;
    CLinearTimeDelta ltdCheck = mudstate.check_counter - ltaNow;
    CLinearTimeDelta ltdIdle = mudstate.idle_counter - ltaNow;
    sprintf(buff, "Timers: Dump...%ld  Clean...%ld  Idlecheck...%ld",
        ltdDump.ReturnSeconds(), ltdCheck.ReturnSeconds(), ltdIdle.ReturnSeconds());
    raw_notify(player, buff);

    sprintf(buff, "Timeouts: Idle...%d  Connect...%d  Tries...%d",
        mudconf.idle_timeout, mudconf.conn_timeout,
        mudconf.retry_limit);
    raw_notify(player, buff);

    sprintf(buff, "Scheduling: Timeslice...%d  Max_Quota...%d  Increment...%d",
        mudconf.timeslice, mudconf.cmd_quota_max, mudconf.cmd_quota_incr);
    raw_notify(player, buff);

    sprintf(buff, "Spaces...%s  Savefiles...%s",
        ed[mudconf.space_compress], ed[mudconf.compress_db]);
    raw_notify(player, buff);

    sprintf(buff, "New characters: Room...#%d  Home...#%d  DefaultHome...#%d  Quota...%d",
        mudconf.start_room, mudconf.start_home, mudconf.default_home,
        mudconf.start_quota);
    raw_notify(player, buff);

    sprintf(buff, "Misc: GuestChar...#%d  IdleQueueChunk...%d  ActiveQueueChunk...%d  Master_room...#%d",
        mudconf.guest_char, mudconf.queue_chunk,
        mudconf.active_q_chunk, mudconf.master_room);
    raw_notify(player, buff);

    free_mbuf(buff);
}

/*
 * ---------------------------------------------------------------------------
 * * list_vattrs: List user-defined attributes
 */

static void list_vattrs(dbref player)
{
    VATTR *va;
    int na;
    char *buff;

    buff = alloc_lbuf("list_vattrs");
    raw_notify(player, "--- User-Defined Attributes ---");
    for (va = vattr_first(), na = 0; va; va = vattr_next(va), na++) {
        if (!(va->flags & AF_DELETED)) {
            sprintf(buff, "%s(%d):", va->name, va->number);
            listset_nametab(player, attraccess_nametab, va->flags,
                    buff, 1);
        }
    }

    raw_notify(player, tprintf("%d attributes, next=%d",
                   na, mudstate.attr_next));
    free_lbuf(buff);
}

int LeftJustifyString(char *field, int nWidth, const char *value)
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

int RightJustifyNumber(char *field, int nWidth, INT64 value)
{
    char buffer[22];
    int n = Tiny_i64toa(value, buffer);
    int nReturn = n;
    if (n < nWidth)
    {
        memset(field, ' ', nWidth-n);
        field += nWidth-n;
        nReturn = nWidth;
    }
    memcpy(field, buffer, n);
    return nReturn;
}

// list_hashstats: List information from hash tables
//
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
    raw_notify(player, "Hash Stats      Size Entries Deleted    Lookups      Hits       Checks   Longest");
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
    list_hashstat(player, "News topics", &mudstate.news_htab);
    list_hashstat(player, "Help topics", &mudstate.help_htab);
    list_hashstat(player, "Wizhelp topics", &mudstate.wizhelp_htab);
    list_hashstat(player, "+Help topics", &mudstate.plushelp_htab);
    list_hashstat(player, "+Shelp topics", &mudstate.staffhelp_htab);
    list_hashstat(player, "Wiznews topics", &mudstate.wiznews_htab);
    list_hashstat(player, "Attribute Cache", &mudstate.acache_htab);
}

#ifndef MEMORY_BASED
//
// These are from 'svdhash.cpp'.
//
extern CLinearTimeAbsolute cs_ltime;
extern int cs_writes;       // total writes
extern int cs_reads;        // total reads
extern int cs_dels;         // total deletes
extern int cs_fails;        // attempts to grab nonexistent
extern int cs_syncs;        // total cache syncs
extern int cs_dbreads;      // total read-throughs
extern int cs_dbwrites;     // total write-throughs
extern int cs_rhits;        // total reads filled from cache
extern int cs_whits;        // total writes to dirty cache
#endif // !MEMORY_BASED


#ifdef RADIX_COMPRESSION
extern unsigned int strings_compressed;     // Total number of compressed strings  
extern unsigned int strings_decompressed;   // Total number of decompressed strings  
extern unsigned int chars_in;               // Total characters compressed
extern unsigned int symbols_out;            // Total symbols emitted
#endif // RADIX_COMPRESSION  

/*
 * ---------------------------------------------------------------------------
 * * list_db_stats: Get useful info from the DB layer about hash stats, etc.
 */

static void list_db_stats(dbref player)
{
#ifdef MEMORY_BASED
    raw_notify(player, "Database is memory based.");
#else
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    CLinearTimeDelta ltd = lsaNow - cs_ltime;
    raw_notify(player, tprintf("DB Cache Stats   Writes       Reads  (over %d seconds)", ltd.ReturnSeconds()));
    raw_notify(player, tprintf("Calls      %12d%12d", cs_writes, cs_reads));
    raw_notify(player, tprintf("\nDeletes    %12d", cs_dels));
    raw_notify(player, tprintf("Syncs      %12d", cs_syncs));
    raw_notify(player, tprintf("I/O        %12d%12d", cs_dbwrites, cs_dbreads));
    raw_notify(player, tprintf("Cache Hits %12d%12d", cs_whits, cs_rhits));
#endif // !MEMORY_BASED

#ifdef RADIX_COMPRESSION
    raw_notify(player, "Compression statistics:");
    raw_notify(player, tprintf("Strings compressed %d", strings_compressed));
    raw_notify(player, tprintf("Strings decompressed %d", strings_decompressed));
    raw_notify(player, tprintf("Compression ratio %d:%d", chars_in, symbols_out + (symbols_out >> 1)));
#endif // RADIX_COMPRESSION
}

/*
 * ---------------------------------------------------------------------------
 * * list_process: List local resource usage stats of the mux process.
 * * Adapted from code by Claudius@PythonMUCK,
 * *     posted to the net by Howard/Dark_Lord.
 */

static void list_process(dbref player)
{
    int maxfds;

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
#endif

#ifdef WIN32
    maxfds = FD_SETSIZE;
#else // WIN32
#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else
    maxfds = sysconf(_SC_OPEN_MAX);
#endif
    int psize = getpagesize();
#endif // WIN32

    /*
     * Go display everything 
     */

#ifdef WIN32
    raw_notify(player, tprintf("Process ID:  %10d", game_pid));
#else // WIN32
    raw_notify(player, tprintf("Process ID:  %10d        %10d bytes per page", game_pid, psize));
#endif // WIN32

#ifdef HAVE_GETRUSAGE
    raw_notify(player, tprintf("Time used:   %10d user   %10d sys",
               usage.ru_utime.tv_sec, usage.ru_stime.tv_sec));
/*
 * raw_notify(player,
 * * tprintf("Resident mem:%10d shared %10d private%10d stack",
 * * ixrss, idrss, isrss));
 */
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
#endif
}
/*
 * ---------------------------------------------------------------------------
 * * do_list: List information stored in internal structures.
 */

#define LIST_ATTRIBUTES 1
#define LIST_COMMANDS   2
#define LIST_COSTS  3
#define LIST_FLAGS  4
#define LIST_FUNCTIONS  5
#define LIST_GLOBALS    6
#define LIST_ALLOCATOR  7
#define LIST_LOGGING    8
#define LIST_DF_FLAGS   9
#define LIST_PERMS  10
#define LIST_ATTRPERMS  11
#define LIST_OPTIONS    12
#define LIST_HASHSTATS  13
#define LIST_BUFTRACE   14
#define LIST_CONF_PERMS 15
#define LIST_SITEINFO   16
#define LIST_POWERS 17
#define LIST_SWITCHES   18
#define LIST_VATTRS 19
#define LIST_DB_STATS   20
#define LIST_PROCESS    21
#define LIST_BADNAMES   22
#define LIST_RESOURCES  23

NAMETAB list_names[] =
{
    {(char *)"allocations",     2,  CA_WIZARD,  LIST_ALLOCATOR},
    {(char *)"attr_permissions",    5,  CA_WIZARD,  LIST_ATTRPERMS},
    {(char *)"attributes",      2,  CA_PUBLIC,  LIST_ATTRIBUTES},
    {(char *)"bad_names",       2,  CA_WIZARD,  LIST_BADNAMES},
    {(char *)"buffers",     2,  CA_WIZARD,  LIST_BUFTRACE},
    {(char *)"commands",        3,  CA_PUBLIC,  LIST_COMMANDS},
    {(char *)"config_permissions",  3,  CA_GOD,     LIST_CONF_PERMS},
    {(char *)"costs",       3,  CA_PUBLIC,  LIST_COSTS},
    {(char *)"db_stats",        2,  CA_WIZARD,  LIST_DB_STATS},
    {(char *)"default_flags",   1,  CA_PUBLIC,  LIST_DF_FLAGS},
    {(char *)"flags",       2,  CA_PUBLIC,  LIST_FLAGS},
    {(char *)"functions",       2,  CA_PUBLIC,  LIST_FUNCTIONS},
    {(char *)"globals",     1,  CA_WIZARD,  LIST_GLOBALS},
    {(char *)"hashstats",       1,  CA_WIZARD,  LIST_HASHSTATS},
    {(char *)"logging",     1,  CA_GOD,     LIST_LOGGING},
    {(char *)"options",     1,  CA_PUBLIC,  LIST_OPTIONS},
    {(char *)"permissions",     2,  CA_WIZARD,  LIST_PERMS},
    {(char *)"powers",      2,  CA_WIZARD,  LIST_POWERS},
    {(char *)"process",     2,  CA_WIZARD,  LIST_PROCESS},
    {(char *)"resources",     1,  CA_WIZARD,  LIST_RESOURCES},
    {(char *)"site_information",    2,  CA_WIZARD,  LIST_SITEINFO},
    {(char *)"switches",        2,  CA_PUBLIC,  LIST_SWITCHES},
    {(char *)"user_attributes", 1,  CA_WIZARD,  LIST_VATTRS},
    { NULL,             0,  0,      0}
};

extern NAMETAB enable_names[];
extern NAMETAB logoptions_nametab[];
extern NAMETAB logdata_nametab[];

void do_list(dbref player, dbref cause, int extra, char *arg)
{
    int flagvalue;

    flagvalue = search_nametab(player, list_names, arg);
    switch (flagvalue) {
    case LIST_ALLOCATOR:
        list_bufstats(player);
        break;
    case LIST_BUFTRACE:
        list_buftrace(player);
        break;
    case LIST_ATTRIBUTES:
        list_attrtable(player);
        break;
    case LIST_COMMANDS:
        list_cmdtable(player);
        break;
    case LIST_SWITCHES:
        list_cmdswitches(player);
        break;
    case LIST_COSTS:
        list_costs(player);
        break;
    case LIST_OPTIONS:
        list_options(player);
        break;
    case LIST_HASHSTATS:
        list_hashstats(player);
        break;
    case LIST_SITEINFO:
        list_siteinfo(player);
        break;
    case LIST_FLAGS:
        display_flagtab(player);
        break;
    case LIST_FUNCTIONS:
        list_functable(player);
        break;
    case LIST_GLOBALS:
        interp_nametab(player, enable_names, mudconf.control_flags,
                (char *)"Global parameters:", (char *)"enabled",
                   (char *)"disabled");
        break;
    case LIST_DF_FLAGS:
        list_df_flags(player);
        break;
    case LIST_PERMS:
        list_cmdaccess(player);
        break;
    case LIST_CONF_PERMS:
        list_cf_access(player);
        break;
    case LIST_POWERS:
        display_powertab(player);
        break;
    case LIST_ATTRPERMS:
        list_attraccess(player);
        break;
    case LIST_VATTRS:
        list_vattrs(player);
        break;
    case LIST_LOGGING:
        interp_nametab(player, logoptions_nametab, mudconf.log_options,
                   (char *)"Events Logged:", (char *)"enabled",
                   (char *)"disabled");
        interp_nametab(player, logdata_nametab, mudconf.log_info,
                   (char *)"Information Logged:", (char *)"yes",
                   (char *)"no");
        break;
    case LIST_DB_STATS:
        list_db_stats(player);
        break;
    case LIST_PROCESS:
        list_process(player);
        break;
    case LIST_BADNAMES:
        badname_list(player, "Disallowed names:");
        break;
    case LIST_RESOURCES:
        list_system_resources(player);
        break;
    default:
        display_nametab(player, list_names,
                (char *)"Unknown option.  Use one of:", 1);
    }
}

