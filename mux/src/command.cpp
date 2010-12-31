/*! \file command.cpp
 * \brief Command parser and support routines.
 *
 * $Id$
 *
 * The functions here crack command lists into commands, decode switches, and
 * match commands to built-in and softcoded commands.  This is one of the
 * three parsers in the server.  The other two parsers are for functions
 * (see eval.cpp) and locks (boolexp.cpp).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "mguests.h"
#include "mathutil.h"
#include "powers.h"
#include "vattr.h"
#include "pcre.h"

// Switch tables for the various commands.
//
static NAMETAB attrib_sw[] =
{
    {T("access"),          1,  CA_GOD,     ATTRIB_ACCESS},
    {T("delete"),          1,  CA_GOD,     ATTRIB_DELETE},
    {T("rename"),          1,  CA_GOD,     ATTRIB_RENAME},
    {(UTF8 *) NULL,        0,       0,     0}
};

static NAMETAB boot_sw[] =
{
    {T("port"),            1,  CA_WIZARD,  BOOT_PORT|SW_MULTIPLE},
    {T("quiet"),           1,  CA_WIZARD,  BOOT_QUIET|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB break_sw[] =
{
    {T("inline"),          1,  CA_WIZARD,  BREAK_INLINE},
    {T("queued"),          1,  CA_WIZARD,  BREAK_QUEUED},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB cboot_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  CBOOT_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB comtitle_sw[] =
{
    {T("off"),             2,  CA_PUBLIC,  COMTITLE_OFF},
    {T("on"),              2,  CA_PUBLIC,  COMTITLE_ON},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB cemit_sw[] =
{
    {T("noheader"),        1,  CA_PUBLIC,  CEMIT_NOHEADER},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB chown_sw[] =
{
    {T("nostrip"),         1,  CA_PUBLIC,  CHOWN_NOSTRIP},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB clone_sw[] =
{
    {T("cost"),            1,  CA_PUBLIC,  CLONE_SET_COST},
    {T("inherit"),         3,  CA_PUBLIC,  CLONE_INHERIT|SW_MULTIPLE},
    {T("inventory"),       3,  CA_PUBLIC,  CLONE_INVENTORY},
    {T("location"),        1,  CA_PUBLIC,  CLONE_LOCATION},
    {T("nostrip"),         2,  CA_WIZARD,  CLONE_NOSTRIP|SW_MULTIPLE},
    {T("parent"),          2,  CA_PUBLIC,  CLONE_FROM_PARENT|SW_MULTIPLE},
    {T("preserve"),        2,  CA_WIZARD,  CLONE_PRESERVE|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB clist_sw[] =
{
    {T("full"),            0,  CA_PUBLIC,  CLIST_FULL},
    {T("headers"),         0,  CA_PUBLIC,  CLIST_HEADERS},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB cset_sw[] =
{
    {T("anon"),            1,  CA_PUBLIC,  CSET_SPOOF},
    {T("header"),          1,  CA_PUBLIC,  CSET_HEADER},
    {T("list"),            2,  CA_PUBLIC,  CSET_LIST},
    {T("log") ,            3,  CA_PUBLIC,  CSET_LOG},
    {T("loud"),            3,  CA_PUBLIC,  CSET_LOUD},
    {T("mute"),            1,  CA_PUBLIC,  CSET_QUIET},
    {T("nospoof"),         1,  CA_PUBLIC,  CSET_NOSPOOF},
    {T("object"),          1,  CA_PUBLIC,  CSET_OBJECT},
    {T("private"),         2,  CA_PUBLIC,  CSET_PRIVATE},
    {T("public"),          2,  CA_PUBLIC,  CSET_PUBLIC},
    {T("quiet"),           1,  CA_PUBLIC,  CSET_QUIET},
    {T("spoof"),           1,  CA_PUBLIC,  CSET_SPOOF},
    {T("timestamp_logs"),  1,  CA_PUBLIC,  CSET_LOG_TIME},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB dbck_sw[] =
{
    {T("full"),            1,  CA_WIZARD,  DBCK_FULL},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB decomp_sw[] =
{
    {T("dbref"),           1,  CA_PUBLIC,  DECOMP_DBREF},
    {(UTF8 *) NULL,        0,           0, 0}
};

static NAMETAB destroy_sw[] =
{
    {T("instant"),         4,  CA_PUBLIC,  DEST_INSTANT|SW_MULTIPLE},
    {T("override"),        8,  CA_PUBLIC,  DEST_OVERRIDE|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB dig_sw[] =
{
    {T("teleport"),        1,  CA_PUBLIC,  DIG_TELEPORT},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB doing_sw[] =
{
    {T("header"),          1,  CA_PUBLIC,  DOING_HEADER},
    {T("message"),         1,  CA_PUBLIC,  DOING_MESSAGE},
    {T("poll"),            1,  CA_PUBLIC,  DOING_POLL},
    {T("quiet"),           1,  CA_PUBLIC,  DOING_QUIET|SW_MULTIPLE},
    {T("unique"),          1,  CA_PUBLIC,  DOING_UNIQUE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB dolist_sw[] =
{
    {T("delimit"),         1,  CA_PUBLIC,  DOLIST_DELIMIT},
    {T("notify"),          1,  CA_PUBLIC,  DOLIST_NOTIFY|SW_MULTIPLE},
    {T("space"),           1,  CA_PUBLIC,  DOLIST_SPACE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB query_sw[] =
{
    {T("sql"),             1,  CA_PUBLIC,  QUERY_SQL},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB drop_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  DROP_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB dump_sw[] =
{
    {T("flatfile"),        1,  CA_WIZARD,  DUMP_FLATFILE|SW_MULTIPLE},
    {T("structure"),       1,  CA_WIZARD,  DUMP_STRUCT|SW_MULTIPLE},
    {T("text"),            1,  CA_WIZARD,  DUMP_TEXT|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB emit_sw[] =
{
    {T("here"),            2,  CA_PUBLIC,  SAY_HERE|SW_MULTIPLE},
    {T("html"),            2,  CA_PUBLIC,  SAY_HTML|SW_MULTIPLE},
    {T("room"),            1,  CA_PUBLIC,  SAY_ROOM|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB enter_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  MOVE_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB examine_sw[] =
{
    {T("brief"),           1,  CA_PUBLIC,  EXAM_BRIEF},
    {T("debug"),           1,  CA_WIZARD,  EXAM_DEBUG},
    {T("full"),            1,  CA_PUBLIC,  EXAM_LONG},
    {T("parent"),          1,  CA_PUBLIC,  EXAM_PARENT},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB femit_sw[] =
{
    {T("here"),            1,  CA_PUBLIC,  PEMIT_HERE|SW_MULTIPLE},
    {T("room"),            1,  CA_PUBLIC,  PEMIT_ROOM|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB fixdb_sw[] =
{
    {T("contents"),        1,     CA_GOD,  FIXDB_CON},
    {T("exits"),           1,     CA_GOD,  FIXDB_EXITS},
    {T("location"),        1,     CA_GOD,  FIXDB_LOC},
    {T("next"),            1,     CA_GOD,  FIXDB_NEXT},
    {T("owner"),           1,     CA_GOD,  FIXDB_OWNER},
    {T("pennies"),         1,     CA_GOD,  FIXDB_PENNIES},
    {T("rename"),          1,     CA_GOD,  FIXDB_NAME},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB flag_sw[] =
{
    {T("remove"),          1,     CA_GOD,  FLAG_REMOVE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB folder_sw[] =
{
    {T("file"),            1,    CA_PUBLIC, FOLDER_FILE},
    {T("list"),            1,    CA_PUBLIC, FOLDER_LIST},
    {T("read"),            1,    CA_PUBLIC, FOLDER_READ},
    {T("set"),             1,    CA_PUBLIC, FOLDER_SET},
    {(UTF8 *) NULL,        0,          0,  0}
};
static NAMETAB fpose_sw[] =
{
    {T("default"),         1,  CA_PUBLIC,  0},
    {T("nospace"),         1,  CA_PUBLIC,  SAY_NOSPACE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB function_sw[] =
{
    {T("delete"),          1,  CA_WIZARD,  FN_DELETE},
    {T("list"),            1,  CA_WIZARD,  FN_LIST},
    {T("preserve"),        3,  CA_WIZARD,  FN_PRES|SW_MULTIPLE},
    {T("privileged"),      3,  CA_WIZARD,  FN_PRIV|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB get_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  GET_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB give_sw[] =
{
    {T("quiet"),           1,  CA_WIZARD,  GIVE_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB goto_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  MOVE_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB halt_sw[] =
{
    {T("all"),             1,  CA_PUBLIC,  HALT_ALL},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB hook_sw[] =
{
    {T("after"),           3,     CA_GOD,  CEF_HOOK_AFTER},
    {T("before"),          3,     CA_GOD,  CEF_HOOK_BEFORE},
    {T("clear"),           3,     CA_GOD,  CEF_HOOK_CLEAR|SW_MULTIPLE},
    {T("fail"),            1,     CA_GOD,  CEF_HOOK_AFAIL},
    {T("ignore"),          3,     CA_GOD,  CEF_HOOK_IGNORE},
    {T("igswitch"),        3,     CA_GOD,  CEF_HOOK_IGSWITCH},
    {T("list"),            3,     CA_GOD,  CEF_HOOK_LIST},
    {T("permit"),          3,     CA_GOD,  CEF_HOOK_PERMIT},
    {T("args"),            3,     CA_GOD,  CEF_HOOK_ARGS},
    {(UTF8 *)NULL,         0,          0,  0}
};

static NAMETAB icmd_sw[] =
{
    {T("check"),           2,     CA_GOD,  ICMD_CHECK},
    {T("clear"),           2,     CA_GOD,  ICMD_CLEAR},
    {T("croom"),           2,     CA_GOD,  ICMD_CROOM},
    {T("disable"),         1,     CA_GOD,  ICMD_DISABLE},
    {T("droom"),           2,     CA_GOD,  ICMD_DROOM},
    {T("ignore"),          1,     CA_GOD,  ICMD_IGNORE},
    {T("iroom"),           2,     CA_GOD,  ICMD_IROOM},
    {T("lroom"),           2,     CA_GOD,  ICMD_LROOM},
    {T("lallroom"),        2,     CA_GOD,  ICMD_LALLROOM},
    {T("off"),             2,     CA_GOD,  ICMD_OFF},
    {T("on"),              2,     CA_GOD,  ICMD_ON},
    {(UTF8 *)NULL,         0,          0,  0}
};

static NAMETAB leave_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  MOVE_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB listmotd_sw[] =
{
    {T("brief"),           1,  CA_WIZARD,  MOTD_BRIEF},
    {(UTF8 *) NULL,        0,          0,  0}
};

NAMETAB lock_sw[] =
{
    {T("defaultlock"),     1,  CA_PUBLIC,  A_LOCK},
    {T("droplock"),        1,  CA_PUBLIC,  A_LDROP},
    {T("enterlock"),       1,  CA_PUBLIC,  A_LENTER},
    {T("getfromlock"),     1,  CA_PUBLIC,  A_LGET},
    {T("givelock"),        1,  CA_PUBLIC,  A_LGIVE},
    {T("leavelock"),       2,  CA_PUBLIC,  A_LLEAVE},
    {T("linklock"),        2,  CA_PUBLIC,  A_LLINK},
    {T("maillock"),        1,  CA_PUBLIC,  A_LMAIL},
    {T("openlock"),        1,  CA_PUBLIC,  A_LOPEN},
    {T("pagelock"),        3,  CA_PUBLIC,  A_LPAGE},
    {T("parentlock"),      3,  CA_PUBLIC,  A_LPARENT},
    {T("receivelock"),     1,  CA_PUBLIC,  A_LRECEIVE},
    {T("speechlock"),      1,  CA_PUBLIC,  A_LSPEECH},
    {T("teloutlock"),      2,  CA_PUBLIC,  A_LTELOUT},
    {T("tportlock"),       2,  CA_PUBLIC,  A_LTPORT},
    {T("uselock"),         1,  CA_PUBLIC,  A_LUSE},
    {T("userlock"),        4,  CA_PUBLIC,  A_LUSER},
    {T("visiblelock"),     1,  CA_PUBLIC,  A_LVISIBLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB look_sw[] =
{
    {T("outside"),         1,  CA_PUBLIC,  LOOK_OUTSIDE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB mail_sw[] =
{
    {T("abort"),           2,  CA_PUBLIC,  MAIL_ABORT},
    {T("alias"),           4,  CA_PUBLIC,  MAIL_ALIAS},
    {T("alist"),           4,  CA_PUBLIC,  MAIL_ALIST},
    {T("bcc"),             1,  CA_PUBLIC,  MAIL_BCC},
    {T("cc"),              2,  CA_PUBLIC,  MAIL_CC},
    {T("clear"),           2,  CA_PUBLIC,  MAIL_CLEAR},
    {T("debug"),           2,  CA_PUBLIC,  MAIL_DEBUG},
    {T("dstats"),          2,  CA_PUBLIC,  MAIL_DSTATS},
    {T("edit"),            1,  CA_PUBLIC,  MAIL_EDIT},
    {T("file"),            2,  CA_PUBLIC,  MAIL_FILE},
    {T("folder"),          3,  CA_PUBLIC,  MAIL_FOLDER},
    {T("forward"),         3,  CA_PUBLIC,  MAIL_FORWARD},
    {T("fstats"),          2,  CA_PUBLIC,  MAIL_FSTATS},
    {T("fwd"),             2,  CA_PUBLIC,  MAIL_FORWARD},
    {T("list"),            1,  CA_PUBLIC,  MAIL_LIST},
    {T("nuke"),            1,  CA_PUBLIC,  MAIL_NUKE},
    {T("proof"),           2,  CA_PUBLIC,  MAIL_PROOF},
    {T("purge"),           2,  CA_PUBLIC,  MAIL_PURGE},
    {T("quick"),           3,  CA_PUBLIC,  MAIL_QUICK},
    {T("quote"),           3,  CA_PUBLIC,  MAIL_QUOTE|SW_MULTIPLE},
    {T("read"),            3,  CA_PUBLIC,  MAIL_READ},
    {T("reply"),           3,  CA_PUBLIC,  MAIL_REPLY},
    {T("replyall"),        6,  CA_PUBLIC,  MAIL_REPLYALL},
    {T("retract"),         3,  CA_PUBLIC,  MAIL_RETRACT},
    {T("review"),          3,  CA_PUBLIC,  MAIL_REVIEW},
    {T("safe"),            2,  CA_PUBLIC,  MAIL_SAFE},
    {T("send"),            2,  CA_PUBLIC,  MAIL_SEND},
    {T("stats"),           2,  CA_PUBLIC,  MAIL_STATS},
    {T("tag"),             1,  CA_PUBLIC,  MAIL_TAG},
    {T("unclear"),         3,  CA_PUBLIC,  MAIL_UNCLEAR},
    {T("untag"),           3,  CA_PUBLIC,  MAIL_UNTAG},
    {T("urgent"),          2,  CA_PUBLIC,  MAIL_URGENT},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB malias_sw[] =
{
    {T("add"),             1,  CA_PUBLIC,  MALIAS_ADD},
    {T("chown"),           1,  CA_PUBLIC,  MALIAS_CHOWN},
    {T("desc"),            1,  CA_PUBLIC,  MALIAS_DESC},
    {T("delete"),          1,  CA_PUBLIC,  MALIAS_DELETE},
    {T("list"),            1,  CA_PUBLIC,  MALIAS_LIST},
    {T("remove"),          1,  CA_PUBLIC,  MALIAS_REMOVE},
    {T("rename"),          1,  CA_PUBLIC,  MALIAS_RENAME},
    {T("status"),          1,  CA_PUBLIC,  MALIAS_STATUS},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB mark_sw[] =
{
    {T("clear"),           1,  CA_PUBLIC,  MARK_CLEAR},
    {T("set"),             1,  CA_PUBLIC,  MARK_SET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB markall_sw[] =
{
    {T("clear"),           1,  CA_PUBLIC,  MARK_CLEAR},
    {T("set"),             1,  CA_PUBLIC,  MARK_SET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB motd_sw[] =
{
    {T("brief"),           1,  CA_WIZARD,  MOTD_BRIEF|SW_MULTIPLE},
    {T("connect"),         1,  CA_WIZARD,  MOTD_ALL},
    {T("down"),            1,  CA_WIZARD,  MOTD_DOWN},
    {T("full"),            1,  CA_WIZARD,  MOTD_FULL},
    {T("list"),            1,  CA_PUBLIC,  MOTD_LIST},
    {T("wizard"),          1,  CA_WIZARD,  MOTD_WIZ},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB notify_sw[] =
{
    {T("all"),             1,  CA_PUBLIC,  NFY_NFYALL},
    {T("first"),           1,  CA_PUBLIC,  NFY_NFY},
    {T("quiet"),           1,  CA_PUBLIC,  NFY_QUIET|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB open_sw[] =
{
    {T("inventory"),       1,  CA_PUBLIC,  OPEN_INVENTORY},
    {T("location"),        1,  CA_PUBLIC,  OPEN_LOCATION},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB page_sw[] =
{
    {T("noeval"),          1,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB pemit_sw[] =
{
    {T("contents"),        1,  CA_PUBLIC,  PEMIT_CONTENTS|SW_MULTIPLE},
    {T("html"),            1,  CA_PUBLIC,  PEMIT_HTML|SW_MULTIPLE},
    {T("list"),            1,  CA_PUBLIC,  PEMIT_LIST|SW_MULTIPLE},
    {T("noeval"),          1,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {T("object"),          1,  CA_PUBLIC,  0},
    {T("silent"),          1,  CA_PUBLIC,  0},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB pose_sw[] =
{
    {T("default"),         1,  CA_PUBLIC,  0},
    {T("noeval"),          3,  CA_PUBLIC,  SW_NOEVAL|SW_MULTIPLE},
    {T("nospace"),         3,  CA_PUBLIC,  SAY_NOSPACE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB ps_sw[] =
{
    {T("all"),             1,  CA_PUBLIC,  PS_ALL|SW_MULTIPLE},
    {T("brief"),           1,  CA_PUBLIC,  PS_BRIEF},
    {T("long"),            1,  CA_PUBLIC,  PS_LONG},
    {T("summary"),         1,  CA_PUBLIC,  PS_SUMM},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB quota_sw[] =
{
    {T("all"),             1,  CA_GOD,     QUOTA_ALL|SW_MULTIPLE},
    {T("fix"),             1,  CA_WIZARD,  QUOTA_FIX},
    {T("remaining"),       1,  CA_WIZARD,  QUOTA_REM|SW_MULTIPLE},
    {T("set"),             1,  CA_WIZARD,  QUOTA_SET},
    {T("total"),           1,  CA_WIZARD,  QUOTA_TOT|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB reference_sw[] =
{
    {T("list"),            1,  CA_PUBLIC,  REFERENCE_LIST},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB say_sw[] =
{
    {T("noeval"),          1,  CA_PUBLIC,  SAY_NOEVAL|SW_NOEVAL|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB set_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  SET_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB stats_sw[] =
{
    {T("all"),             1,  CA_PUBLIC,  STAT_ALL},
    {T("me"),              1,  CA_PUBLIC,  STAT_ME},
    {T("player"),          1,  CA_PUBLIC,  STAT_PLAYER},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB sweep_sw[] =
{
    {T("commands"),        3,  CA_PUBLIC,  SWEEP_COMMANDS|SW_MULTIPLE},
    {T("connected"),       3,  CA_PUBLIC,  SWEEP_CONNECT|SW_MULTIPLE},
    {T("exits"),           1,  CA_PUBLIC,  SWEEP_EXITS|SW_MULTIPLE},
    {T("here"),            1,  CA_PUBLIC,  SWEEP_HERE|SW_MULTIPLE},
    {T("inventory"),       1,  CA_PUBLIC,  SWEEP_ME|SW_MULTIPLE},
    {T("listeners"),       1,  CA_PUBLIC,  SWEEP_LISTEN|SW_MULTIPLE},
    {T("players"),         1,  CA_PUBLIC,  SWEEP_PLAYER|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB switch_sw[] =
{
    {T("all"),             1,  CA_PUBLIC,  SWITCH_ANY},
    {T("default"),         1,  CA_PUBLIC,  SWITCH_DEFAULT},
    {T("first"),           1,  CA_PUBLIC,  SWITCH_ONE},
    {T("notify"),          1,  CA_PUBLIC,  SWITCH_NOTIFY|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB teleport_sw[] =
{
    {T("list"),            1,  CA_PUBLIC,  TELEPORT_LIST|SW_MULTIPLE},
    {T("loud"),            1,  CA_PUBLIC,  TELEPORT_DEFAULT},
    {T("quiet"),           1,  CA_PUBLIC,  TELEPORT_QUIET},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB timecheck_sw[] =
{
    {T("log"),             1,  CA_WIZARD,  TIMECHK_LOG | SW_MULTIPLE},
    {T("reset"),           1,  CA_WIZARD,  TIMECHK_RESET | SW_MULTIPLE},
    {T("screen"),          1,  CA_WIZARD,  TIMECHK_SCREEN | SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB toad_sw[] =
{
    {T("no_chown"),        1,  CA_WIZARD,  TOAD_NO_CHOWN|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB trig_sw[] =
{
    {T("quiet"),           1,  CA_PUBLIC,  TRIG_QUIET},
    {T("notify"),          1,  CA_PUBLIC,  TRIG_NOTIFY|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
};

static NAMETAB wait_sw[] =
{
    {T("until"),           1,  CA_PUBLIC,   WAIT_UNTIL},
    {(UTF8 *) NULL,        0,          0,   0}
};

static NAMETAB verb_sw[] =
{
    {T("no_name"),         3,  CA_PUBLIC,   VERB_NONAME},
    {(UTF8 *) NULL,        0,          0,   0}
};

static NAMETAB wall_sw[] =
{
    {T("admin"),           1,  CA_ADMIN,    SHOUT_ADMIN|SW_MULTIPLE},
    {T("emit"),            1,  CA_ANNOUNCE, SHOUT_EMIT|SW_MULTIPLE},
    {T("no_prefix"),       1,  CA_ANNOUNCE, SHOUT_NOTAG|SW_MULTIPLE},
    {T("pose"),            1,  CA_ANNOUNCE, SHOUT_POSE|SW_MULTIPLE},
    {T("wizard"),          1,  CA_ANNOUNCE, SHOUT_WIZARD|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,   0}
};

static NAMETAB warp_sw[] =
{
    {T("check"),           1,  CA_WIZARD,  TWARP_CLEAN|SW_MULTIPLE},
    {T("dump"),            1,  CA_WIZARD,  TWARP_DUMP|SW_MULTIPLE},
    {T("events"),          1,  CA_WIZARD,  TWARP_EVENTS|SW_MULTIPLE},
    {T("idle"),            1,  CA_WIZARD,  TWARP_IDLE|SW_MULTIPLE},
    {T("queue"),           1,  CA_WIZARD,  TWARP_QUEUE|SW_MULTIPLE},
    {(UTF8 *) NULL,        0,          0,  0}
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
    {T("@@"),          NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_comment},
    {T("@backup"),     NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, do_backup},
    {T("@dbck"),       dbck_sw,    CA_WIZARD,   0,          CS_NO_ARGS, 0, do_dbck},
    {T("@dbclean"),    NULL,       CA_GOD,      0,          CS_NO_ARGS, 0, do_dbclean},
    {T("@dump"),       dump_sw,    CA_WIZARD,   0,          CS_NO_ARGS, 0, do_dump},
    {T("@mark_all"),   markall_sw, CA_WIZARD,   MARK_SET,   CS_NO_ARGS, 0, do_markall},
    {T("@readcache"),  NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, do_readcache},
    {T("@restart"),    NULL,       CA_NO_GUEST|CA_NO_SLAVE, 0, CS_NO_ARGS, 0, do_restart},
#if defined(HAVE_WORKING_FORK)
    {T("@startslave"), NULL,       CA_WIZARD,   0,          CS_NO_ARGS, 0, boot_slave},
#endif // HAVE_WORKING_FORK
    {T("@timecheck"),  timecheck_sw, CA_WIZARD, 0,          CS_NO_ARGS, 0, do_timecheck},
    {T("clearcom"),    NULL,       CA_NO_SLAVE, 0,          CS_NO_ARGS, 0, do_clearcom},
    {T("info"),        NULL,       CA_PUBLIC,   CMD_INFO,   CS_NO_ARGS, 0, logged_out0},
    {T("inventory"),   NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_inventory},
    {T("leave"),       leave_sw,   CA_LOCATION, 0,          CS_NO_ARGS, 0, do_leave},
    {T("logout"),      NULL,       CA_PUBLIC,   CMD_LOGOUT, CS_NO_ARGS, 0, logged_out0},
    {T("quit"),        NULL,       CA_PUBLIC,   CMD_QUIT,   CS_NO_ARGS, 0, logged_out0},
    {T("report"),      NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_report},
    {T("score"),       NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_score},
    {T("version"),     NULL,       CA_PUBLIC,   0,          CS_NO_ARGS, 0, do_version},
    {(UTF8 *)NULL,          NULL,       0,           0,          0,          0, NULL}
};

static CMDENT_ONE_ARG command_table_one_arg[] =
{
    {T("@apply_marked"), NULL,       CA_WIZARD|CA_GBL_INTERP,    0,  CS_ONE_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND,   0, do_apply_marked},
    {T("@boot"),         boot_sw,    CA_NO_GUEST|CA_NO_SLAVE,    0,  CS_ONE_ARG|CS_INTERP, 0, do_boot},
    {T("@ccreate"),      NULL,       CA_NO_SLAVE|CA_NO_GUEST,    0,  CS_ONE_ARG,           0, do_createchannel},
    {T("@cdestroy"),     NULL,       CA_NO_SLAVE|CA_NO_GUEST,    0,  CS_ONE_ARG,           0, do_destroychannel},
    {T("@clist"),        clist_sw,   CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_chanlist},
    {T("@cut"),          NULL,       CA_WIZARD|CA_LOCATION,      0,  CS_ONE_ARG|CS_INTERP, 0, do_cut},
    {T("@cwho"),         NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_channelwho},
    {T("@destroy"),      destroy_sw, CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, DEST_ONE,   CS_ONE_ARG|CS_INTERP,   0, do_destroy},
    {T("@disable"),      NULL,       CA_WIZARD,       GLOB_DISABLE,  CS_ONE_ARG,           0, do_global},
    {T("@doing"),        doing_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_doing},
    {T("@emit"),         emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,  SAY_EMIT,   CS_ONE_ARG|CS_INTERP,   0, do_say},
    {T("@enable"),       NULL,       CA_WIZARD,        GLOB_ENABLE,  CS_ONE_ARG,           0, do_global},
    {T("@entrances"),    NULL,       CA_NO_GUEST,                0,  CS_ONE_ARG|CS_INTERP, 0, do_entrances},
    {T("@eval"),         NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG|CS_INTERP, 0, do_eval},
    {T("@find"),         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_find},
    {T("@halt"),         halt_sw,    CA_NO_SLAVE,                0,  CS_ONE_ARG|CS_INTERP, 0, do_halt},
    {T("@hook"),         hook_sw,    CA_GOD,                     0,  CS_ONE_ARG|CS_INTERP, 0, do_hook},
    {T("@kick"),         NULL,       CA_WIZARD,         QUEUE_KICK,  CS_ONE_ARG|CS_INTERP, 0, do_queue},
    {T("@last"),         NULL,       CA_NO_GUEST,                0,  CS_ONE_ARG|CS_INTERP, 0, do_last},
    {T("@list"),         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_list},
    {T("@list_file"),    NULL,       CA_WIZARD,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_list_file},
    {T("@listcommands"), NULL,       CA_GOD,                     0,  CS_ONE_ARG,           0, do_listcommands},
    {T("@listmotd"),     listmotd_sw,CA_PUBLIC,          MOTD_LIST,  CS_ONE_ARG,           0, do_motd},
    {T("@mark"),         mark_sw,    CA_WIZARD,          SRCH_MARK,  CS_ONE_ARG|CS_NOINTERP,   0, do_search},
    {T("@motd"),         motd_sw,    CA_WIZARD,                  0,  CS_ONE_ARG,           0, do_motd},
    {T("@nemit"),        emit_sw,    CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE, SAY_EMIT, CS_ONE_ARG|CS_UNPARSE|CS_NOSQUISH, 0, do_say},
    {T("@poor"),         NULL,       CA_GOD,                     0,  CS_ONE_ARG|CS_INTERP, 0, do_poor},
    {T("@ps"),           ps_sw,      CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_ps},
    {T("@quitprogram"),  NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_quitprog},
    {T("@search"),       NULL,       CA_PUBLIC,        SRCH_SEARCH,  CS_ONE_ARG|CS_NOINTERP,   0, do_search},
    {T("@shutdown"),     NULL,       CA_NO_GUEST|CA_NO_SLAVE,    0,  CS_ONE_ARG,           0, do_shutdown},
    {T("@stats"),        stats_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_stats},
    {T("@sweep"),        sweep_sw,   CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_sweep},
    {T("@timewarp"),     warp_sw,    CA_WIZARD,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_timewarp},
    {T("@unlink"),       NULL,       CA_NO_SLAVE|CA_GBL_BUILD,   0,  CS_ONE_ARG|CS_INTERP, 0, do_unlink},
    {T("@unlock"),       lock_sw,    CA_NO_SLAVE,                0,  CS_ONE_ARG|CS_INTERP, 0, do_unlock},
    {T("@wall"),         wall_sw,    CA_ANNOUNCE,     SHOUT_DEFAULT, CS_ONE_ARG|CS_INTERP, 0, do_shout},
    {T("@wipe"),         NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_ONE_ARG|CS_INTERP,   0, do_wipe},
    {T("allcom"),        NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_allcom},
    {T("comlist"),       NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_comlist},
    {T("delcom"),        NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_delcom},
    {T("doing"),         NULL,       CA_PUBLIC,          CMD_DOING,  CS_ONE_ARG,           0, logged_out1},
    {T("drop"),          drop_sw,    CA_NO_SLAVE|CA_CONTENTS|CA_LOCATION|CA_NO_GUEST,  0,  CS_ONE_ARG|CS_INTERP,   0, do_drop},
    {T("enter"),         enter_sw,   CA_LOCATION,                0,  CS_ONE_ARG|CS_INTERP, 0, do_enter},
    {T("examine"),       examine_sw, CA_PUBLIC,                  0,  CS_ONE_ARG|CS_INTERP, 0, do_examine},
    {T("get"),           get_sw,     CA_LOCATION|CA_NO_GUEST,    0,  CS_ONE_ARG|CS_INTERP, 0, do_get},
#if defined(FIRANMUX)
    {T("goto"),          goto_sw,    CA_LOCATION|CA_NO_IMMOBILE, 0,  CS_ONE_ARG|CS_INTERP, 0, do_move},
#else
    {T("goto"),          goto_sw,    CA_LOCATION,                0,  CS_ONE_ARG|CS_INTERP, 0, do_move},
#endif // FIRANMUX
    {T("look"),          look_sw,    CA_LOCATION,        LOOK_LOOK,  CS_ONE_ARG|CS_INTERP, 0, do_look},
    {T("outputprefix"),  NULL,       CA_PUBLIC,         CMD_PREFIX,  CS_ONE_ARG,           0, logged_out1},
    {T("outputsuffix"),  NULL,       CA_PUBLIC,         CMD_SUFFIX,  CS_ONE_ARG,           0, logged_out1},
    {T("pose"),          pose_sw,    CA_LOCATION|CA_NO_SLAVE,  SAY_POSE,   CS_ONE_ARG|CS_INTERP,   0, do_say},
    {T("puebloclient"),  NULL,       CA_PUBLIC,   CMD_PUEBLOCLIENT,  CS_ONE_ARG,           0, logged_out1},
    {T("say"),           say_sw,     CA_LOCATION|CA_NO_SLAVE,  SAY_SAY,    CS_ONE_ARG|CS_INTERP,   0, do_say},
    {T("session"),       NULL,       CA_PUBLIC,        CMD_SESSION,  CS_ONE_ARG,           0, logged_out1},
    {T("think"),         NULL,       CA_NO_SLAVE,                0,  CS_ONE_ARG,           0, do_think},
    {T("train"),         NULL,       CA_PUBLIC,                  0,  CS_ONE_ARG,           0, do_train},
    {T("use"),           NULL,       CA_NO_SLAVE|CA_GBL_INTERP,  0,  CS_ONE_ARG|CS_INTERP, 0, do_use},
    {T("who"),           NULL,       CA_PUBLIC,            CMD_WHO,  CS_ONE_ARG,           0, logged_out1},
    {T("\\"),            NULL,       CA_NO_GUEST|CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN,   0, do_say},
    {T(":"),             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {T(";"),             NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {T("\""),            NULL,       CA_LOCATION|CF_DARK|CA_NO_SLAVE,  SAY_PREFIX, CS_ONE_ARG|CS_INTERP|CS_LEADIN, 0, do_say},
    {T("-"),             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,  0,  CS_ONE_ARG|CS_LEADIN,   0, do_postpend},
    {T("~"),             NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,  0,  CS_ONE_ARG|CS_LEADIN,   0, do_prepend},
    {T("#"),             NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CF_DARK, 0, CS_ONE_ARG|CS_INTERP|CS_CMDARG, 0, do_force_prefixed},
    {(UTF8 *)NULL,       NULL,       0,                          0,    0,                  0, NULL}
};

static CMDENT_TWO_ARG command_table_two_arg[] =
{
    {T("@addcommand"),  NULL,       CA_GOD,                                           0,           CS_TWO_ARG,           0, do_addcommand},
    {T("@admin"),       NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_admin},
    {T("@alias"),       NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          0,           CS_TWO_ARG,           0, do_alias},
    {T("@assert"),      break_sw,   CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_assert},
    {T("@attribute"),   attrib_sw,  CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_attribute},
    {T("@break"),       break_sw,   CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_break},
    {T("@cboot"),       cboot_sw,   CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_chboot},
    {T("@ccharge"),     NULL,       CA_NO_SLAVE|CA_NO_GUEST,       EDIT_CHANNEL_CCHARGE,           CS_TWO_ARG,           0, do_editchannel},
    {T("@cchown"),      NULL,       CA_NO_SLAVE|CA_NO_GUEST,        EDIT_CHANNEL_CCHOWN,           CS_TWO_ARG,           0, do_editchannel},
    {T("@cemit"),       cemit_sw,   CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_cemit},
    {T("@chown"),       chown_sw,   CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,             CHOWN_ONE,   CS_TWO_ARG|CS_INTERP, 0, do_chown},
    {T("@chownall"),    chown_sw,   CA_WIZARD|CA_GBL_BUILD,                           CHOWN_ALL,   CS_TWO_ARG|CS_INTERP, 0, do_chownall},
    {T("@chzone"),      NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD,             0,           CS_TWO_ARG|CS_INTERP, 0, do_chzone},
    {T("@clone"),       clone_sw,   CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST, 0,           CS_TWO_ARG|CS_INTERP, 0, do_clone},
    {T("@coflags"),     NULL,       CA_NO_SLAVE,                   EDIT_CHANNEL_COFLAGS,           CS_TWO_ARG,           0, do_editchannel},
    {T("@cpflags"),     NULL,       CA_NO_SLAVE,                   EDIT_CHANNEL_CPFLAGS,           CS_TWO_ARG,           0, do_editchannel},
    {T("@create"),      NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_CONTENTS|CA_NO_GUEST, 0,           CS_TWO_ARG|CS_INTERP, 0, do_create},
    {T("@cset"),        cset_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_chopen},
    {T("@decompile"),   decomp_sw,  CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_decomp},
    {T("@delcommand"),  NULL,       CA_GOD,                                           0,           CS_TWO_ARG,           0, do_delcommand},
    {T("@dolist"),      dolist_sw,  CA_GBL_INTERP,                                    0,           CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_dolist},
    {T("@drain"),       NULL,       CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,            NFY_DRAIN,   CS_TWO_ARG,           0, do_notify},
    {T("@email"),       NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG,           0, do_plusemail},
    {T("@femit"),       femit_sw,   CA_LOCATION|CA_NO_GUEST|CA_NO_SLAVE,              PEMIT_FEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("@fixdb"),       fixdb_sw,   CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_fixdb},
    {T("@flag"),        flag_sw,    CA_GOD,                                           0,           CS_TWO_ARG,           0, do_flag},
    {T("@folder"),      folder_sw,  CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_folder},
    {T("@force"),       NULL,       CA_NO_SLAVE|CA_GBL_INTERP|CA_NO_GUEST,            0,           CS_TWO_ARG|CS_INTERP|CS_CMDARG, 0, do_force},
    {T("@forwardlist"), NULL,       CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG,           0, do_forwardlist},
    {T("@fpose"),       fpose_sw,   CA_LOCATION|CA_NO_SLAVE,                          PEMIT_FPOSE, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("@fsay"),        NULL,       CA_LOCATION|CA_NO_SLAVE,                          PEMIT_FSAY,  CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("@function"),    function_sw,CA_GOD,                                           0,           CS_TWO_ARG|CS_INTERP, 0, do_function},
    {T("@link"),        NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG|CS_INTERP, 0, do_link},
    {T("@lock"),        lock_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_lock},
    {T("@log"),         NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG,           0, do_log},
    {T("@mail"),        mail_sw,    CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_mail},
    {T("@malias"),      malias_sw,  CA_NO_SLAVE|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_malias},
    {T("@moniker"),     NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_moniker},
    {T("@name"),        NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG|CS_INTERP, 0, do_name},
    {T("@newpassword"), NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG,           0, do_newpassword},
    {T("@notify"),      notify_sw,  CA_GBL_INTERP|CA_NO_SLAVE|CA_NO_GUEST,            0,           CS_TWO_ARG,           0, do_notify},
    {T("@npemit"),      pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_PEMIT, CS_TWO_ARG|CS_UNPARSE|CS_NOSQUISH, 0, do_pemit},
    {T("@oemit"),       NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_OEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("@parent"),      NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG,           0, do_parent},
    {T("@password"),    NULL,       CA_NO_GUEST,                                      0,           CS_TWO_ARG,           0, do_password},
    {T("@pcreate"),     NULL,       CA_WIZARD|CA_GBL_BUILD,                           PCRE_PLAYER, CS_TWO_ARG,           0, do_pcreate},
    {T("@pemit"),       pemit_sw,   CA_NO_GUEST|CA_NO_SLAVE,                          PEMIT_PEMIT, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("@power"),       NULL,       CA_PUBLIC,                                        0,           CS_TWO_ARG,           0, do_power},
    {T("@program"),     NULL,       CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_prog},
#if defined(TINYMUX_MODULES)
    {T("@query"),       query_sw,   CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP|CS_CMDARG, 0, do_query},
#endif
    {T("@quota"),       quota_sw,   CA_PUBLIC,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_quota},
    {T("@reference"),   reference_sw, CA_PUBLIC,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_reference},
    {T("@robot"),       NULL,       CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST|CA_PLAYER,   PCRE_ROBOT,  CS_TWO_ARG,           0, do_pcreate},
#ifdef REALITY_LVLS
    {T("@rxlevel"),     NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_rxlevel},
#endif
    {T("@set"),         set_sw,     CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST,             0,           CS_TWO_ARG,           0, do_set},
    {T("@teleport"),    teleport_sw,CA_NO_GUEST,                                      TELEPORT_DEFAULT, CS_TWO_ARG|CS_INTERP, 0, do_teleport},
#ifdef REALITY_LVLS
    {T("@txlevel"),     NULL,       CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_txlevel},
#endif
    {T("@toad"),        toad_sw,    CA_WIZARD,                                        0,           CS_TWO_ARG|CS_INTERP, 0, do_toad},
    {T("@wait"),        wait_sw,    CA_GBL_INTERP,                                    0,           CS_TWO_ARG|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_wait},
    {T("addcom"),       NULL,       CA_NO_SLAVE,                                      0,           CS_TWO_ARG,           0, do_addcom},
    {T("comtitle"),     comtitle_sw,CA_NO_SLAVE,                                      0,           CS_TWO_ARG,           0, do_comtitle},
    {T("give"),         give_sw,    CA_LOCATION|CA_NO_GUEST,                          0,           CS_TWO_ARG|CS_INTERP, 0, do_give},
    {T("kill"),         NULL,       CA_NO_GUEST|CA_NO_SLAVE,                          KILL_KILL,   CS_TWO_ARG|CS_INTERP, 0, do_kill},
    {T("page"),         page_sw,    CA_NO_SLAVE,                                      0,           CS_TWO_ARG|CS_INTERP, 0, do_page},
    {T("slay"),         NULL,       CA_WIZARD,                                        KILL_SLAY,   CS_TWO_ARG|CS_INTERP, 0, do_kill},
    {T("whisper"),      NULL,       CA_LOCATION|CA_NO_SLAVE,                          PEMIT_WHISPER, CS_TWO_ARG|CS_INTERP, 0, do_pemit},
    {T("&"),            NULL,       CA_NO_GUEST|CA_NO_SLAVE|CF_DARK,                  0,           CS_TWO_ARG|CS_LEADIN, 0, do_setvattr},
    {(UTF8 *)NULL,      NULL,       0,                                                0,           0,                    0, NULL}
};

static CMDENT_TWO_ARG_ARGV command_table_two_arg_argv[] =
{
    {T("@cpattr"),     NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV,             0, do_cpattr},
    {T("@dig"),        dig_sw,     CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_dig},
    {T("@edit"),       NULL,       CA_NO_SLAVE|CA_NO_GUEST,              0,  CS_TWO_ARG|CS_ARGV|CS_STRIP_AROUND, 0, do_edit},
    {T("@icmd"),       icmd_sw,    CA_GOD,                               0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_icmd},
    {T("@if"),         NULL,       CA_GBL_INTERP,                        0,  CS_TWO_ARG|CS_ARGV|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_if},
    {T("@mvattr"),     NULL,       CA_NO_SLAVE|CA_NO_GUEST|CA_GBL_BUILD, 0,  CS_TWO_ARG|CS_ARGV,             0, do_mvattr},
    {T("@open"),       open_sw,    CA_NO_SLAVE|CA_GBL_BUILD|CA_NO_GUEST, 0,  CS_TWO_ARG|CS_ARGV|CS_INTERP,   0, do_open},
    {T("@switch"),     switch_sw,  CA_GBL_INTERP,                        0,  CS_TWO_ARG|CS_ARGV|CS_CMDARG|CS_NOINTERP|CS_STRIP_AROUND, 0, do_switch},
    {T("@trigger"),    trig_sw,    CA_GBL_INTERP,                        0,  CS_TWO_ARG|CS_ARGV,             0, do_trigger},
    {T("@verb"),       verb_sw,    CA_GBL_INTERP|CA_NO_SLAVE,            0,  CS_TWO_ARG|CS_ARGV|CS_INTERP|CS_STRIP_AROUND, 0, do_verb},
    {(UTF8 *)NULL,     NULL,       0,                                    0,  0,                              0, NULL}
};

static CMDENT *goto_cmdp;

void commands_no_arg_add(CMDENT_NO_ARG cmdent[])
{
    CMDENT_NO_ARG *cp0a;
    for (cp0a = cmdent; cp0a->cmdname; cp0a++)
    {
        if (!hashfindLEN(cp0a->cmdname, strlen((char *)cp0a->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp0a->cmdname, strlen((char *)cp0a->cmdname), cp0a,
                       &mudstate.command_htab);
        }
    }
}

void commands_one_arg_add(CMDENT_ONE_ARG cmdent[])
{
    CMDENT_ONE_ARG *cp1a;
    for (cp1a = cmdent; cp1a->cmdname; cp1a++)
    {
        if (!hashfindLEN(cp1a->cmdname, strlen((char *)cp1a->cmdname),
                        &mudstate.command_htab))
        {
            hashaddLEN(cp1a->cmdname, strlen((char *)cp1a->cmdname), cp1a,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_add(CMDENT_TWO_ARG cmdent[])
{
    CMDENT_TWO_ARG *cp2a;
    for (cp2a = cmdent; cp2a->cmdname; cp2a++)
    {
        if (!hashfindLEN(cp2a->cmdname, strlen((char *)cp2a->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2a->cmdname, strlen((char *)cp2a->cmdname), cp2a,
                       &mudstate.command_htab);
        }
    }
}

void commands_two_arg_argv_add(CMDENT_TWO_ARG_ARGV cmdent[])
{
    CMDENT_TWO_ARG_ARGV *cp2aa;
    for (cp2aa = cmdent; cp2aa->cmdname; cp2aa++)
    {
        if (!hashfindLEN(cp2aa->cmdname, strlen((char *)cp2aa->cmdname),
                         &mudstate.command_htab))
        {
            hashaddLEN(cp2aa->cmdname, strlen((char *)cp2aa->cmdname), cp2aa,
                       &mudstate.command_htab);
        }
    }
}

void init_cmdtab(void)
{
    ATTR *ap;

    // Load attribute-setting commands.
    //
    for (ap = AttrTable; ap->name; ap++)
    {
        if (ap->flags & AF_NOCMD)
        {
            continue;
        }

        size_t nBuffer;
        bool bValid;
        UTF8 *cbuff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        CMDENT_TWO_ARG *cp2a = NULL;
        try
        {
            cp2a = new CMDENT_TWO_ARG;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL != cp2a)
        {
            cp2a->cmdname = StringClone(cbuff);
            cp2a->perms = CA_NO_GUEST | CA_NO_SLAVE;
            cp2a->switches = NULL;
            if (ap->flags & (AF_WIZARD | AF_MDARK))
            {
                cp2a->perms |= CA_WIZARD;
            }
            cp2a->extra = ap->number;
            cp2a->callseq = CS_TWO_ARG;
            cp2a->flags = CEF_ALLOC;
            cp2a->handler = do_setattr;
            hashaddLEN(cp2a->cmdname, nBuffer, cp2a, &mudstate.command_htab);
        }
        else
        {
            ISOUTOFMEMORY(cp2a);
        }
    }

    // Load the builtin commands
    //
    commands_no_arg_add(command_table_no_arg);
    commands_one_arg_add(command_table_one_arg);
    commands_two_arg_add(command_table_two_arg);
    commands_two_arg_argv_add(command_table_two_arg_argv);

    cache_prefix_cmds();

    goto_cmdp = (CMDENT *) hashfindLEN((char *)"goto", strlen("goto"), &mudstate.command_htab);
}

static CMDENT *g_prefix_cmds[256];

void clear_prefix_cmds()
{
    for (int i = 0; i < 256; i++)
    {
        g_prefix_cmds[i] = NULL;
    }
}

#ifdef SELFCHECK
void finish_cmdtab()
{
    clear_prefix_cmds();
    goto_cmdp = NULL;

    // First pass is to get rid of aliases.
    //
    CHashTable ht;
    CMDENT *cmdp;
    for (cmdp = (CMDENT *)hash_firstentry(&mudstate.command_htab);
         cmdp != NULL;
         cmdp = (CMDENT *)hash_nextentry(&mudstate.command_htab))
    {
        if (0 == (cmdp->flags & CEF_VISITED))
        {
            cmdp->flags |= CEF_VISITED;
            hashaddLEN(cmdp->cmdname, strlen((char *)cmdp->cmdname), cmdp, &ht);
        }
    }
    hashflush(&mudstate.command_htab);

    // Second pass is to free unique CMDENTs and related things.
    //
    for (cmdp = (CMDENT *)hash_firstentry(&ht);
         cmdp != NULL;
         cmdp = (CMDENT *)hash_nextentry(&ht))
    {
        if (cmdp->callseq & CS_ADDED)
        {
            ADDENT *nextp = cmdp->addent;
            while (NULL != nextp)
            {
                ADDENT *add = nextp;
                nextp = nextp->next;

                MEMFREE(add->name);
                add->name = NULL;
                MEMFREE(add);
                add = NULL;
            }
            cmdp->addent = NULL;
        }

        if (cmdp->flags & CEF_ALLOC)
        {
            MEMFREE(cmdp->cmdname);
            cmdp->cmdname = NULL;
            delete cmdp;
            cmdp = NULL;
        }
    }
}
#endif

/*! \brief Fills in the table of single-character prefix commands.
 *
 * Command entries for known prefix commands (<code>" : ; \ # & - ~</code>)
 * are copied from the regular command table. Entries for all other starting
 * characters are set to NULL.
 *
 * \return         None.
 */
