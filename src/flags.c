/*
 * flags.c - flag manipulation routines 
 */
/*
 * $Id: flags.c,v 1.2 1997/04/16 06:01:00 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "db.h"
#include "mudconf.h"
#include "externs.h"
#include "command.h"
#include "flags.h"
#include "alloc.h"
#include "powers.h"

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * fh_any: set or clear indicated bit, no security checking
 */

int fh_any(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (fflags & FLAG_WORD3) {
		if (reset)
			s_Flags3(target, Flags3(target) & ~flag);
		else
			s_Flags3(target, Flags3(target) | flag);
	} else if (fflags & FLAG_WORD2) {
		if (reset)
			s_Flags2(target, Flags2(target) & ~flag);
		else
			s_Flags2(target, Flags2(target) | flag);
	} else {
		if (reset)
			s_Flags(target, Flags(target) & ~flag);
		else
			s_Flags(target, Flags(target) | flag);
	}
	return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * fh_god: only GOD may set or clear the bit
 */

int fh_god(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!God(player))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_wiz: only WIZARDS (or GOD) may set or clear the bit
 */

int fh_wiz(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!Wizard(player) && !God(player))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_fixed: Settable only on players by WIZARDS
 */

int fh_fixed(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (isPlayer(target))
		if (!Wizard(player) && !God(player))
			return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_wizroy: only WIZARDS, ROYALTY, (or GOD) may set or clear the bit
 */

int fh_wizroy(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!WizRoy(player) && !God(player))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_inherit: only players may set or clear this bit.
 */

int fh_inherit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!Inherits(player))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_wiz_bit: Only GOD may set/clear this bit on others.
 */

int fh_wiz_bit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!God(player))
		return 0;
	if (God(target) && reset) {
		notify(player, "You cannot make yourself mortal.");
		return 0;
	}
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_dark_bit: manipulate the dark bit. Nonwizards may not set on players.
 */

int fh_dark_bit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (!reset && isPlayer(target) && !((target == player) &&
					    Can_Hide(player)) &&
	    (!Wizard(player) && !God(player)))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_going_bit: manipulate the going bit.  Non-gods may only clear on rooms.
 */

int fh_going_bit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	if (Going(target) && reset && (Typeof(target) != TYPE_GARBAGE)) {
		notify(player, "Your object has been spared from destruction.");
		return (fh_any(target, player, flag, fflags, reset));
	}
	if (!God(player))
		return 0;
	return (fh_any(target, player, flag, fflags, reset));
}

/*
 * ---------------------------------------------------------------------------
 * * fh_hear_bit: set or clear bits that affect hearing.
 */

int fh_hear_bit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{
	int could_hear;

	if (isPlayer(target) && (flag & MONITOR))
		if (Can_Monitor(player))
			fh_any(target, player, flag, fflags, reset);
		else
			return 0;

	could_hear = Hearer(target);
	fh_any(target, player, flag, fflags, reset);
	handle_ears(target, could_hear, Hearer(target));
	return 1;
}

#ifdef DSPACE
/*
 * ---------------------------------------------------------------------------
 * * fh_dynamic_bit: only settable on rooms or exits.
 */

int fh_dynamic_bit(target, player, flag, fflags, reset)
dbref target, player;
FLAG flag;
int fflags, reset;
{

	if (isPlayer(target) || isThing(target))
		return 0;

	return (fh_any(target, player, flag, fflags, reset));
}

#endif
/* *INDENT-OFF* */