void cache_prefix_cmds(void)
{
    clear_prefix_cmds();

#define SET_PREFIX_CMD(s) g_prefix_cmds[(unsigned char)(s)[0]] = \
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

inline bool is_prefix_cmd(const UTF8 *pCommand, size_t *pnPrefix, CMDENT **ppcmd)
{
    bool fReturn = false;
    size_t nPrefix = 0;
    CMDENT *pcmd = NULL;

    if (NULL != pCommand)
    {
        pcmd = g_prefix_cmds[(unsigned char)pCommand[0]];
        if (NULL != pcmd)
        {
            nPrefix = 1;
            fReturn = true;
        }
        else if (  0xE2 == pCommand[0]
                && 0x80 == pCommand[1]
                && 0x9C == pCommand[2])
        {
            // U+201C is a Unicode quote typically sent instead of ASCII double quote.
            //
            pcmd = g_prefix_cmds[(unsigned char)'"'];
            if (NULL != pcmd)
            {
                nPrefix = 3;
                fReturn = true;
            }
        }
    }

    if (NULL != pnPrefix)
    {
        *pnPrefix = nPrefix;
    }

    if (NULL != ppcmd)
    {
        *ppcmd = pcmd;
    }
    return fReturn;
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
#if defined(FIRANMUX)
           // || ((mask & CA_NO_IMMOBILE) && Immobile(player))
           || ((mask & CA_NO_RESTRICTED) && Restricted(player))
#endif // FIRANMUX
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
static UTF8 *hook_name(const UTF8 *pCommand, int key)
{
    const char *keylet;
    switch (key)
    {
    case CEF_HOOK_AFAIL:
        keylet = "AF";
        break;
    case CEF_HOOK_AFTER:
        keylet = "A";
        break;
    case CEF_HOOK_BEFORE:
        keylet = "B";
        break;
    case CEF_HOOK_IGNORE:
        keylet = "I";
        break;
    case CEF_HOOK_PERMIT:
        keylet = "P";
        break;
    case CEF_HOOK_ARGS:
        keylet = "R";
        break;
    default:
        return NULL;
    }

    const UTF8 *cmdName = pCommand;
    if (  pCommand[0]
       && !pCommand[1])
    {
        switch (pCommand[0])
        {
        case '"' : cmdName = T("say");    break;
        case ':' :
        case ';' : cmdName = T("pose");   break;
        case '\\': cmdName = T("@emit");  break;
        case '#' : cmdName = T("@force"); break;
        case '&' : cmdName = T("@set");   break;
        case '-' : cmdName = T("@mail");  break;
        case '~' : cmdName = T("@mail");  break;
        }
    }

    return tprintf(T("%s_%s"), keylet, cmdName);
}

static bool process_hook(dbref executor, CMDENT *cmdp, int key, bool save_flg)
{
    bool retval = true;
    ATTR *hk_attr = atr_str(hook_name(cmdp->cmdname, key));
    if (hk_attr)
    {
        dbref aowner;
        int aflags;
        UTF8 *atext = atr_get("process_hook.1060", mudconf.hook_obj,
                              hk_attr->number, &aowner, &aflags);
        if (atext[0] && !(aflags & AF_NOPROG))
        {
            reg_ref **preserve = NULL;
            if (save_flg)
            {
                preserve = PushRegisters(MAX_GLOBAL_REGS);
                save_global_regs(preserve);
            }
            UTF8 *buff, *bufc;
            bufc = buff = alloc_lbuf("process_hook");
            mux_exec(atext, LBUF_SIZE-1, buff, &bufc, mudconf.hook_obj, executor,
                     executor, AttrTrace(aflags, EV_FCHECK|EV_EVAL), NULL, 0);
            *bufc = '\0';
            if (save_flg)
            {
                restore_global_regs(preserve);
                PopRegisters(preserve, MAX_GLOBAL_REGS);
            }
            retval = xlate(buff);
            free_lbuf(buff);
        }
        free_lbuf(atext);
    }
    return retval;
}

void process_hook_args(dbref executor, CMDENT *cmdp, UTF8* arg1, UTF8* arg2, UTF8* args[], int nargs, UTF8* sw)
{
    ATTR *hk_attr = atr_str(hook_name(cmdp->cmdname, CEF_HOOK_ARGS));
    if (hk_attr)
    {
        dbref aowner;
        int aflags;
        UTF8 *atext = atr_get("process_hook_args.1093", mudconf.hook_obj,
                              hk_attr->number, &aowner, &aflags);
        if (atext[0] && !(aflags & AF_NOPROG))
        {
            reg_ref **preserve = NULL;
            preserve = PushRegisters(MAX_GLOBAL_REGS);
            save_global_regs(preserve);
            UTF8 *bufc = NULL;
            UTF8* inargs[5];
            if (arg1)
            {
                inargs[0] = alloc_lbuf("process_hook_args.1103");
                mux_strncpy(inargs[0], arg1, LBUF_SIZE-1);
            }
            else
            {
                inargs[0] = (UTF8*)"";
            }

            if (arg2)
            {
                inargs[1] = alloc_lbuf("process_hook_args.1110");
                mux_strncpy(inargs[1], arg2, LBUF_SIZE-1);
            }
            else
            {
                inargs[1] = (UTF8*)"";
            }

            inargs[2] = (UTF8*)"";
            inargs[4] = sw;

            if (arg1)
            {
                bufc = arg1;
                inargs[3] = (UTF8*)"0";
                mux_exec(atext, LBUF_SIZE-1, arg1, &bufc, mudconf.hook_obj, executor,
                         executor, AttrTrace(aflags, EV_FCHECK|EV_EVAL), (const UTF8**)inargs, 5);
            }

            if (arg2)
            {
                bufc = arg2;
                inargs[3] = (UTF8*)"1";
                mux_exec(atext, LBUF_SIZE-1, arg2, &bufc, mudconf.hook_obj, executor,
                         executor, AttrTrace(aflags, EV_FCHECK|EV_EVAL), (const UTF8**)inargs, 5);
            }

            if (nargs)
            {
                inargs[2] = alloc_lbuf("process_hook_args.1135");
                UTF8 qbuff[I64BUF_SIZE];
                inargs[3] = qbuff;
                for (int i = 0; i < nargs; i++)
                {
                    mux_i64toa(i+2, qbuff);
                    bufc = args[i];
                    mux_strncpy(inargs[2], args[i], LBUF_SIZE-1);
                    mux_exec(atext, LBUF_SIZE-1, args[i], &bufc, mudconf.hook_obj, executor,
                            executor, AttrTrace(aflags, EV_FCHECK|EV_EVAL), (const UTF8**)inargs, 5);
                }
                free_lbuf(inargs[2]);
            }

            if (arg2) free_lbuf(inargs[1]);
            if (arg1) free_lbuf(inargs[0]);

            if (NULL != bufc)
            {
                *bufc = '\0';
            }
            restore_global_regs(preserve);
            PopRegisters(preserve, MAX_GLOBAL_REGS);
        }
        free_lbuf(atext);
    }
}


/* ---------------------------------------------------------------------------
 * process_cmdent: Perform indicated command with passed args.
 */

static void process_cmdent(CMDENT *cmdp, UTF8 *switchp, dbref executor, dbref caller,
            dbref enactor, int eval, bool interactive, UTF8 *arg, UTF8 *unp_command,
            const UTF8 *cargs[], int ncargs)
{
    // Perform object type checks.
    //
    if (Invalid_Objtype(executor))
    {
        notify(executor, T("Command incompatible with executor type."));
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
        notify(executor, T("Sorry, building is not allowed now."));
        return;
    }
    if (Protect(CA_GBL_INTERP) && !(mudconf.control_flags & CF_INTERP))
    {
        notify(executor, T("Sorry, queueing and triggering are not allowed now."));
        return;
    }

    UTF8 *buf1, *buf2, tchar, *bp, *str, *buff, *j, *new0;
    UTF8 *args[MAX_ARG];
    UTF8* sw = switchp;
    int nargs, i, interp, key, xkey, aflags;
    dbref aowner;
    ADDENT *add;

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
            buf1 = (UTF8 *)strchr((char *)switchp, '/');
            if (buf1)
            {
                *buf1++ = '\0';
            }
            if (!search_nametab(executor, cmdp->switches, switchp, &xkey))
            {
                if (xkey == -1)
                {
                    notify(executor,
                       tprintf(T("Unrecognized switch \xE2\x80\x98%s\xE2\x80\x99 for command \xE2\x80\x98%s\xE2\x80\x99."),
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
                    notify(executor, T("Illegal combination of switches."));
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
        notify(executor, tprintf(T("Command %s does not take switches."),
            cmdp->cmdname));
        return;
    }

    bool bGoodHookObj = (Good_obj(mudconf.hook_obj) && !Going(mudconf.hook_obj));

    // 'Before' hooks.
    // @hook idea from TinyMUSH 3, code from RhostMUSH. Ported by Jake Nelson.
    //
    if (  (cmdp->flags & CEF_HOOK_BEFORE)
       && bGoodHookObj)
    {
        process_hook(executor, cmdp, CEF_HOOK_BEFORE, false);
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
        interp = 0;
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

    UTF8 *aargs[NUM_ENV_VARS];
    int nargs2;
    switch (cmdp->callseq & CS_NARG_MASK)
    {
    case CS_NO_ARGS: // <cmd>   (no args)
        (*(((CMDENT_NO_ARG *)cmdp)->handler))(executor, caller, enactor, eval, key);
        break;

    case CS_ONE_ARG:    // <cmd> <arg>

        // If an unparsed command, just give it to the handler
        //
#if 0
        // This never happens.
        //
        if (cmdp->callseq & CS_UNPARSE)
        {
            // Add CEF_HOOK_ARGS before uncommenting
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

            // Copy and skip first character for CS_LEADIN, to
            // prevent the \ prefix command from escaping part of
            // its argument.
            //
            if (  (cmdp->callseq & CS_LEADIN)
               && UTF8_SIZE1 == utf8_FirstByte[(unsigned char)str[0]])
            {
                UTF8 ch = *str++;
                *bp++ = ch;

                if (  mudconf.space_compress
                   && (  ';' == ch
                      || ':' == ch)
                   && str[0] == ' ')
                {
                    // Skip following space to prevent the parser from space
                    // compressing it.
                    //
                    *bp++ = *str++;
                }
            }
            mux_exec(str, LBUF_SIZE-1, buf1, &bp, executor, caller, enactor,
                eval|interp|EV_FCHECK|EV_TOP, cargs, ncargs);
            *bp = '\0';
        }
        else
        {
            buf1 = parse_to(&arg, '\0', interp | EV_TOP);
        }

        // Call the correct handler.
        //
        if (cmdp->callseq & CS_ADDED)
        {
            for (add = cmdp->addent; add != NULL; add = add->next)
            {
                buff = atr_get("process_cmdent.1347", add->thing, add->atr, &aowner, &aflags);

                // Attribute should contain at least two characters and first
                // character is '$'.
                //
                if (  AMATCH_CMD != buff[0]
                   || '\0' == buff[1])
                {
                    free_lbuf(buff);
                    break;
                }

                // Skip the '$' character and the next to allow "$::cmd".
                //
                size_t iBuff;
                for (iBuff = 2;  '\0' != buff[iBuff]
                              && ':'  != buff[iBuff]; iBuff++)
                {
                    ; // Nothing.
                }

                if ('\0' == buff[iBuff])
                {
                    free_lbuf(buff);
                    break;
                }
                buff[iBuff++] = '\0';

                if (!(cmdp->callseq & CS_LEADIN))
                {
                    for (j = unp_command; *j && (*j != ' '); j++)
                    {
                        ; // Nothing.
                    }
                }
                else
                {
                    for (j = unp_command; *j; j++)
                    {
                        ; // Nothing.
                    }
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
                    wait_que(add->thing, caller, executor,
                        AttrTrace(aflags, 0), false, lta, NOTHING, 0,
                        buff + iBuff,
                        NUM_ENV_VARS, (const UTF8 **)aargs,
                        mudstate.global_regs);

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
            if ((cmdp->flags & CEF_HOOK_ARGS) && bGoodHookObj)
            {
                process_hook_args(executor, cmdp, buf1, NULL, NULL, 0, sw);
            }
            (*(((CMDENT_ONE_ARG *)cmdp)->handler))(executor, caller,
                enactor, eval, key, buf1, cargs, ncargs);
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
        mux_exec(buf2, LBUF_SIZE-1, buf1, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL|EV_TOP, cargs, ncargs);
        *bp = '\0';

        if (cmdp->callseq & CS_ARGV)
        {
            // Arg2 is ARGV style.  Go get the args.
            //
            parse_arglist(executor, caller, enactor, arg,
                eval|interp|EV_STRIP_LS|EV_STRIP_TS, args, MAX_ARG, cargs,
                ncargs, &nargs);

            if ((cmdp->flags & CEF_HOOK_ARGS) && bGoodHookObj)
            {
                process_hook_args(executor, cmdp, buf1, NULL, args, nargs, sw);
            }
            (*(((CMDENT_TWO_ARG_ARGV *)cmdp)->handler))(executor, caller,
                enactor, eval, key, buf1, args, nargs, cargs, ncargs);

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
                mux_exec(arg, LBUF_SIZE-1, buf2, &bp, executor, caller, enactor,
                    eval|interp|EV_FCHECK|EV_TOP, cargs, ncargs);
                *bp = '\0';
            }
            else if (cmdp->callseq & CS_UNPARSE)
            {
                buf2 = parse_to(&arg, '\0', eval|interp|EV_TOP|EV_NO_COMPRESS);
            }
            else
            {
                buf2 = parse_to(&arg, '\0', eval|interp|EV_STRIP_LS|EV_STRIP_TS|EV_TOP);
            }

            if ((cmdp->flags & CEF_HOOK_ARGS) && bGoodHookObj)
            {
                process_hook_args(executor, cmdp, nargs2>=1?buf1:NULL, nargs2>=2?buf2:NULL, NULL, 0, sw);
            }
            (*(((CMDENT_TWO_ARG *)cmdp)->handler))(executor, caller,
                enactor, eval, key, nargs2, buf1, buf2, cargs, ncargs);

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
    if (  (cmdp->flags & CEF_HOOK_AFTER)
       && bGoodHookObj)
    {
        process_hook(executor, cmdp, CEF_HOOK_AFTER, false);
    }
}

static int cmdtest(dbref player, const UTF8 *cmd)
{
    UTF8 *buff1;
    const UTF8 *pt1, *pt2;
    dbref aowner;
    int aflags, rval;

    rval = 0;
    buff1 = atr_get("cmdtest.1573", player, A_CMDCHECK, &aowner, &aflags);
    pt1 = buff1;
    while (pt1 && *pt1)
    {
        pt2 = (const UTF8 *)strchr((const char *)pt1, ':');
        if (!pt2 || (pt2 == pt1))
            break;
        if (!strncmp((const char *)pt2+1, (const char *)cmd, strlen((const char *)cmd)))
        {
            if (*(pt2-1) == '1')
                rval = 1;
            else
                rval = 2;
            break;
        }
        pt1 = (const UTF8 *)strchr((const char *)pt2+1, ' ');
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

static int zonecmdtest(dbref player, const UTF8 *cmd)
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

// ---------------------------------------------------------------------------
// process_command: Execute a command.
//
UTF8 *process_command
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    bool  interactive,
    UTF8 *arg_command,
    const UTF8 *args[],
    int   nargs
)
{
    static UTF8 preserve_cmd[LBUF_SIZE];
    UTF8 *pOriginalCommand = arg_command;
    static UTF8 SpaceCompressCommand[LBUF_SIZE];
    static UTF8 LowerCaseCommand[LBUF_SIZE];

    // Robustify player.
    //
    const UTF8 *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = T("< process_command >");
    mudstate.nStackNest = 0;
    mudstate.bStackLimitReached = false;

    mux_assert(pOriginalCommand);

    if (!Good_obj(executor))
    {
        STARTLOG(LOG_BUGS, T("CMD"), T("PLYR"));
        log_printf(T("Bad player in process_command: %d"), executor);
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
            tprintf(T("Attempt to execute command by halted object #%d"), executor));
        mudstate.debug_cmd = cmdsave;
        return pOriginalCommand;
    }
    if (  Suspect(executor)
       && (mudconf.log_options & LOG_SUSPECTCMDS))
    {
        STARTLOG(LOG_SUSPECTCMDS, T("CMD"), T("SUSP"));
        log_name_and_loc(executor);
        log_text(T(" entered: "));
        log_text(pOriginalCommand);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALLCOMMANDS, T("CMD"), T("ALL"));
        log_name_and_loc(executor);
        log_text(T(" entered: "));
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
        notify(Owner(executor), tprintf(T("%s] %s"), Name(executor), pOriginalCommand));
    }

    // Eat leading whitespace, and space-compress if configured.
    //
    while (mux_isspace(*pOriginalCommand))
    {
        pOriginalCommand++;
    }
    mux_strncpy(preserve_cmd, pOriginalCommand, sizeof(preserve_cmd)-1);
    mudstate.debug_cmd = pOriginalCommand;
    mudstate.curr_cmd = preserve_cmd;

    UTF8 *pCommand;
    UTF8 *p, *q, *arg, *bp;
    int aflags;
    dbref exit, aowner;

    if (mudconf.space_compress)
    {
        // Compress out the spaces and use that as the command
        //
        pCommand = SpaceCompressCommand;

        p = pOriginalCommand;
        q = SpaceCompressCommand;
        while (  *p
              && q < SpaceCompressCommand + LBUF_SIZE)
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

            if (  *p
               && q < SpaceCompressCommand + LBUF_SIZE)
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
    int cval = 0;
    int hval = 0;
    bool bGoodHookObj = (Good_obj(mudconf.hook_obj) && !Going(mudconf.hook_obj));

    size_t nPrefix = 0;
    CMDENT *cmdp = NULL;
    if (is_prefix_cmd(pCommand, &nPrefix, &cmdp))
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore
        //
        UTF8 check2[UTF8_SIZE4+1];
        memcpy(check2, pCommand, nPrefix);
        check2[nPrefix] = '\0';
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, check2);
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), check2);
        }
        else
        {
            cval = 0;
        }

        if (cval == 0)
        {
            cval = zonecmdtest(executor, check2);
        }

        hval = 0;
        if (  (cmdp->flags & (CEF_HOOK_IGNORE|CEF_HOOK_PERMIT))
           && bGoodHookObj)
        {
            if (  (cmdp->flags & CEF_HOOK_IGNORE)
               && !process_hook(executor, cmdp, CEF_HOOK_IGNORE, true))
            {
                hval = 2;
            }
            else if (  (cmdp->flags & CEF_HOOK_PERMIT)
                    && !process_hook(executor, cmdp, CEF_HOOK_PERMIT, true))
            {
                hval = 1;
            }
        }

        if (  cval != 2
           && hval != 2)
        {
            if (  cval == 1
               || hval == 1)
            {
                if (  (cmdp->flags & CEF_HOOK_AFAIL)
                   && bGoodHookObj)
                {
                    process_hook(executor, cmdp, CEF_HOOK_AFAIL, false);
                }
                else
                {
                    notify(executor, NOPERM_MESSAGE);
                }
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }
            process_cmdent(cmdp, NULL, executor, caller, enactor,
                eval, interactive, pCommand, pCommand, args, nargs);
            if (mudstate.bStackLimitReached)
            {
                STARTLOG(LOG_ALWAYS, T("CMD"), T("SPAM"));
                log_name_and_loc(executor);
                log_text(T(" entered: "));
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
        mudstate.debug_cmd = cmdsave;
        return preserve_cmd;
    }

    // Check for the HOME command.
    //
    if (  Has_location(executor)
       && string_compare(pCommand, T("home")) == 0)
    {
        // CmdCheck tests for @icmd. higcheck tests for i/p hooks.
        // Both from RhostMUSH.
        // cval/hval values: 0 normal, 1 disable, 2 ignore.
        //
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, T("home"));
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), T("home"));
        }
        else
        {
            cval = 0;
        }

        if (cval == 0)
        {
            cval = zonecmdtest(executor, T("home"));
        }

        if (cval != 2)
        {
            if (!check_access(executor, mudconf.restrict_home))
            {
                notify(executor, NOPERM_MESSAGE);
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }
            if (cval == 1)
            {
                notify(executor, NOPERM_MESSAGE);
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }
            if (  (  Fixed(executor)
                  || Fixed(Owner(executor)))
               && !WizRoy(executor))
            {
                notify(executor, mudconf.fixed_home_msg);
                mudstate.debug_cmd = cmdsave;
                return preserve_cmd;
            }
            do_move(executor, caller, enactor, eval, 0, (UTF8 *)"home", NULL, 0);
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
        //
        if (CmdCheck(executor))
        {
            cval = cmdtest(executor, T("goto"));
        }
        else if (CmdCheck(Owner(executor)))
        {
            cval = cmdtest(Owner(executor), T("goto"));
        }
        else
        {
            cval = 0;
        }

        if (cval == 0)
        {
            cval = zonecmdtest(executor, T("goto"));
        }

        hval = 0;
        if (  (goto_cmdp->flags & (CEF_HOOK_IGNORE|CEF_HOOK_PERMIT))
           && bGoodHookObj)
        {
            if (  (goto_cmdp->flags & CEF_HOOK_IGNORE)
               && !process_hook(executor, goto_cmdp, CEF_HOOK_IGNORE, true))
            {
                hval = 2;
            }
            else if (  (goto_cmdp->flags & CEF_HOOK_PERMIT)
                    && !process_hook(executor, goto_cmdp, CEF_HOOK_PERMIT, true))
            {
                hval = 1;
            }
        }

        if (  cval != 2
           && hval != 2)
        {
            // Check for an exit name.
            //
            init_match_check_keys(executor, pCommand, TYPE_EXIT);
            match_exit_with_parents();
            exit = last_match_result();
            bool bMaster = (NOTHING == exit);

            if (bMaster)
            {
                // Check for an exit in the master room.
                //
                init_match_check_keys(executor, pCommand, TYPE_EXIT);
                match_master_exit();
                exit = last_match_result();
            }

            if (exit != NOTHING)
            {
                if (  1 == cval
                   || 1 == hval)
                {
                    if (  (goto_cmdp->flags & CEF_HOOK_AFAIL)
                       && bGoodHookObj)
                    {
                        process_hook(executor, goto_cmdp, CEF_HOOK_AFAIL, false);
                    }
                    else
                    {
                        notify(executor, NOPERM_MESSAGE);
                    }
                    mudstate.debug_cmd = cmdsave;
                    return preserve_cmd;
                }

                if (  (goto_cmdp->flags & CEF_HOOK_BEFORE)
                   && bGoodHookObj)
                {
                    process_hook(executor, goto_cmdp, CEF_HOOK_BEFORE, false);
                }

                if (!bMaster)
                {
                    move_exit(executor, exit, false, T("You can\xE2\x80\x99t go that way."), 0);
                }
                else
                {
                    move_exit(executor, exit, true, NULL, 0);
                }

                if (  (goto_cmdp->flags & CEF_HOOK_AFTER)
                    && bGoodHookObj)
                {
                    process_hook(executor, goto_cmdp, CEF_HOOK_AFTER, false);
                }
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
    size_t nLowerCaseCommand = 0, iPos = 0;
    while (  pCommand[iPos]
          && !mux_isspace(pCommand[iPos])
          && nLowerCaseCommand < LBUF_SIZE - 1)
    {
        LowerCaseCommand[nLowerCaseCommand] = mux_tolower_ascii(pCommand[iPos]);
        iPos++;
        nLowerCaseCommand++;
    }
    LowerCaseCommand[nLowerCaseCommand] = '\0';

    // Skip spaces before arg
    //
    while (mux_isspace(pCommand[iPos]))
    {
        iPos++;
    }

    // Remember where arg starts
    //
    arg = pCommand + iPos;

    // Strip off any command switches and save them.
    //
    UTF8 *pSlash = (UTF8 *)strchr((char *)LowerCaseCommand, '/');
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
        //
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

        hval = 0;
        if (  (cmdp->flags & (CEF_HOOK_IGNORE|CEF_HOOK_PERMIT))
           && bGoodHookObj)
        {
            if (  (cmdp->flags & CEF_HOOK_IGNORE)
               && !process_hook(executor, cmdp, CEF_HOOK_IGNORE, true))
            {
                hval = 2;
            }
            else if (  (cmdp->flags & CEF_HOOK_PERMIT)
                    && !process_hook(executor, cmdp, CEF_HOOK_PERMIT, true))
            {
                hval = 1;
            }
        }

        // If the command contains a switch, but the command doesn't support
        // any switches or the command contains one that isn't supported,
        // CEF_HOOK_IGSWITCH will allow us to treat the entire command as if it
        // weren't a built-in command.
        //
        int flagvalue;
        if (  (cmdp->flags & CEF_HOOK_IGSWITCH)
           && pSlash)
        {
            if (cmdp->switches)
            {
                search_nametab(executor, cmdp->switches, pSlash, &flagvalue);
                if (flagvalue & SW_MULTIPLE)
                {
                    MUX_STRTOK_STATE ttswitch;
                    // All the switches given a command shouldn't exceed 200 chars together
                    UTF8 switch_buff[200];
                    UTF8 *switch_ptr;
                    mux_strncpy(switch_buff, pSlash, sizeof(switch_buff)-1);
                    mux_strtok_src(&ttswitch, switch_buff);
                    mux_strtok_ctl(&ttswitch, T("/"));
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
                if (  (cmdp->flags & CEF_HOOK_AFAIL)
                   && bGoodHookObj)
                {
                    process_hook(executor, cmdp, CEF_HOOK_AFAIL, false);
                }
                else
                {
                    notify(executor, NOPERM_MESSAGE);
                }
                mudstate.debug_cmd = cmdsave;
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
            process_cmdent(cmdp, pSlash, executor, caller, enactor, eval,
                interactive, arg, pCommand, args, nargs);
            if (mudstate.bStackLimitReached)
            {
                STARTLOG(LOG_ALWAYS, T("CMD"), T("SPAM"));
                log_name_and_loc(executor);
                log_text(T(" entered: "));
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
    mux_exec(pCommand, LBUF_SIZE-1, LowerCaseCommand, &bp, executor, caller, enactor,
        eval|EV_EVAL|EV_FCHECK|EV_STRIP_CURLY|EV_TOP, args, nargs);
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
                //
                if (CmdCheck(executor))
                {
                    cval = cmdtest(executor, T("leave"));
                }
                else if (CmdCheck(Owner(executor)))
                {
                    cval = cmdtest(Owner(executor), T("leave"));
                }
                else
                {
                    cval = 0;
                }

                if (cval == 0)
                {
                    cval = zonecmdtest(executor, T("leave"));
                }

                cmdp = (CMDENT *)hashfindLEN("leave", strlen("leave"), &mudstate.command_htab);

                hval = 0;
                if (  (cmdp->flags & (CEF_HOOK_IGNORE|CEF_HOOK_PERMIT))
                   && bGoodHookObj)
                {
                    if (  (cmdp->flags & CEF_HOOK_IGNORE)
                       && !process_hook(executor, cmdp, CEF_HOOK_IGNORE, true))
                    {
                        hval = 2;
                    }
                    else if (  (cmdp->flags & CEF_HOOK_PERMIT)
                            && !process_hook(executor, cmdp, CEF_HOOK_PERMIT, true))
                    {
                        hval = 1;
                    }
                }

                if (  cval != 2
                   && hval != 2)
                {
                    if (  cval == 1
                       || hval == 1)
                    {
                        if (  (cmdp->flags & CEF_HOOK_AFAIL)
                           && bGoodHookObj)
                        {
                            process_hook(executor, cmdp, CEF_HOOK_AFAIL, false);
                        }
                        else
                        {
                            notify(executor, NOPERM_MESSAGE);
                        }
                        mudstate.debug_cmd = cmdsave;
                        return preserve_cmd;
                    }

                    if (  (cmdp->flags & CEF_HOOK_BEFORE)
                       && bGoodHookObj)
                    {
                        process_hook(executor, cmdp, CEF_HOOK_BEFORE, false);
                    }

                    do_leave(executor, caller, executor, 0, 0);

                    if (  (cmdp->flags & CEF_HOOK_AFTER)
                        && bGoodHookObj)
                    {
                        process_hook(executor, cmdp, CEF_HOOK_AFTER, false);
                    }

                    mudstate.debug_cmd = cmdsave;
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
                    //
                    if (CmdCheck(executor))
                    {
                        cval = cmdtest(executor, T("enter"));
                    }
                    else if (CmdCheck(Owner(executor)))
                    {
                        cval = cmdtest(Owner(executor), T("enter"));
                    }
                    else
                    {
                        cval = 0;
                    }

                    if (cval == 0)
                    {
                        cval = zonecmdtest(executor, T("enter"));
                    }

                    cmdp = (CMDENT *)hashfindLEN("enter", strlen("enter"), &mudstate.command_htab);

                    hval = 0;
                    if (  (cmdp->flags & (CEF_HOOK_IGNORE|CEF_HOOK_PERMIT))
                       && bGoodHookObj)
                    {
                        if (  (cmdp->flags & CEF_HOOK_IGNORE)
                           && !process_hook(executor, cmdp, CEF_HOOK_IGNORE, true))
                        {
                            hval = 2;
                        }
                        else if (  (cmdp->flags & CEF_HOOK_PERMIT)
                                && !process_hook(executor, cmdp, CEF_HOOK_PERMIT, true))
                        {
                            hval = 1;
                        }
                    }

                    if (  cval != 2
                       && hval != 2)
                    {
                        if (  cval == 1
                           || hval == 1)
                        {
                            if (  (cmdp->flags & CEF_HOOK_AFAIL)
                               && bGoodHookObj)
                            {
                                process_hook(executor, cmdp, CEF_HOOK_AFAIL, false);
                            }
                            else
                            {
                                notify(executor, NOPERM_MESSAGE);
                            }
                            mudstate.debug_cmd = cmdsave;
                            return preserve_cmd;
                        }

                        if (  (cmdp->flags & CEF_HOOK_BEFORE)
                           && bGoodHookObj)
                        {
                            process_hook(executor, cmdp, CEF_HOOK_BEFORE, false);
                        }

                        do_enter_internal(executor, exit, false);

                        if (  (cmdp->flags & CEF_HOOK_AFTER)
                            && bGoodHookObj)
                        {
                            process_hook(executor, cmdp, CEF_HOOK_AFTER, false);
                        }
                        mudstate.debug_cmd = cmdsave;
                        return preserve_cmd;
                    }
                    else if (cval == 1)
                    {
                        notify_quiet(executor, NOPERM_MESSAGE);
                        mudstate.debug_cmd = cmdsave;
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
            UTF8 *errtext = atr_get("process_command.2491", mudconf.global_error_obj, A_VA, &aowner, &aflags);
            UTF8 *errbuff = alloc_lbuf("process_command.error_msg");
            UTF8 *errbufc = errbuff;
            mux_exec(errtext, LBUF_SIZE-1, errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                AttrTrace(aflags, EV_EVAL|EV_FCHECK|EV_STRIP_CURLY|EV_TOP),
                (const UTF8 **)&pCommand, 1);
            *errbufc = '\0';
            notify(executor, errbuff);
            free_lbuf(errtext);
            free_lbuf(errbuff);
        }
        else
        {
            // We use LowerCaseCommand for another purpose.
            //
            notify(executor, T("Huh?  (Type \xE2\x80\x9Chelp\xE2\x80\x9D for help.)"));
            STARTLOG(LOG_BADCOMMANDS, "CMD", "BAD");
            log_name_and_loc(executor);
            log_text(T(" entered: "));
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
    UTF8 *buf = alloc_lbuf("list_cmdtable");
    UTF8 *bp = buf;
    ITL itl;
    ItemToList_Init(&itl, buf, &bp);
    ItemToList_AddString(&itl, T("Commands:"));

    for ( CMDENT_NO_ARG *cmdp0a = command_table_no_arg;
          cmdp0a->cmdname;
          cmdp0a++)
    {
        if (  check_access(player, cmdp0a->perms)
            && !(cmdp0a->perms & CF_DARK))
        {
            ItemToList_AddString(&itl, cmdp0a->cmdname);
        }
    }

    for ( CMDENT_ONE_ARG *cmdp1a = command_table_one_arg;
          cmdp1a->cmdname;
          cmdp1a++)
    {
        if (  check_access(player, cmdp1a->perms)
            && !(cmdp1a->perms & CF_DARK))
        {
            ItemToList_AddString(&itl, cmdp1a->cmdname);
        }
    }

    for ( CMDENT_TWO_ARG *cmdp2a = command_table_two_arg;
          cmdp2a->cmdname;
          cmdp2a++)
    {
        if (  check_access(player, cmdp2a->perms)
            && !(cmdp2a->perms & CF_DARK))
        {
            ItemToList_AddString(&itl, cmdp2a->cmdname);
        }
    }

    for ( CMDENT_TWO_ARG_ARGV *cmdp2av = command_table_two_arg_argv;
          cmdp2av->cmdname;
          cmdp2av++)
    {
        if (  check_access(player, cmdp2av->perms)
            && !(cmdp2av->perms & CF_DARK))
        {
            ItemToList_AddString(&itl, cmdp2av->cmdname);
        }
    }

    // Players get the list of logged-out cmds too
    //
    if (isPlayer(player))
    {
        for (NAMETAB *nt = logout_cmdtable; nt->name; nt++)
        {
            if (  God(player)
               || check_access(player, nt->perm))
            {
                ItemToList_AddString(&itl, nt->name);
            }
        }
    }
    ItemToList_Final(&itl);

    notify(player, buf);
    free_lbuf(buf);
}

// ---------------------------------------------------------------------------
// list_attrtable: List available attributes.
//
static void list_attrtable(dbref player)
{
    UTF8 *buf = alloc_lbuf("list_attrtable");
    UTF8 *bp = buf;
    ITL itl;
    ItemToList_Init(&itl, buf, &bp);
    ItemToList_AddString(&itl, T("Attributes:"));
    for (ATTR *ap = AttrTable; ap->name; ap++)
    {
        if (See_attr(player, player, ap))
        {
            ItemToList_AddString(&itl, ap->name);
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
    {T("builder"),               6, CA_WIZARD, CA_BUILDER},
#if !defined(FIRANMUX)
    {T("dark"),                  4, CA_GOD,    CF_DARK},
#endif // FIRANMUX
    {T("disabled"),              4, CA_GOD,    CA_DISABLED},
    {T("global_build"),          8, CA_PUBLIC, CA_GBL_BUILD},
    {T("global_interp"),         8, CA_PUBLIC, CA_GBL_INTERP},
    {T("god"),                   2, CA_GOD,    CA_GOD},
    {T("head"),                  2, CA_WIZARD, CA_HEAD},
    {T("immortal"),              3, CA_WIZARD, CA_IMMORTAL},
    {T("need_location"),         6, CA_PUBLIC, CA_LOCATION},
    {T("need_contents"),         6, CA_PUBLIC, CA_CONTENTS},
    {T("need_player"),           6, CA_PUBLIC, CA_PLAYER},
    {T("no_haven"),              4, CA_PUBLIC, CA_NO_HAVEN},
#if defined(FIRANMUX)
    {T("no_immobile"),           5, CA_WIZARD, CA_NO_IMMOBILE},
    {T("no_restricted"),         6, CA_WIZARD, CA_NO_RESTRICTED},
#endif // FIRANMUX
    {T("no_robot"),              4, CA_WIZARD, CA_NO_ROBOT},
    {T("no_slave"),              5, CA_PUBLIC, CA_NO_SLAVE},
    {T("no_suspect"),            5, CA_WIZARD, CA_NO_SUSPECT},
    {T("no_guest"),              5, CA_WIZARD, CA_NO_GUEST},
    {T("no_uninspected"),        5, CA_WIZARD, CA_NO_UNINS},
    {T("robot"),                 2, CA_WIZARD, CA_ROBOT},
    {T("staff"),                 4, CA_WIZARD, CA_STAFF},
    {T("static"),                4, CA_GOD,    CA_STATIC},
    {T("uninspected"),           5, CA_WIZARD, CA_UNINS},
    {T("wizard"),                3, CA_WIZARD, CA_WIZARD},
    {(UTF8 *)NULL,                    0, 0,         0}
};

static void list_cmdaccess(dbref player)
{
    for ( CMDENT_NO_ARG *cmdp0a = command_table_no_arg;
          cmdp0a->cmdname;
          cmdp0a++)
    {
        if (  check_access(player, cmdp0a->perms)
           && !(cmdp0a->perms & CF_DARK))
        {
            listset_nametab(player, access_nametab, cmdp0a->perms, cmdp0a->cmdname, true);
        }
    }

    for ( CMDENT_ONE_ARG *cmdp1a = command_table_one_arg;
          cmdp1a->cmdname;
          cmdp1a++)
    {
        if (  check_access(player, cmdp1a->perms)
           && !(cmdp1a->perms & CF_DARK))
        {
            listset_nametab(player, access_nametab, cmdp1a->perms, cmdp1a->cmdname, true);
        }
    }

    for ( CMDENT_TWO_ARG *cmdp2a = command_table_two_arg;
          cmdp2a->cmdname;
          cmdp2a++)
    {
        if (  check_access(player, cmdp2a->perms)
           && !(cmdp2a->perms & CF_DARK))
        {
            listset_nametab(player, access_nametab, cmdp2a->perms, cmdp2a->cmdname, true);
        }
    }

    for ( CMDENT_TWO_ARG_ARGV *cmdp2av = command_table_two_arg_argv;
          cmdp2av->cmdname;
          cmdp2av++)
    {
        if (  check_access(player, cmdp2av->perms)
           && !(cmdp2av->perms & CF_DARK))
        {
            listset_nametab(player, access_nametab, cmdp2av->perms, cmdp2av->cmdname, true);
        }
    }

    for (ATTR *ap = AttrTable; ap->name; ap++)
    {
        if (ap->flags & AF_NOCMD)
        {
            continue;
        }

        size_t nBuffer;
        bool bValid;
        UTF8 *buff2 = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
        if (!bValid)
        {
            continue;
        }

        CMDENT *cmdp = (CMDENT *)hashfindLEN(buff2, nBuffer, &mudstate.command_htab);
        if (  NULL != cmdp
           && check_access(player, cmdp->perms)
           && !(cmdp->perms & CF_DARK))
        {
            listset_nametab(player, access_nametab, cmdp->perms, cmdp->cmdname, true);
        }
    }
}

// ---------------------------------------------------------------------------
// list_cmdswitches: List switches for commands.
//
static void list_cmdswitches(dbref player)
{
    for ( CMDENT_NO_ARG *cmdp0a = command_table_no_arg;
          cmdp0a->cmdname;
          cmdp0a++)
    {
        if (  cmdp0a->switches
           && check_access(player, cmdp0a->perms)
           && !(cmdp0a->perms & CF_DARK))
        {
            display_nametab(player, cmdp0a->switches, cmdp0a->cmdname, false);
        }
    }

    for ( CMDENT_ONE_ARG *cmdp1a = command_table_one_arg;
          cmdp1a->cmdname;
          cmdp1a++)
    {
        if (  cmdp1a->switches
           && check_access(player, cmdp1a->perms)
           && !(cmdp1a->perms & CF_DARK))
        {
            display_nametab(player, cmdp1a->switches, cmdp1a->cmdname, false);
        }
    }

    for ( CMDENT_TWO_ARG *cmdp2a = command_table_two_arg;
          cmdp2a->cmdname;
          cmdp2a++)
    {
        if (  cmdp2a->switches
           && check_access(player, cmdp2a->perms)
           && !(cmdp2a->perms & CF_DARK))
        {
            display_nametab(player, cmdp2a->switches, cmdp2a->cmdname, false);
        }
    }

    for ( CMDENT_TWO_ARG_ARGV *cmdp2av = command_table_two_arg_argv;
          cmdp2av->cmdname;
          cmdp2av++)
    {
        if (  cmdp2av->switches
           && check_access(player, cmdp2av->perms)
           && !(cmdp2av->perms & CF_DARK))
        {
            display_nametab(player, cmdp2av->switches, cmdp2av->cmdname, false);
        }
    }
}

// ---------------------------------------------------------------------------
// list_attraccess: List access to attributes.
//
NAMETAB attraccess_nametab[] =
{
    {T("const"),       1,  CA_PUBLIC,  AF_CONST},
    {T("dark"),        2,  CA_WIZARD,  AF_DARK},
    {T("deleted"),     2,  CA_WIZARD,  AF_DELETED},
    {T("god"),         1,  CA_PUBLIC,  AF_GOD},
    {T("hidden"),      1,  CA_WIZARD,  AF_MDARK},
    {T("ignore"),      2,  CA_WIZARD,  AF_NOCMD},
    {T("internal"),    2,  CA_WIZARD,  AF_INTERNAL},
    {T("is_lock"),     4,  CA_PUBLIC,  AF_IS_LOCK},
    {T("locked"),      1,  CA_PUBLIC,  AF_LOCK},
    {T("no_command"),  4,  CA_PUBLIC,  AF_NOPROG},
    {T("no_inherit"),  4,  CA_PUBLIC,  AF_PRIVATE},
    {T("private"),     1,  CA_PUBLIC,  AF_ODARK},
    {T("regexp"),      1,  CA_PUBLIC,  AF_REGEXP},
    {T("visual"),      1,  CA_PUBLIC,  AF_VISUAL},
    {T("wizard"),      1,  CA_PUBLIC,  AF_WIZARD},
    {(UTF8 *) NULL,         0,          0,          0}
};

NAMETAB indiv_attraccess_nametab[] =
{
    {T("case"),                1,  CA_PUBLIC,  AF_CASE},
    {T("hidden"),              1,  CA_WIZARD,  AF_MDARK},
    {T("html"),                2,  CA_PUBLIC,  AF_HTML},
    {T("no_command"),          4,  CA_PUBLIC,  AF_NOPROG},
    {T("no_inherit"),          4,  CA_PUBLIC,  AF_PRIVATE},
    {T("no_name"),             4,  CA_PUBLIC,  AF_NONAME},
    {T("no_parse"),            4,  CA_PUBLIC,  AF_NOPARSE},
    {T("regexp"),              1,  CA_PUBLIC,  AF_REGEXP},
    {T("trace"),               1,  CA_PUBLIC,  AF_TRACE},
    {T("visual"),              1,  CA_PUBLIC,  AF_VISUAL},
    {T("wizard"),              1,  CA_WIZARD,  AF_WIZARD},
    {(UTF8 *) NULL,                 0,          0,          0}
};

static void list_attraccess(dbref player)
{
    for (ATTR *ap = AttrTable; ap->name; ap++)
    {
        if (bCanReadAttr(player, player, ap, false))
        {
            listset_nametab(player, attraccess_nametab, ap->flags, ap->name, true);
        }
    }
}

// ---------------------------------------------------------------------------
// cf_access: Change command or switch permissions.
//
CF_HAND(cf_access)
{
    UNUSED_PARAMETER(vp);

    CMDENT *cmdp;
    UTF8 *ap;
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

    cmdp = (CMDENT *)hashfindLEN(str, strlen((char *)str), &mudstate.command_htab);
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
        if (!mux_stricmp(str, T("home")))
        {
            return cf_modify_bits(&(mudconf.restrict_home), ap, pExtra,
                                  nExtra, player, cmd);
        }
        cf_log_notfound(player, cmd, T("Command"), str);
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

    for (ap = AttrTable; ap->name; ap++)
    {
        size_t nBuffer;
        bool bValid;
        UTF8 *buff = MakeCanonicalAttributeCommand(ap->name, &nBuffer, &bValid);
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
    ATTR *ap;
    UTF8 *sp;

    ATTRPERM **ppv = (ATTRPERM **)vp;

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

    ap = atr_str((UTF8 *)str);
    if (ap)
    {
        // This is a straight-out built-in attribute, so we'll just modify directly.
        //
        return cf_modify_bits(&(ap->flags), sp, pExtra, nExtra, player, cmd);
    }
    else
    {
        // This is either a wildcard or a vattr, so should be added to the table.
        //
        ATTRPERM *perm = NULL;
        try
        {
            perm = new ATTRPERM;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == perm)
        {
            cf_log_syntax(player, cmd, T("Out of memory."));
            return -1;
        }

        perm->wildcard = StringClone(str);
        perm->flags = 0;

        ATTRPERM *head = *ppv;

        // Add our permission to the list.
        //
        if (mudstate.bReadingConfiguration)
        {
            if (NULL == head)
            {
                *ppv = perm;
            }
            else
            {
                ATTRPERM *last;
                for (last = head; last->next; last = last->next)
                {
                    ; // Nothing.
                }
                last->next = perm;
            }
        }
        else
        {
            perm->next = head;
            *ppv = perm;
        }

        // Call the standard permission parser.
        //
        return cf_modify_bits(&(perm->flags), sp, pExtra, nExtra, player, cmd);
    }
}

// ---------------------------------------------------------------------------
// cf_cmd_alias: Add a command alias.
//
CF_HAND(cf_cmd_alias)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    UTF8 *ap;
    CMDENT *cmdp, *cmd2;
    NAMETAB *nt;

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *alias = mux_strtok_parse(&tts);
    UTF8 *orig = mux_strtok_parse(&tts);

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
        cmdp = (CMDENT *) hashfindLEN(orig, strlen((char *)orig), (CHashTable *) vp);
        if (cmdp == NULL || cmdp->switches == NULL)
        {
            cf_log_notfound(player, cmd, T("Command"), orig);
            return -1;
        }

        // Look up the switch
        //
        nt = find_nametab_ent(player, (NAMETAB *) cmdp->switches, ap);
        if (!nt)
        {
            cf_log_notfound(player, cmd, T("Switch"), ap);
            return -1;
        }

        if (!hashfindLEN(alias, strlen((char *)alias), (CHashTable *)vp))
        {
            // Create the new command table entry.
            //
            cmd2 = NULL;
            try
            {
                cmd2 = new CMDENT;
            }
            catch (...)
            {
                ; // Nothing.
            }
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
            cmd2->flags = CEF_ALLOC;

            hashaddLEN(cmd2->cmdname, strlen((char *)cmd2->cmdname), cmd2, (CHashTable *) vp);
        }
    }
    else
    {
        // A normal (non-switch) alias
        //
        void *hp = hashfindLEN(orig, strlen((char *)orig), (CHashTable *) vp);
        if (hp == NULL)
        {
            cf_log_notfound(player, cmd, T("Entry"), orig);
            return -1;
        }
        hashaddLEN(alias, strlen((char *)alias), hp, (CHashTable *) vp);
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
    UTF8 *playerb = decode_flags(player, &fs);

    fs = mudconf.room_flags;
    fs.word[FLAG_WORD1] |= TYPE_ROOM;
    UTF8 *roomb = decode_flags(player, &fs);

    fs = mudconf.exit_flags;
    fs.word[FLAG_WORD1] |= TYPE_EXIT;
    UTF8 *exitb = decode_flags(player, &fs);

    fs = mudconf.thing_flags;
    fs.word[FLAG_WORD1] |= TYPE_THING;
    UTF8 *thingb = decode_flags(player, &fs);

    fs = mudconf.robot_flags;
    fs.word[FLAG_WORD1] |= TYPE_PLAYER;
    UTF8 *robotb = decode_flags(player, &fs);

    UTF8 *buff = alloc_lbuf("list_df_flags");
    mux_sprintf(buff, LBUF_SIZE,
        T("Default flags: Players...%s Rooms...%s Exits...%s Things...%s Robots...%s"),
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
    UTF8 *buff = alloc_mbuf("list_costs");

    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, T(" and %d quota"), mudconf.room_quota);
    }
    notify(player,
           tprintf(T("Digging a room costs %d %s%s."),
               mudconf.digcost, coin_name(mudconf.digcost), buff));
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, T(" and %d quota"), mudconf.exit_quota);
    }
    notify(player,
           tprintf(T("Opening a new exit costs %d %s%s."),
               mudconf.opencost, coin_name(mudconf.opencost), buff));
    notify(player,
           tprintf(T("Linking an exit, home, or dropto costs %d %s."),
               mudconf.linkcost, coin_name(mudconf.linkcost)));
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, T(" and %d quota"), mudconf.thing_quota);
    }
    if (mudconf.createmin == mudconf.createmax)
    {
        raw_notify(player,
               tprintf(T("Creating a new thing costs %d %s%s."),
                   mudconf.createmin,
                   coin_name(mudconf.createmin), buff));
    }
    else
    {
        raw_notify(player,
        tprintf(T("Creating a new thing costs between %d and %d %s%s."),
            mudconf.createmin, mudconf.createmax,
            mudconf.many_coins, buff));
    }
    if (mudconf.quotas)
    {
        mux_sprintf(buff, MBUF_SIZE, T(" and %d quota"), mudconf.player_quota);
    }
    notify(player,
           tprintf(T("Creating a robot costs %d %s%s."),
               mudconf.robotcost, coin_name(mudconf.robotcost), buff));
    if (mudconf.killmin == mudconf.killmax)
    {
        int chance = 100;
        if (0 < mudconf.killguarantee)
        {
            chance = (mudconf.killmin * 100) / mudconf.killguarantee;
        }
        raw_notify(player, tprintf(T("Killing costs %d %s, with a %d%% chance of success."),
            mudconf.killmin, coin_name(mudconf.digcost), chance));
    }
    else
    {
        int cost_surething;
        raw_notify(player, tprintf(T("Killing costs between %d and %d %s."),
            mudconf.killmin, mudconf.killmax, mudconf.many_coins));
        if (0 < mudconf.killguarantee)
        {
            cost_surething = mudconf.killguarantee;
        }
        else
        {
            cost_surething = mudconf.killmin;
        }
        raw_notify(player, tprintf(T("You must spend %d %s to guarantee success."),
            cost_surething, coin_name(cost_surething)));
    }
    raw_notify(player,
           tprintf(T("Computationally expensive commands and functions (ie: @entrances, @find, @search, @stats (with an argument or switch), search(), and stats()) cost %d %s."),
            mudconf.searchcost, coin_name(mudconf.searchcost)));
    if (mudconf.machinecost > 0)
        raw_notify(player,
           tprintf(T("Each command run from the queue costs 1/%d %s."),
               mudconf.machinecost, mudconf.one_coin));
    if (mudconf.waitcost > 0)
    {
        raw_notify(player,
               tprintf(T("A %d %s deposit is charged for putting a command on the queue."),
                   mudconf.waitcost, mudconf.one_coin));
        raw_notify(player, T("The deposit is refunded when the command is run or canceled."));
    }
    if (mudconf.sacfactor == 0)
    {
        mux_ltoa(mudconf.sacadjust, buff);
    }
    else if (mudconf.sacfactor == 1)
    {
        if (mudconf.sacadjust < 0)
            mux_sprintf(buff, MBUF_SIZE, T("<create cost> - %d"), -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            mux_sprintf(buff, MBUF_SIZE, T("<create cost> + %d"), mudconf.sacadjust);
        else
            mux_sprintf(buff, MBUF_SIZE, T("<create cost>"));
    }
    else
    {
        if (mudconf.sacadjust < 0)
            mux_sprintf(buff, MBUF_SIZE, T("(<create cost> / %d) - %d"), mudconf.sacfactor, -mudconf.sacadjust);
        else if (mudconf.sacadjust > 0)
            mux_sprintf(buff, MBUF_SIZE, T("(<create cost> / %d) + %d"), mudconf.sacfactor, mudconf.sacadjust);
        else
            mux_sprintf(buff, MBUF_SIZE, T("<create cost> / %d"), mudconf.sacfactor);
    }
    raw_notify(player, tprintf(T("The value of an object is %s."), buff));
    if (mudconf.clone_copy_cost)
        raw_notify(player, T("The default value of cloned objects is the value of the original object."));
    else
        raw_notify(player, tprintf(T("The default value of cloned objects is %d %s."),
                mudconf.createmin, coin_name(mudconf.createmin)));

    free_mbuf(buff);
}

// ---------------------------------------------------------------------------
// list_options: List more game options from mudconf.
//
static const UTF8 *switchd[] =
{
    T("/first"),
    T("/all")
};

static const UTF8 *examd[] =
{
    T("/brief"),
    T("/full")
};

static const UTF8 *ed[] =
{
    T("Disabled"),
    T("Enabled")
};

static void list_options(dbref player)
{
    UTF8 *buff;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    if (mudconf.quotas)
        raw_notify(player, T("Building quotas are enforced."));
    if (mudconf.name_spaces)
        raw_notify(player, T("Player names may contain spaces."));
    else
        raw_notify(player, T("Player names may not contain spaces."));
    if (!mudconf.robot_speak)
        raw_notify(player, T("Robots are not allowed to speak in public areas."));
    if (mudconf.player_listen)
        raw_notify(player, T("The @Listen/@Ahear attribute set works on player objects."));
    if (mudconf.ex_flags)
    {
        // Using \230 instead \x98 because \x98c is making gcc choke.
        //
        raw_notify(player, T("The \xE2\x80\230examine\xE2\x80\x99 command lists the flag names for the object\xE2\x80\x99s flags."));
    }
    if (!mudconf.quiet_look)
        raw_notify(player, T("The \xE2\x80\x98look\xE2\x80\x99 command shows visible attributes in addition to the description."));
    if (mudconf.see_own_dark)
        raw_notify(player, T("The \xE2\x80\x98look\xE2\x80\x99 command lists DARK objects owned by you."));
    if (!mudconf.dark_sleepers)
        raw_notify(player, T("The \xE2\x80\x98look\xE2\x80\x99 command shows disconnected players."));
    if (mudconf.terse_look)
        raw_notify(player, T("The \xE2\x80\x98look\xE2\x80\x99 command obeys the TERSE flag."));
    if (mudconf.trace_topdown)
    {
        raw_notify(player, T("Trace output is presented top-down (whole expression first, then sub-exprs)."));
        raw_notify(player, tprintf(T("Only %d lines of trace output are displayed."), mudconf.trace_limit));
    }
    else
    {
        raw_notify(player, T("Trace output is presented bottom-up (subexpressions first)."));
    }
    if (!mudconf.quiet_whisper)
        raw_notify(player, T("The \xE2\x80\x98whisper\xE2\x80\x99 command lets others in the room with you know you whispered."));
    if (mudconf.pemit_players)
        raw_notify(player, T("The \xE2\x80\x98@pemit\xE2\x80\x99 command may be used to emit to faraway players."));
    if (!mudconf.terse_contents)
        raw_notify(player, T("The TERSE flag suppresses listing the contents of a location."));
    if (!mudconf.terse_exits)
        raw_notify(player, T("The TERSE flag suppresses listing obvious exits in a location."));
    if (!mudconf.terse_movemsg)
        raw_notify(player, T("The TERSE flag suppresses enter/leave/succ/drop messages generated by moving."));
    if (mudconf.pub_flags)
    {
        // Using \230 instead \x98 because \x98c is making gcc choke.
        //
        raw_notify(player, T("The \xE2\x80\230flags()\xE2\x80\x99 function will return the flags of any object."));
    }
    if (mudconf.read_rem_desc)
        raw_notify(player, T("The \xE2\x80\x98get()\xE2\x80\x99 function will return the description of faraway objects,"));
    if (mudconf.read_rem_name)
        raw_notify(player, T("The \xE2\x80\x98name()\xE2\x80\x99 function will return the name of faraway objects."));
    raw_notify(player, tprintf(T("The default switch for the \xE2\x80\x98@switch\xE2\x80\x99 command is %s."), switchd[mudconf.switch_df_all]));

    // Using \230 instead \x98 because \x98c is making gcc choke.
    //
    raw_notify(player, tprintf(T("The default switch for the \xE2\x80\230examine\xE2\x80\x99 command is %s."), examd[mudconf.exam_public]));
    if (mudconf.sweep_dark)
        raw_notify(player, T("Players may @sweep dark locations."));
    if (mudconf.fascist_tport)
        raw_notify(player, T("You may only @teleport out of locations that are JUMP_OK or that you control."));
    raw_notify(player,
           tprintf(T("Players may have at most %d commands in the queue at one time."),
               mudconf.queuemax));
    if (mudconf.match_mine)
    {
        if (mudconf.match_mine_pl)
            raw_notify(player, T("All objects search themselves for $-commands."));
        else
            raw_notify(player, T("Objects other than players search themselves for $-commands."));
    }
    if (!Wizard(player))
        return;
    buff = alloc_mbuf("list_options");

    raw_notify(player,
           tprintf(T("%d commands are run from the queue when there is no net activity."),
               mudconf.queue_chunk));
    raw_notify(player,
           tprintf(T("%d commands are run from the queue when there is net activity."),
               mudconf.active_q_chunk));
    if (mudconf.idle_wiz_dark)
        raw_notify(player, T("Wizards idle for longer than the default timeout are automatically set DARK."));
    if (mudconf.safe_unowned)
        raw_notify(player, T("Objects not owned by you are automatically considered SAFE."));
    if (mudconf.paranoid_alloc)
        raw_notify(player, T("The buffer pools are checked for consistency on each allocate or free."));
    if (mudconf.cache_names)
        raw_notify(player, T("A separate name cache is used."));
#if defined(HAVE_WORKING_FORK)
    if (mudconf.fork_dump)
    {
        raw_notify(player, T("Database dumps are performed by a fork()ed process."));
    }
#endif // HAVE_WORKING_FORK
    if (mudconf.max_players >= 0)
        raw_notify(player,
        tprintf(T("There may be at most %d players logged in at once."),
            mudconf.max_players));
    if (mudconf.quotas)
        mux_sprintf(buff, MBUF_SIZE, T(" and %d quota"), mudconf.start_quota);
    else
        *buff = '\0';
    raw_notify(player,
           tprintf(T("New players are given %d %s to start with."),
               mudconf.paystart, mudconf.many_coins));
    raw_notify(player,
           tprintf(T("Players are given %d %s each day they connect."),
               mudconf.paycheck, mudconf.many_coins));
    raw_notify(player,
      tprintf(T("Earning money is difficult if you have more than %d %s."),
          mudconf.paylimit, mudconf.many_coins));
    if (mudconf.payfind > 0)
        raw_notify(player,
               tprintf(T("Players have a 1 in %d chance of finding a %s each time they move."),
                   mudconf.payfind, mudconf.one_coin));
    raw_notify(player,
           tprintf(T("The head of the object freelist is #%d."),
               mudstate.freelist));

    mux_sprintf(buff, MBUF_SIZE, T("Intervals: Dump...%d  Clean...%d  Idlecheck...%d"),
        mudconf.dump_interval, mudconf.check_interval,
        mudconf.idle_interval);
    raw_notify(player, buff);

    CLinearTimeDelta ltdDump = mudstate.dump_counter - ltaNow;
    CLinearTimeDelta ltdCheck = mudstate.check_counter - ltaNow;
    CLinearTimeDelta ltdIdle = mudstate.idle_counter - ltaNow;

    long lDump  = ltdDump.ReturnSeconds();
    long lCheck = ltdCheck.ReturnSeconds();
    long lIdle  = ltdIdle.ReturnSeconds();
    mux_sprintf(buff, MBUF_SIZE, T("Timers: Dump...%ld  Clean...%ld  Idlecheck...%ld"),
        lDump, lCheck, lIdle);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, T("Timeouts: Idle...%d  Connect...%d  Tries...%d"),
        mudconf.idle_timeout, mudconf.conn_timeout,
        mudconf.retry_limit);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, T("Scheduling: Timeslice...%s  Max_Quota...%d  Increment...%d"),
        mudconf.timeslice.ReturnSecondsString(3), mudconf.cmd_quota_max,
        mudconf.cmd_quota_incr);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, T("Spaces...%s  Savefiles...%s"),
        ed[mudconf.space_compress], ed[mudconf.compress_db]);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, T("New characters: Room...#%d  Home...#%d  DefaultHome...#%d  Quota...%d"),
        mudconf.start_room, mudconf.start_home, mudconf.default_home,
        mudconf.start_quota);
    raw_notify(player, buff);

    mux_sprintf(buff, MBUF_SIZE, T("Misc: GuestChar...#%d  IdleQueueChunk...%d  ActiveQueueChunk...%d  Master_room...#%d"),
        mudconf.guest_char, mudconf.queue_chunk,
        mudconf.active_q_chunk, mudconf.master_room);
    raw_notify(player, buff);

    free_mbuf(buff);
}

// ---------------------------------------------------------------------------
// list_vattrs: List user-defined attributes
//
static void list_vattrs(dbref player, UTF8 *s_mask)
{
    bool wild_mtch =  s_mask
                   && s_mask[0] != '\0';

    UTF8 *buff = alloc_lbuf("list_vattrs");

    // If wild_match, then only list attributes that match wildcard(s)
    //
    UTF8 *p = tprintf(T("--- User-Defined Attributes %s---"),
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
            mux_sprintf(buff, LBUF_SIZE, T("%s(%d)"), va->name, va->number);
            listset_nametab(player, attraccess_nametab, va->flags, buff, true);
        }
    }

    if (wild_mtch)
    {
        p = tprintf(T("%d attributes matched, %d attributes total, next=%d"), wna,
            na, mudstate.attr_next);
    }
    else
    {
        p = tprintf(T("%d attributes, next=%d"), na, mudstate.attr_next);
    }
    raw_notify(player, p);
    free_lbuf(buff);
}

size_t LeftJustifyString(UTF8 *field, size_t nWidth, const UTF8 *value)
{
    size_t n = strlen((char *)value);
    if (n > nWidth)
    {
        n = nWidth;
    }
    memcpy(field, value, n);
    memset(field+n, ' ', nWidth-n);
    return nWidth;
}

size_t RightJustifyNumber(UTF8 *field, size_t nWidth, INT64 value, UTF8 chFill)
{
    UTF8   buffer[I64BUF_SIZE];
    size_t nReturn = 0;
    if (nWidth < sizeof(buffer))
    {
        size_t n = mux_i64toa(value, buffer);
        if (n < sizeof(buffer))
        {
            nReturn = n;
            if (n < nWidth)
            {
                memset(field, chFill, nWidth-n);
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
static void list_hashstat(dbref player, const UTF8 *tab_name, CHashTable *htab)
{
    unsigned int hashsize;
    int          entries, max_scan;
    INT64        deletes, scans, hits, checks;

    htab->GetStats(&hashsize, &entries, &deletes, &scans, &hits, &checks,
        &max_scan);

    UTF8 buff[MBUF_SIZE];
    UTF8 *p = buff;

    p += LeftJustifyString(p,  13, tab_name);      *p++ = ' ';
    p += RightJustifyNumber(p,  4, hashsize, ' '); *p++ = ' ';
    p += RightJustifyNumber(p,  6, entries,  ' '); *p++ = ' ';
    p += RightJustifyNumber(p,  7, deletes,  ' '); *p++ = ' ';
    p += RightJustifyNumber(p, 13, scans,    ' '); *p++ = ' ';
    p += RightJustifyNumber(p, 13, hits,     ' '); *p++ = ' ';
    p += RightJustifyNumber(p, 13, checks,   ' '); *p++ = ' ';
    p += RightJustifyNumber(p,  4, max_scan, ' '); *p = '\0';
    raw_notify(player, buff);
}

static void list_hashstats(dbref player)
{
    raw_notify(player, T("Hash Stats    Size    Num     Del       Lookups          Hits        Probes Long"));
    list_hashstat(player, T("Commands"), &mudstate.command_htab);
    list_hashstat(player, T("Logout Cmds"), &mudstate.logout_cmd_htab);
    list_hashstat(player, T("Functions"), &mudstate.func_htab);
    list_hashstat(player, T("Flags"), &mudstate.flags_htab);
    list_hashstat(player, T("Powers"), &mudstate.powers_htab);
    list_hashstat(player, T("Attr Names"), &mudstate.attr_name_htab);
    list_hashstat(player, T("Vattr Names"), &mudstate.vattr_name_htab);
    list_hashstat(player, T("Player Names"), &mudstate.player_htab);
    list_hashstat(player, T("Net Descr."), &mudstate.desc_htab);
    list_hashstat(player, T("Fwd. lists"), &mudstate.fwdlist_htab);
    list_hashstat(player, T("Excl. $-cmds"), &mudstate.parent_htab);
    list_hashstat(player, T("Mail Messages"), &mudstate.mail_htab);
    list_hashstat(player, T("Channel Names"), &mudstate.channel_htab);
#if !defined(MEMORY_BASED)
    list_hashstat(player, T("Attr. Cache"), &mudstate.acache_htab);
#endif // MEMORY_BASED
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
    raw_notify(player, T("Database is memory based."));
#else // MEMORY_BASED
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    CLinearTimeDelta ltd = lsaNow - cs_ltime;
    raw_notify(player, tprintf(T("DB Cache Stats   Writes       Reads  (over %d seconds)"), ltd.ReturnSeconds()));
    raw_notify(player, tprintf(T("Calls      %12d%12d"), cs_writes, cs_reads));
    raw_notify(player, tprintf(T("\nDeletes    %12d"), cs_dels));
    raw_notify(player, tprintf(T("Syncs      %12d"), cs_syncs));
    raw_notify(player, tprintf(T("I/O        %12d%12d"), cs_dbwrites, cs_dbreads));
    raw_notify(player, tprintf(T("Cache Hits %12d%12d"), cs_whits, cs_rhits));
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

#if defined(UNIX_NETWORKING)
#ifdef HAVE_GETDTABLESIZE
    int maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    int maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE
    int psize = getpagesize();
    raw_notify(player, tprintf(T("Process ID:  %10d        %10d bytes per page"), game_pid, psize));
#else // UNIX_NETWORKING
    raw_notify(player, tprintf(T("Process ID:  %10d"), game_pid));
#endif // UNIX_NETWORKING

#ifdef HAVE_GETRUSAGE
    raw_notify(player, tprintf(T("Time used:   %10d user   %10d sys"),
               usage.ru_utime.tv_sec, usage.ru_stime.tv_sec));
    raw_notify(player, tprintf(T("Resident mem:%10d shared %10d private%10d stack"),
           ixrss, idrss, isrss));
    raw_notify(player,
           tprintf(T("Integral mem:%10ld shared %10ld private%10ld stack"),
               usage.ru_ixrss, usage.ru_idrss, usage.ru_isrss));
    raw_notify(player,
           tprintf(T("Max res mem: %10ld pages  %10ld bytes"),
               usage.ru_maxrss, (usage.ru_maxrss * psize)));
    raw_notify(player,
           tprintf(T("Page faults: %10ld hard   %10ld soft   %10ld swapouts"),
               usage.ru_majflt, usage.ru_minflt, usage.ru_nswap));
    raw_notify(player,
           tprintf(T("Disk I/O:    %10ld reads  %10ld writes"),
               usage.ru_inblock, usage.ru_oublock));
    raw_notify(player,
           tprintf(T("Network I/O: %10ld in     %10ld out"),
               usage.ru_msgrcv, usage.ru_msgsnd));
    raw_notify(player,
           tprintf(T("Context swi: %10ld vol    %10ld forced %10ld sigs"),
               usage.ru_nvcsw, usage.ru_nivcsw, usage.ru_nsignals));
    raw_notify(player,
           tprintf(T("Descs avail: %10d"), maxfds));
#endif // HAVE_GETRUSAGE
}

//----------------------------------------------------------------------------
// list_modules
//
//
static void list_modules(dbref executor)
{
#if defined(TINYMUX_MODULES)
    raw_notify(executor, T("Modules:"));
    int i;
    for (i = 0; ; i++)
    {
        MUX_MODULE_INFO ModuleInfo;
        MUX_RESULT mr = mux_ModuleInfo(i, &ModuleInfo);
        if (  MUX_FAILED(mr)
           || MUX_S_FALSE == mr)
        {
            break;
        }

        raw_notify(executor, tprintf(T("%s (%s)"), ModuleInfo.pName, ModuleInfo.bLoaded ? T("loaded") : T("unloaded")));
    }

#ifdef STUB_SLAVE
    if (NULL != mudstate.pISlaveControl)
    {
        for (i = 0; ; i++)
        {
            MUX_MODULE_INFO ModuleInfo;
            MUX_RESULT mr = mudstate.pISlaveControl->ModuleInfo(i, &ModuleInfo);
            if (  MUX_FAILED(mr)
               || MUX_S_FALSE == mr)
            {
                break;
            }

            raw_notify(executor, tprintf(T("%s (%s) by stubslave"), ModuleInfo.pName, ModuleInfo.bLoaded ? T("loaded") : T("unloaded")));
        }
    }
#endif
#else
    raw_notify(executor, T("Modules not enabled."));
#endif
}

//----------------------------------------------------------------------------
// list_rlevels
//

#ifdef REALITY_LVLS
static void list_rlevels(dbref player)
{
    int i;
    raw_notify(player, T("Reality levels:"));
    for (i = 0; i < mudconf.no_levels; ++i)
    {
        raw_notify(player, tprintf(T("    Level: %-20.20s    Value: 0x%08x     Desc: %s"),
            mudconf.reality_level[i].name, mudconf.reality_level[i].value,
                mudconf.reality_level[i].attr));
    }
    raw_notify(player, T("--Completed."));
}
#endif // REALITY_LVLS

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
#define LIST_MODULES    25
#ifdef REALITY_LVLS
#define LIST_RLEVELS    26
#endif

NAMETAB list_names[] =
{
    {T("allocations"),        2,  CA_WIZARD,  LIST_ALLOCATOR},
    {T("attr_permissions"),   5,  CA_WIZARD,  LIST_ATTRPERMS},
    {T("attributes"),         2,  CA_PUBLIC,  LIST_ATTRIBUTES},
    {T("bad_names"),          2,  CA_WIZARD,  LIST_BADNAMES},
    {T("buffers"),            2,  CA_WIZARD,  LIST_BUFTRACE},
    {T("commands"),           3,  CA_PUBLIC,  LIST_COMMANDS},
    {T("config_permissions"), 3,  CA_GOD,     LIST_CONF_PERMS},
    {T("costs"),              3,  CA_PUBLIC,  LIST_COSTS},
    {T("db_stats"),           2,  CA_WIZARD,  LIST_DB_STATS},
    {T("default_flags"),      1,  CA_PUBLIC,  LIST_DF_FLAGS},
    {T("flags"),              2,  CA_PUBLIC,  LIST_FLAGS},
    {T("functions"),          2,  CA_PUBLIC,  LIST_FUNCTIONS},
    {T("globals"),            2,  CA_WIZARD,  LIST_GLOBALS},
    {T("hashstats"),          1,  CA_WIZARD,  LIST_HASHSTATS},
    {T("logging"),            1,  CA_GOD,     LIST_LOGGING},
    {T("modules"),            1,  CA_WIZARD,  LIST_MODULES},
    {T("options"),            1,  CA_PUBLIC,  LIST_OPTIONS},
    {T("permissions"),        2,  CA_WIZARD,  LIST_PERMS},
    {T("powers"),             2,  CA_WIZARD,  LIST_POWERS},
    {T("process"),            2,  CA_WIZARD,  LIST_PROCESS},
    {T("resources"),          1,  CA_WIZARD,  LIST_RESOURCES},
    {T("site_information"),   2,  CA_WIZARD,  LIST_SITEINFO},
    {T("switches"),           2,  CA_PUBLIC,  LIST_SWITCHES},
    {T("user_attributes"),    1,  CA_WIZARD,  LIST_VATTRS},
    {T("guests"),             2,  CA_WIZARD,  LIST_GUESTS},
#ifdef REALITY_LVLS
    {T("rlevels"),            3,  CA_PUBLIC,  LIST_RLEVELS},
#endif
    {(UTF8 *) NULL,                0,  0,          0}
};

void do_list(dbref executor, dbref caller, dbref enactor, int eval, int key,
    UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, arg);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *s_option = mux_strtok_parse(&tts);

    int flagvalue;
    if (!search_nametab(executor, list_names, arg, &flagvalue))
    {
        if (flagvalue == -1)
        {
            display_nametab(executor, list_names, T("Unknown option.  Use one of"), true);
        }
        else
        {
            notify(executor, NOPERM_MESSAGE);
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
                T("Global parameters:"), T("enabled"), T("disabled"));
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
                   T("Events Logged:"), T("enabled"), T("disabled"));
        interp_nametab(executor, logdata_nametab, mudconf.log_info,
                   T("Information Logged:"), T("yes"), T("no"));
        break;
    case LIST_DB_STATS:
        list_db_stats(executor);
        break;
    case LIST_PROCESS:
        list_process(executor);
        break;
    case LIST_BADNAMES:
        badname_list(executor, T("Disallowed names:"));
        break;
    case LIST_RESOURCES:
        list_system_resources(executor);
        break;
    case LIST_GUESTS:
        Guest.ListAll(executor);
        break;
    case LIST_MODULES:
        list_modules(executor);
        break;
#ifdef REALITY_LVLS
    case LIST_RLEVELS:
        list_rlevels(executor);
        break;
#endif
    }
}

void do_assert(dbref executor, dbref caller, dbref enactor, int eval, int key,
               int nargs, UTF8 *arg1, UTF8 *command, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);

    if (!xlate(arg1))
    {
        break_called = true;
        if (  NULL != command
           && '\0' != command[0])
        {
            if (key & BREAK_INLINE)
            {
                process_command(executor, caller, enactor, eval, false, command, cargs, ncargs);
            }
            else
            {
                CLinearTimeAbsolute lta;
                wait_que(executor, caller, enactor, eval, false, lta, NOTHING, 0,
                    command,
                    ncargs, cargs,
                    mudstate.global_regs);
            }
        }
    }
}

void do_break(dbref executor, dbref caller, dbref enactor, int eval, int key,
              int nargs, UTF8 *arg1, UTF8 *command, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);

    if (xlate(arg1))
    {
        break_called = true;
        if (  NULL != command
           && '\0' != command[0])
        {
            if (key & BREAK_INLINE)
            {
                process_command(executor, caller, enactor, eval, false, command, cargs, ncargs);
            }
            else
            {
                CLinearTimeAbsolute lta;
                wait_que(executor, caller, enactor, eval, false, lta, NOTHING, 0,
                    command,
                    ncargs, cargs,
                    mudstate.global_regs);
            }
        }
    }
}

// do_icmd: Ignore or disable commands on a per-player or per-room basis.
// Used with express permission of RhostMUSH developers.
// Bludgeoned into MUX by Jake Nelson 7/2002.
//
void do_icmd(dbref player, dbref cause, dbref enactor, int eval, int key,
             UTF8 *name, UTF8 *args[], int nargs, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cause);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CMDENT *cmdp;
    UTF8 *buff1, *pt1, *pt2, *pt3, *atrpt, *pt5;
    int x, aflags;
    size_t y;
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
            notify(player, T("@icmd: Bad Location."));
            return;
        }
        if (key == ICMD_CROOM)
        {
            atr_clr(target, A_CMDCHECK);
            notify(player, T("@icmd: Location - All cleared."));
            notify(player, T("@icmd: Done."));
            return;
        }
        else if (key == ICMD_LROOM)
        {
            atrpt = atr_get("do_icmd.4060", target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player, T("Location CmdCheck attribute is:"));
                notify(player, atrpt);
            }
            else
            {
                notify(player, T("Location CmdCheck attribute is empty."));
            }
            free_lbuf(atrpt);
            notify(player, T("@icmd: Done."));
            return;
        }
        else if (key == ICMD_LALLROOM)
        {
            target = Location(player);
            if (  !Good_obj(target)
               || Going(target)
               || isPlayer(target))
            {
                notify(player, T("@icmd: Bad Location."));
                return;
            }
            notify(player, T("Scanning all locations and zones from your current location:"));
            bFound = false;
            atrpt = atr_get("do_icmd.4086", target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player, tprintf(T("%c     --- At %s(#%d) :"),
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
                    atrpt = atr_get("do_icmd.4102", zone, A_CMDCHECK, &aowner, &aflags);
                    if (*atrpt)
                    {
                        notify(player, tprintf(T("%c     z-- At %s(#%d) :"),
                            '*', Name(zone), zone));
                        notify(player, atrpt);
                        bFound = true;
                    }
                    free_lbuf(atrpt);
                }
            }
            if (!bFound)
            {
                notify(player, T("@icmd: Location - No icmds found at current location."));
            }
            notify(player, T("@icmd: Done."));
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
            notify(player, T("@icmd: Bad player."));
            return;
        }
        if ((key == ICMD_OFF) || (key == ICMD_CLEAR))
        {
            s_Flags(target, FLAG_WORD3, Flags3(target) & ~CMDCHECK);
            if (key == ICMD_CLEAR)
            {
                atr_clr(target, A_CMDCHECK);
            }
            notify(player, T("@icmd: All cleared."));
            notify(player, T("@icmd: Done."));
            return;
        }
        else if (key == ICMD_ON)
        {
            s_Flags(target, FLAG_WORD3, Flags3(target) | CMDCHECK);
            notify(player, T("@icmd: Activated."));
            notify(player, T("@icmd: Done."));
            return;
        }
        else if (key == ICMD_CHECK)
        {
            if (CmdCheck(target))
            {
                notify(player, T("CmdCheck is active."));
            }
            else
            {
                notify(player, T("CmdCheck is not active."));
            }
            atrpt = atr_get("do_icmd.4166", target, A_CMDCHECK, &aowner, &aflags);
            if (*atrpt)
            {
                notify(player, T("CmdCheck attribute is:"));
                notify(player, atrpt);
            }
            else
            {
                notify(player, T("CmdCheck attribute is empty."));
            }
            free_lbuf(atrpt);
            notify(player, T("@icmd: Done."));
            return;
        }
    }
    else
    {
        key = loc_set;
    }

    const UTF8 *message = T("");
    buff1 = alloc_lbuf("do_icmd");
    for (x = 0; x < nargs; x++)
    {
        pt1 = args[x];
        pt2 = buff1;
        while (  *pt1
              && pt2 < buff1 + LBUF_SIZE)
        {
            *pt2++ = mux_tolower_ascii(*pt1++);
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
            if (!string_compare(pt1, T("home")))
            {
                bHome = true;
                cmdp = NULL;
            }
            else
            {
                bHome = false;
                cmdp = (CMDENT *) hashfindLEN(pt1, strlen((char *)pt1), &mudstate.command_htab);
            }
            if (cmdp || bHome)
            {
                atrpt = atr_get("do_icmd.4223", target, A_CMDCHECK, &aowner, &aflags);
                if (cmdp)
                {
                    aflags = (int)strlen((char *)cmdp->cmdname);
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
                            pt1 = (UTF8 *)strstr((char *)pt5, "::");
                            if (pt1)
                            {
                                pt1++;
                            }
                        }
                        else
                        {
                            pt1 = (UTF8 *)strstr((char *)pt5, (char *)cmdp->cmdname);
                        }
                    }
                    else
                    {
                        pt1 = (UTF8 *)strstr((char *)pt5, "home");
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
                        if (cmdp)
                        {
                            pt3 = tprintf(T(" %d:%s"), key + 1, cmdp->cmdname);
                        }
                        else
                        {
                            pt3 = tprintf(T(" %d:home"), key + 1);
                        }

                        size_t natrpt = strlen((char *)atrpt);
                        size_t npt3 = strlen((char *)pt3);
                        if ((natrpt + npt3) < LBUF_SIZE - 1)
                        {
                            mux_strncpy(atrpt + natrpt, pt3, LBUF_SIZE-natrpt-1);
                            atr_add_raw(target, A_CMDCHECK, atrpt);
                            if ( loc_set == -1 )
                            {
                                s_Flags(target, FLAG_WORD3, Flags3(target) | CMDCHECK);
                            }
                            message = T("Set");
                        }
                    }
                    else
                    {
                        message = T("Command already present");
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
                        // Save the char that'll be nulled by mux_strncpy.
                        // (Even though it may be unneeded by this point.)
                        UTF8 cSave = buff1[y];
                        mux_strncpy(buff1, atrpt, y);
                        buff1[y] = cSave;
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
                                size_t natrpt = strlen((char *)atrpt);
                                mux_strncpy(atrpt + natrpt, pt2, LBUF_SIZE-natrpt-1);
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
                            message = T("Cleared");
                        }
                        else
                        {
                            atr_add_raw(target, A_CMDCHECK, atrpt);
                            message = T("Cleared");
                        }
                    }
                    else
                    {
                        message = T("Command not present");
                    }
                }
                free_lbuf(atrpt);
            }
            else
            {
                message = T("Bad command");
            }
            notify(player, tprintf(T("@icmd:%s %s."), (loc_set == -1) ? T("") : T(" Location -"), message));
        }
    }
    free_lbuf(buff1);
    notify(player, T("@icmd: Done."));
}

// do_train: show someone else in the same room what code you're entering and the result
// From RhostMUSH, changed to use notify_all_from_inside.
//
void do_train(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *string, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (0 < mudstate.train_nest_lev)
    {
        notify(executor, T("Train cannot be used to teach command, train."));
        return;
    }
    mudstate.train_nest_lev++;
    dbref loc = Location(executor);
    if (!Good_obj(loc))
    {
        notify(executor, T("Bad location."));
        mudstate.train_nest_lev--;
        return;
    }
    if (  !string
       || !*string)
    {
        notify(executor, T("Train requires an argument."));
        mudstate.train_nest_lev--;
        return;
    }

    notify_all_from_inside(loc, executor, tprintf(T("%s types -=> %s"),
        Moniker(executor), string));
    process_command(executor, caller, enactor, eval, true, string, NULL, 0);
    mudstate.train_nest_lev--;
}

void do_moniker(dbref executor, dbref caller, dbref enactor, int eval, int key,
                 int nargs, UTF8 *name, UTF8 *instr, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing = match_thing(executor, name);
    if (!Good_obj(thing))
    {
        return;
    }

    if (!Controls(executor, thing))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    if (  instr == NULL
       || instr[0] == '\0')
    {
        notify_quiet(executor, T("Moniker cleared."));
        s_Moniker(thing, NULL);
    }
    else
    {
        s_Moniker(thing, instr);
        if (  !Quiet(executor)
           && !Quiet(thing))
        {
            notify_quiet(executor, T("Moniker set."));
        }
    }
    set_modified(thing);
}