FLAGENT gen_flags[] = { 
{"ABODE",		ABODE,		'A',
	FLAG_WORD2,	0,			fh_any},
{"ANSI",                ANSI,           'X',   
        FLAG_WORD2,       0,                      fh_any},
{"AUDITORIUM",		AUDITORIUM,	'b',
	FLAG_WORD2,	0,			fh_any},
{"COMPRESS",		COMPRESS,	'.',
	FLAG_WORD2, 	0,			fh_any},
{"CHOWN_OK",		CHOWN_OK,	'C',
	0,		0,			fh_any},
{"HAS_DAILY",		HAS_DAILY,	'*',
	FLAG_WORD2,		CA_GOD|CA_NO_DECOMP,	fh_god},
{"PLAYER_MAILS",	PLAYER_MAILS,	'B',
	FLAG_WORD2,		CA_GOD|CA_NO_DECOMP,	fh_god},
{"DARK",		DARK,		'D',
	0,		0,			fh_dark_bit},
{"FLOATING",		FLOATING,	'F',
	FLAG_WORD2,	0,			fh_any},
{"GAGGED", 		GAGGED,		'j',
	FLAG_WORD2,	0,			fh_wiz},
{"GOING",		GOING,		'G',
	0,		CA_NO_DECOMP,		fh_going_bit},
{"HAVEN",		HAVEN,		'H',
	0,		0,			fh_any},
{"HEAD",                HEAD_FLAG,      '?',
        FLAG_WORD2,       0,                      fh_wiz},
{"INHERIT",		INHERIT,	'I',
	0,		0,			fh_inherit},
{"JUMP_OK",		JUMP_OK,	'J',
	0,		0,			fh_any},
{"KEY",			KEY,		'K',
	FLAG_WORD2,	0,			fh_any},
{"LINK_OK",		LINK_OK,	'L',
	0,		0,			fh_any},
{"MONITOR",		MONITOR,	'M',
	0,		0,			fh_hear_bit},
{"NOSPOOF",		NOSPOOF,	'N',
	0,		0,			fh_any},
{"OPAQUE",		OPAQUE,		'O',
	0,		0,			fh_any},
{"QUIET",		QUIET,		'Q',
	0,		0,			fh_any},
{"STAFF",		STAFF,		'w',
	FLAG_WORD2,		0,			fh_wiz},
{"STICKY",		STICKY,		'S',
	0,		0,			fh_any},
{"TRACE",		TRACE,		'T',
	0,		0,			fh_any},
{"UNFINDABLE",		UNFINDABLE,	'U',
	FLAG_WORD2,	0,			fh_any},
{"VISUAL",		VISUAL,		'V',
	0,		0,			fh_any},
{"VACATION",		VACATION,	'|',
	FLAG_WORD2,	0,			fh_fixed},
{"WIZARD",		WIZARD,		'W',
	0,		0,			fh_wiz_bit},
{"PARENT_OK",		PARENT_OK,	'Y',
	FLAG_WORD2,	0,			fh_any},
{"ROYALTY",             ROYALTY,        'Z',    
        0,	       0,                      fh_wiz},
{"FIXED",               FIXED,           'f',
        FLAG_WORD2,       0,                      fh_fixed}, 
{"UNINSPECTED",         UNINSPECTED,     'g',
        FLAG_WORD2,       0,                      fh_wizroy},
{"NO_COMMAND",          NO_COMMAND,      'n',
        FLAG_WORD2,       0,                      fh_any},
{"NOBLEED",             NOBLEED,         '-',
        FLAG_WORD2,       0,                      fh_any},
#ifdef DSPACE
{"DYNAMIC",             DYNAMIC,         '!',
        FLAG_WORD2,       0,                      fh_dynamic_bit},
#endif
{"AUDIBLE",		HEARTHRU,	'a',
	0,		0,			fh_hear_bit},
{"CONNECTED",		CONNECTED,	'c',
	FLAG_WORD2,	CA_NO_DECOMP,		fh_god},
{"DESTROY_OK",		DESTROY_OK,	'd',
	0,		0,			fh_any},
{"ENTER_OK",		ENTER_OK,	'e',
	0,		0,			fh_any},
{"HALTED",		HALT,		'h',
	0,		0,			fh_any},
{"IMMORTAL",		IMMORTAL,	'i',
	0,		0,			fh_wiz},
{"LIGHT",		LIGHT,		'l',
	FLAG_WORD2,	0,			fh_any},
{"MYOPIC",		MYOPIC,		'm',
	0,		0,			fh_any},
{"PUPPET",		PUPPET,		'p',
	0,		0,			fh_hear_bit},
{"TERSE",		TERSE,		'q',
	0,		0,			fh_any},
{"ROBOT",		ROBOT,		'r',
	0,		0,			fh_any},
{"SAFE",		SAFE,		's',
	0,		0,			fh_any},
{"TRANSPARENT",		SEETHRU,	't',
	0,		0,			fh_any},
{"SUSPECT",		SUSPECT,	'u',
	FLAG_WORD2,	CA_WIZARD,		fh_wiz},
{"VERBOSE",		VERBOSE,	'v',
	0,		0,			fh_any},
{"SLAVE",		SLAVE,		'x',
	FLAG_WORD2,	CA_WIZARD,		fh_wiz},
{"HAS_STARTUP",		HAS_STARTUP,	'+',
	0,		CA_GOD|CA_NO_DECOMP,	fh_god},
{"HAS_FORWARDLIST",	HAS_FWDLIST,	'&',
	FLAG_WORD2,	CA_GOD|CA_NO_DECOMP,	fh_god},
{"HAS_LISTEN",		HAS_LISTEN,	'@',
	FLAG_WORD2,	CA_GOD|CA_NO_DECOMP,	fh_god},
{"HTML", 		HTML,           '(',
	FLAG_WORD2,       0,                      fh_any},
{ NULL,			0,		' ',
	0,		0,			NULL}};