// do_hook: run softcode before or after running a hardcode command, or
// softcode access. Original idea from TinyMUSH 3, code from RhostMUSH.
// Used with express permission of RhostMUSH developers.
// Bludgeoned into MUX by Jake Nelson 7/2002.
//
static void show_hook(UTF8 *bf, UTF8 *bfptr, int key)
{
    if (key & CEF_HOOK_BEFORE)
        safe_str(T("before "), bf, &bfptr);
    if (key & CEF_HOOK_AFTER)
        safe_str(T("after "), bf, &bfptr);
    if (key & CEF_HOOK_PERMIT)
        safe_str(T("permit "), bf, &bfptr);
    if (key & CEF_HOOK_IGNORE)
        safe_str(T("ignore "), bf, &bfptr);
    if (key & CEF_HOOK_IGSWITCH)
        safe_str(T("igswitch "), bf, &bfptr);
    if (key & CEF_HOOK_AFAIL)
        safe_str(T("afail "), bf, &bfptr);
    if (key & CEF_HOOK_ARGS)
        safe_str(T("args "), bf, &bfptr);
    *bfptr = '\0';
}

static void hook_loop(dbref executor, CMDENT *cmdp, UTF8 *s_ptr, UTF8 *s_ptrbuff)
{
    show_hook(s_ptrbuff, s_ptr, HOOKMASK(cmdp->flags));
    const UTF8 *pFmt = T("%-32.32s | %s");
    const UTF8 *pCmd = cmdp->cmdname;
    if (  pCmd[0] != '\0'
       && pCmd[1] == '\0')
    {
        switch (pCmd[0])
        {
        case '"':
            pFmt = T("S %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98\"\xE2\x80\x99 hook on \xE2\x80\x98say\xE2\x80\x99)");
            break;
        case ':':
            pFmt = T("P %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98:\xE2\x80\x99 hook on \xE2\x80\x98pose\xE2\x80\x99)");
            break;
        case ';':
            pFmt = T("P %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98;\xE2\x80\x99 hook on \xE2\x80\x98pose\xE2\x80\x99)");
            break;
        case '\\':
            pFmt = T("E %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98\\\\\xE2\x80\x99 hook on \xE2\x80\x98@emit\xE2\x80\x99)");
            break;
        case '#':
            pFmt = T("F %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98#\xE2\x80\x99 hook on \xE2\x80\x98@force\xE2\x80\x99)");
            break;
        case '&':
            pFmt = T("V %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98&\xE2\x80\x99 hook on \xE2\x80\x98@set\xE2\x80\x99)");
            break;
        case '-':
            pFmt = T("M %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98-\xE2\x80\x99 hook on \xE2\x80\x98@mail\xE2\x80\x99)");
            break;
        case '~':
            pFmt = T("M %-30.30s | %s");
            pCmd = T("(\xE2\x80\x98~\xE2\x80\x99 hook on \xE2\x80\x98@mail\xE2\x80\x99)");
            break;
        }
    }
    notify(executor, tprintf(pFmt, pCmd, s_ptrbuff));
}