#endif	/* STANDALONE */

OBJENT object_types[8] = {
{"ROOM",	'R', CA_PUBLIC,	OF_CONTENTS|OF_EXITS|OF_DROPTO|OF_HOME},
{"THING",	' ', CA_PUBLIC,
	OF_CONTENTS|OF_LOCATION|OF_EXITS|OF_HOME|OF_SIBLINGS},
{"EXIT",	'E', CA_PUBLIC,	OF_SIBLINGS},
{"PLAYER",	'P', CA_PUBLIC,
	OF_CONTENTS|OF_LOCATION|OF_EXITS|OF_HOME|OF_OWNER|OF_SIBLINGS},
{"TYPE5",	'+', CA_GOD,	0},
{"GARBAGE",	'-', CA_PUBLIC,
	OF_CONTENTS|OF_LOCATION|OF_EXITS|OF_HOME|OF_SIBLINGS},
{"GARBAGE",	'#', CA_GOD,	0}};

/* *INDENT-ON* */





#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * init_flagtab: initialize flag hash tables.
 */

void NDECL(init_flagtab)
{
	FLAGENT *fp;
	char *nbuf, *np, *bp;

	hashinit(&mudstate.flags_htab, 100 * HASH_FACTOR);
	nbuf = alloc_sbuf("init_flagtab");
	for (fp = gen_flags; fp->flagname; fp++) {
		for (np = nbuf, bp = (char *)fp->flagname; *bp; np++, bp++)
			*np = ToLower(*bp);
		*np = '\0';
		hashadd(nbuf, (int *)fp, &mudstate.flags_htab);
	}
	free_sbuf(nbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * display_flags: display available flags.
 */

void display_flagtab(player)
dbref player;
{
	char *buf, *bp;
	FLAGENT *fp;

	bp = buf = alloc_lbuf("display_flagtab");
	safe_str((char *)"Flags:", buf, &bp);
	for (fp = gen_flags; fp->flagname; fp++) {
		if ((fp->listperm & CA_WIZARD) && !Wizard(player))
			continue;
		if ((fp->listperm & CA_GOD) && !God(player))
			continue;
		safe_chr(' ', buf, &bp);
		safe_str((char *)fp->flagname, buf, &bp);
		safe_chr('(', buf, &bp);
		safe_chr(fp->flaglett, buf, &bp);
		safe_chr(')', buf, &bp);
	}
	*bp = '\0';
	notify(player, buf);
	free_lbuf(buf);
}

FLAGENT *find_flag(thing, flagname)
dbref thing;
char *flagname;
{
	char *cp;

	/*
	 * Make sure the flag name is valid 
	 */

	for (cp = flagname; *cp; cp++)
		*cp = ToLower(*cp);
	return (FLAGENT *) hashfind(flagname, &mudstate.flags_htab);
}

/*
 * ---------------------------------------------------------------------------
 * * flag_set: Set or clear a specified flag on an object. 
 */

void flag_set(target, player, flag, key)
dbref target, player;
char *flag;
int key;
{
	FLAGENT *fp;
	int negate, result;

	/*
	 * Trim spaces, and handle the negation character 
	 */

	negate = 0;
	while (*flag && isspace(*flag))
		flag++;
	if (*flag == '!') {
		negate = 1;
		flag++;
	}
	while (*flag && isspace(*flag))
		flag++;

	/*
	 * Make sure a flag name was specified 
	 */

	if (*flag == '\0') {
		if (negate)
			notify(player, "You must specify a flag to clear.");
		else
			notify(player, "You must specify a flag to set.");
		return;
	}
	fp = find_flag(target, flag);
	if (fp == NULL) {
		notify(player, "I don't understand that flag.");
		return;
	}
	/*
	 * Invoke the flag handler, and print feedback 
	 */

	result = fp->handler(target, player, fp->flagvalue,
			     fp->flagflag, negate);
	if (!result)
		notify(player, "Permission denied.");
	else if (!(key & SET_QUIET) && !Quiet(player))
		notify(player, (negate ? "Cleared." : "Set."));
	return;
}

/*
 * ---------------------------------------------------------------------------
 * * decode_flags: converts a flags word into corresponding letters.
 */

char *decode_flags(player, flagword, flag2word, flag3word)
dbref player;
FLAG flagword, flag2word, flag3word;
{
	char *buf, *bp;
	FLAGENT *fp;
	int flagtype;
	FLAG fv;

	buf = bp = alloc_sbuf("decode_flags");
	*bp = '\0';

	if (!Good_obj(player)) {
		StringCopy(buf, "#-2 ERROR");
		return buf;
	}
	flagtype = (flagword & TYPE_MASK);
	if (object_types[flagtype].lett != ' ')
		safe_sb_chr(object_types[flagtype].lett, buf, &bp);

	for (fp = gen_flags; fp->flagname; fp++) {
		if (fp->flagflag & FLAG_WORD3)
			fv = flag3word;
		else if (fp->flagflag & FLAG_WORD2)
			fv = flag2word;
		else
			fv = flagword;
		if (fv & fp->flagvalue) {
			if ((fp->listperm & CA_WIZARD) && !Wizard(player))
				continue;
			if ((fp->listperm & CA_GOD) && !God(player))
				continue;
			/*
			 * don't show CONNECT on dark wizards to mortals 
			 */
			if ((flagtype == TYPE_PLAYER) &&
			    (fp->flagvalue == CONNECTED) &&
			((flagword & (WIZARD | DARK)) == (WIZARD | DARK)) &&
			    !Wizard(player))
				continue;
			safe_sb_chr(fp->flaglett, buf, &bp);
		}
	}

	*bp = '\0';
	return buf;
}

/*
 * ---------------------------------------------------------------------------
 * * has_flag: does object have flag visible to player?
 */

int has_flag(player, it, flagname)
dbref player, it;
char *flagname;
{
	FLAGENT *fp;
	FLAG fv;

	fp = find_flag(it, flagname);
	if (fp == NULL)
		return 0;

	if (fp->flagflag & FLAG_WORD3)
		fv = Flags3(it);
	else if (fp->flagflag & FLAG_WORD2)
		fv = Flags2(it);
	else
		fv = Flags(it);

	if (fv & fp->flagvalue) {
		if ((fp->listperm & CA_WIZARD) && !Wizard(player))
			return 0;
		if ((fp->listperm & CA_GOD) && !God(player))
			return 0;
		/*
		 * don't show CONNECT on dark wizards to mortals 
		 */
		if (isPlayer(it) &&
		    (fp->flagvalue == CONNECTED) &&
		    ((Flags(it) & (WIZARD | DARK)) == (WIZARD | DARK)) &&
		    !Wizard(player))
			return 0;
		return 1;
	}
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * flag_description: Return an mbuf containing the type and flags on thing.
 */

char *flag_description(player, target)
dbref player, target;
{
	char *buff, *bp;
	FLAGENT *fp;
	int otype;
	FLAG fv;

	/*
	 * Allocate the return buffer 
	 */

	otype = Typeof(target);
	bp = buff = alloc_mbuf("flag_description");

	/*
	 * Store the header strings and object type 
	 */

	safe_mb_str((char *)"Type: ", buff, &bp);
	safe_mb_str((char *)object_types[otype].name, buff, &bp);
	safe_mb_str((char *)" Flags:", buff, &bp);
	if (object_types[otype].perm != CA_PUBLIC) {
		*bp = '\0';
		return buff;
	}
	/*
	 * Store the type-invariant flags 
	 */

	for (fp = gen_flags; fp->flagname; fp++) {
		if (fp->flagflag & FLAG_WORD3)
			fv = Flags3(target);
		else if (fp->flagflag & FLAG_WORD2)
			fv = Flags2(target);
		else
			fv = Flags(target);
		if (fv & fp->flagvalue) {
			if ((fp->listperm & CA_WIZARD) && !Wizard(player))
				continue;
			if ((fp->listperm & CA_GOD) && !God(player))
				continue;
			/*
			 * don't show CONNECT on dark wizards to mortals 
			 */
			if (isPlayer(target) &&
			    (fp->flagvalue == CONNECTED) &&
			    ((Flags(target) & (WIZARD | DARK)) == (WIZARD | DARK)) &&
			    !Wizard(player))
				continue;
			safe_mb_chr(' ', buff, &bp);
			safe_mb_str((char *)fp->flagname, buff, &bp);
		}
	}

	/*
	 * Terminate the string, and return the buffer to the caller 
	 */

	*bp = '\0';
	return buff;
}

/*
 * ---------------------------------------------------------------------------
 * * Return an lbuf containing the name and number of an object
 */

char *unparse_object_numonly(target)
dbref target;
{
	char *buf;

	buf = alloc_lbuf("unparse_object_numonly");
	if (target == NOTHING) {
		StringCopy(buf, "*NOTHING*");
	} else if (target == HOME) {
		StringCopy(buf, "*HOME*");
	} else if (!Good_obj(target)) {
		sprintf(buf, "*ILLEGAL*(#%d)", target);
	} else {
		sprintf(buf, "%s(#%d)", Name(target), target);
	}
	return buf;
}

/*
 * ---------------------------------------------------------------------------
 * * Return an lbuf pointing to the object name and possibly the db# and flags
 */

char *unparse_object(player, target, obey_myopic)
dbref player, target;
int obey_myopic;
{
	char *buf, *fp;
	int exam;

	buf = alloc_lbuf("unparse_object");
	if (target == NOTHING) {
		StringCopy(buf, "*NOTHING*");
	} else if (target == HOME) {
		StringCopy(buf, "*HOME*");
	} else if (!Good_obj(target)) {
		sprintf(buf, "*ILLEGAL*(#%d)", target);
	} else {
		if (obey_myopic)
			exam = MyopicExam(player, target);
		else
			exam = Examinable(player, target);
		if (exam ||
		    (Flags(target) & (CHOWN_OK | JUMP_OK | LINK_OK | DESTROY_OK)) ||
		    (Flags2(target) & ABODE)) {

			/*
			 * show everything 
			 */
			fp = unparse_flags(player, target);
			sprintf(buf, "%s(#%d%s)", Name(target), target, fp);
			free_sbuf(fp);
		} else {
			/*
			 * show only the name. 
			 */
			StringCopy(buf, Name(target));
		}
	}
	return buf;
}

/*
 * ---------------------------------------------------------------------------
 * * convert_flags: convert a list of flag letters into its bit pattern.
 * * Also set the type qualifier if specified and not already set.
 */

int convert_flags(player, flaglist, fset, p_type)
dbref player;
char *flaglist;
FLAGSET *fset;
FLAG *p_type;
{
	int i, handled;
	char *s;
	FLAG flag1mask, flag2mask, flag3mask, type;
	FLAGENT *fp;

	flag1mask = flag2mask = flag3mask = 0;
	type = NOTYPE;

	for (s = flaglist; *s; s++) {
		handled = 0;

		/*
		 * Check for object type 
		 */

		for (i = 0; (i <= 7) && !handled; i++) {
			if ((object_types[i].lett == *s) &&
			    !(((object_types[i].perm & CA_WIZARD) &&
			       !Wizard(player)) ||
			      ((object_types[i].perm & CA_GOD) &&
			       !God(player)))) {
				if ((type != NOTYPE) && (type != i)) {
					notify(player,
					       tprintf("%c: Conflicting type specifications.",
						       *s));
					return 0;
				}
				type = i;
				handled = 1;
			}
		}

		/*
		 * Check generic flags 
		 */

		if (handled)
			continue;
		for (fp = gen_flags; (fp->flagname) && !handled; fp++) {
			if ((fp->flaglett == *s) &&
			    !(((fp->listperm & CA_WIZARD) &&
			       !Wizard(player)) ||
			      ((fp->listperm & CA_GOD) &&
			       !God(player)))) {
				if (fp->flagflag & FLAG_WORD3)
					flag3mask |= fp->flagvalue;
				else if (fp->flagflag & FLAG_WORD2)
					flag2mask |= fp->flagvalue;
				else
					flag1mask |= fp->flagvalue;
				handled = 1;
			}
		}

		if (!handled) {
			notify(player,
			       tprintf("%c: Flag unknown or not valid for specified object type",
				       *s));
			return 0;
		}
	}

	/*
	 * return flags to search for and type 
	 */

	(*fset).word1 = flag1mask;
	(*fset).word2 = flag2mask;
	(*fset).word3 = flag3mask;
	*p_type = type;
	return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * decompile_flags: Produce commands to set flags on target.
 */

void decompile_flags(player, thing, thingname)
dbref player, thing;
char *thingname;
{
	FLAG f1, f2, f3;
	FLAGENT *fp;

	/*
	 * Report generic flags 
	 */

	f1 = Flags(thing);
	f2 = Flags2(thing);
	f3 = Flags3(thing);
	
	for (fp = gen_flags; fp->flagname; fp++) {

		/*
		 * Skip if we shouldn't decompile this flag 
		 */

		if (fp->listperm & CA_NO_DECOMP)
			continue;

		/*
		 * Skip if this flag is not set 
		 */

		if (fp->flagflag & FLAG_WORD3) {
			if (!(f3 & fp->flagvalue))
				continue;
		} else if (fp->flagflag & FLAG_WORD2) {
			if (!(f2 & fp->flagvalue))
				continue;
		} else {
			if (!(f1 & fp->flagvalue))
				continue;
		}

		/*
		 * Skip if we can't see this flag 
		 */

		if (!check_access(player, fp->listperm))
			continue;

		/*
		 * We made it this far, report this flag 
		 */

		notify(player, tprintf("@set %s=%s", strip_ansi(thingname), fp->flagname));
	}
}

#endif /*
        * STANDALONE 
        */