void do_hook(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool negate, found;
    UTF8 *s_ptr, *s_ptrbuff, *cbuff, *p;
    const UTF8 *q;
    CMDENT *cmdp = (CMDENT *)NULL;

    if (  (  key
          && !(key & CEF_HOOK_LIST))
       || (  (  !key
             || (key & CEF_HOOK_LIST))
          && *name))
    {
        cmdp = (CMDENT *)hashfindLEN(name, strlen((char *)name), &mudstate.command_htab);
        if (!cmdp)
        {
            notify(executor, T("@hook: Non-existent command name given."));
            return;
        }
    }
    if (  (key & CEF_HOOK_CLEAR)
       && (key & CEF_HOOK_LIST))
    {
        notify(executor, T("@hook: Incompatible switches."));
        return;
    }

    if (key & CEF_HOOK_CLEAR)
    {
        negate = true;
        key = key & ~CEF_HOOK_CLEAR;
        key = key & ~SW_MULTIPLE;
    }
    else
    {
        negate = false;
    }

    if (key & (CEF_HOOK_BEFORE|CEF_HOOK_AFTER|CEF_HOOK_PERMIT|CEF_HOOK_IGNORE|CEF_HOOK_IGSWITCH|CEF_HOOK_AFAIL|CEF_HOOK_ARGS))
    {
        if (negate)
        {
            cmdp->flags = cmdp->flags & ~key;
        }
        else
        {
            cmdp->flags = cmdp->flags | key;
        }

        if (cmdp->flags)
        {
            s_ptr = s_ptrbuff = alloc_lbuf("@hook");
            show_hook(s_ptrbuff, s_ptr, HOOKMASK(cmdp->flags));
            notify(executor, tprintf(T("@hook: New mask for \xE2\x80\x98%s\xE2\x80\x99 -> %s"), cmdp->cmdname, s_ptrbuff));
            free_lbuf(s_ptrbuff);
        }
        else
        {
            notify(executor, tprintf(T("@hook: New mask for \xE2\x80\x98%s\xE2\x80\x99 is empty."), cmdp->cmdname));
        }
    }

    if (  (key & CEF_HOOK_LIST)
       || !key)
    {
        if (cmdp)
        {
            if (cmdp->flags)
            {
                s_ptr = s_ptrbuff = alloc_lbuf("@hook");
                show_hook(s_ptrbuff, s_ptr, HOOKMASK(cmdp->flags));
                notify(executor, tprintf(T("@hook: Mask for hashed command \xE2\x80\x98%s\xE2\x80\x99 -> %s"), cmdp->cmdname, s_ptrbuff));
                free_lbuf(s_ptrbuff);
            }
            else
            {
                notify(executor, tprintf(T("@hook: Mask for hashed command \xE2\x80\x98%s\xE2\x80\x99 is empty."), cmdp->cmdname));
            }
        }
        else
        {
            notify(executor, tprintf(T("%.32s-+-%s"),
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf(T("%-32s | %s"), "Built-in Command", "Hook Mask Values"));
            notify(executor, tprintf(T("%.32s-+-%s"),
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
                    if (0 != HOOKMASK(cmdp2->flags))
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
                    if (0 != HOOKMASK(cmdp2->flags))
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
                    if (0 != HOOKMASK(cmdp2->flags))
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
                    if (0 != HOOKMASK(cmdp2->flags))
                    {
                        found = true;
                        hook_loop(executor, (CMDENT *)cmdp2, s_ptr, s_ptrbuff);
                    }
                }
            }
            if (!found)
            {
                notify(executor, tprintf(T("%26s -- No @hooks defined --"), " "));
            }
            found = false;
            /* We need to search the attribute table as well */
            notify(executor, tprintf(T("%.32s-+-%s"),
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf(T("%-32s | %s"), "Built-in Attribute", "Hook Mask Values"));
            notify(executor, tprintf(T("%.32s-+-%s"),
                "--------------------------------",
                "--------------------------------------------"));
            cbuff = alloc_sbuf("cbuff_hook");
            for (ATTR *ap = AttrTable; ap->name; ap++)
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
                    safe_sb_chr(mux_tolower_ascii(*q), cbuff, &p);
                }
                *p = '\0';
                size_t ncbuff = p - cbuff;
                cmdp = (CMDENT *)hashfindLEN(cbuff, ncbuff, &mudstate.command_htab);
                if (  cmdp
                   && 0 != HOOKMASK(cmdp->flags))
                {
                    found = true;
                    show_hook(s_ptrbuff, s_ptr, HOOKMASK(cmdp->flags));
                    notify(executor, tprintf(T("%-32.32s | %s"), cmdp->cmdname, s_ptrbuff));
                }
            }
            free_sbuf(cbuff);
            if (!found)
            {
                notify(executor, tprintf(T("%26s -- No @hooks defined --"), " "));
            }
            free_lbuf(s_ptrbuff);
            notify(executor, tprintf(T("%.32s-+-%s"),
                "--------------------------------",
                "--------------------------------------------"));
            notify(executor, tprintf(T("The hook object is currently: #%d (%s)"),
                mudconf.hook_obj,
                (  (  Good_obj(mudconf.hook_obj)
                   && !Going(mudconf.hook_obj))
                ? "VALID" : "INVALID")));
        }
    }
}

NAMETAB allow_charset_nametab[] =
{
    {T("ascii"),           5,       0,     ALLOW_CHARSET_ASCII},
    {T("hangul"),          6,       0,     ALLOW_CHARSET_HANGUL},
    {T("hiragana"),        8,       0,     ALLOW_CHARSET_HIRAGANA},
    {T("iso8859-1"),       9,       0,     ALLOW_CHARSET_8859_1},
    {T("iso8859-2"),       9,       0,     ALLOW_CHARSET_8859_2},
    {T("kanji"),           5,       0,     ALLOW_CHARSET_KANJI},
    {T("katakana"),        8,       0,     ALLOW_CHARSET_KATAKANA},
    {T("latin-1"),         7,       0,     ALLOW_CHARSET_8859_1},
    {T("latin-2"),         7,       0,     ALLOW_CHARSET_8859_2},
    {(UTF8 *) NULL,        0,       0,     0}
};
