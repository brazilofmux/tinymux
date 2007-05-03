/*
 * unparse.c 
 */
/*
 * $Id: unparse.c,v 1.2 1997/04/16 06:02:03 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "config.h"
#include "db.h"
#include "mudconf.h"
#include "externs.h"
#include "interface.h"
#include "flags.h"
#include "powers.h"
#include "alloc.h"

/*
 * Boolexp decompile formats 
 */

#define F_EXAMINE	1	/*
				 * Normal 
				 */
#define F_QUIET		2	/*
				 * Binary for db dumps 
				 */
#define F_DECOMPILE	3	/*
				 * @decompile output 
				 */
#define F_FUNCTION	4	/*
				 * [lock()] output 
				 */

/*
 * Take a dbref (loc) and generate a string.  -1, -3, or (#loc) Note, this
 * will give players object numbers of stuff they don't control, but it's
 * only internal currently, so it's not a problem.
 */

static char *unparse_object_quiet(player, loc)
dbref player, loc;
{
	static char buf[SBUF_SIZE];

	switch (loc) {
	case NOTHING:
		return (char *)"-1";
	case HOME:
		return (char *)"-3";
	default:
		sprintf(buf, "(#%d)", loc);
		return buf;
	}
}

static char boolexp_buf[LBUF_SIZE];
static char *buftop;

static void unparse_boolexp1(player, b, outer_type, format)
dbref player;
BOOLEXP *b;
char outer_type;
int format;
{
	ATTR *ap;
	char *tbuf, sep_ch;

#ifndef STANDALONE
	char *buff;

#endif

	if ((b == TRUE_BOOLEXP)) {
		if (format == F_EXAMINE) {
			safe_str((char *)"*UNLOCKED*", boolexp_buf, &buftop);
		}
		return;
	}
	switch (b->type) {
	case BOOLEXP_AND:
		if (outer_type == BOOLEXP_NOT) {
			safe_chr('(', boolexp_buf, &buftop);
		}
		unparse_boolexp1(player, b->sub1, b->type, format);
		safe_chr(AND_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub2, b->type, format);
		if (outer_type == BOOLEXP_NOT) {
			safe_chr(')', boolexp_buf, &buftop);
		}
		break;
	case BOOLEXP_OR:
		if (outer_type == BOOLEXP_NOT || outer_type == BOOLEXP_AND) {
			safe_chr('(', boolexp_buf, &buftop);
		}
		unparse_boolexp1(player, b->sub1, b->type, format);
		safe_chr(OR_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub2, b->type, format);
		if (outer_type == BOOLEXP_NOT || outer_type == BOOLEXP_AND) {
			safe_chr(')', boolexp_buf, &buftop);
		}
		break;
	case BOOLEXP_NOT:
		safe_chr('!', boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub1, b->type, format);
		break;
	case BOOLEXP_INDIR:
		safe_chr(INDIR_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub1, b->type, format);
		break;
	case BOOLEXP_IS:
		safe_chr(IS_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub1, b->type, format);
		break;
	case BOOLEXP_CARRY:
		safe_chr(CARRY_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub1, b->type, format);
		break;
	case BOOLEXP_OWNER:
		safe_chr(OWNER_TOKEN, boolexp_buf, &buftop);
		unparse_boolexp1(player, b->sub1, b->type, format);
		break;
	case BOOLEXP_CONST:
#ifndef STANDALONE
		switch (format) {
		case F_QUIET:

			/*
			 * Quiet output - for dumps and internal use. * * * * 
			 * Always #Num 
			 */

			safe_str((char *)unparse_object_quiet(player, b->thing),
				 boolexp_buf, &buftop);
			break;
		case F_EXAMINE:

			/*
			 * Examine output - informative. * Name(#Num) or Name 
			 * 
			 * *  
			 */

			buff = unparse_object(player, b->thing, 0);
			safe_str(buff, boolexp_buf, &buftop);
			free_lbuf(buff);
			break;
		case F_DECOMPILE:

			/*
			 * Decompile output - should be usable on other * * * 
			 * MUXes. * *Name if player, Name if thing, else #Num 
			 * 
			 * *  
			 */

			switch (Typeof(b->thing)) {
			case TYPE_PLAYER:
				safe_chr('*', boolexp_buf, &buftop);
			case TYPE_THING:
				safe_str(Name(b->thing), boolexp_buf, &buftop);
				break;
			default:
				buff = alloc_sbuf("unparse_boolexp1");
				sprintf(buff, "#%d", b->thing);
				safe_str(buff, boolexp_buf, &buftop);
				free_sbuf(buff);
			}
			break;
		case F_FUNCTION:

			/*
			 * Function output - must be usable by @lock cmd. * * 
			 * 
			 * *  * * *Name if player, else #Num 
			 */

			switch (Typeof(b->thing)) {
			case TYPE_PLAYER:
				safe_chr('*', boolexp_buf, &buftop);
				safe_str(Name(b->thing), boolexp_buf, &buftop);
				break;
			default:
				buff = alloc_sbuf("unparse_boolexp1");
				sprintf(buff, "#%d", b->thing);
				safe_str(buff, boolexp_buf, &buftop);
				free_sbuf(buff);
			}
		}
#else
		safe_str((char *)unparse_object_quiet(player, b->thing),
			 boolexp_buf, &buftop);
#endif
		break;
	case BOOLEXP_ATR:
	case BOOLEXP_EVAL:
		if (b->type == BOOLEXP_EVAL)
			sep_ch = '/';
		else
			sep_ch = ':';
		ap = atr_num(b->thing);
		if (ap && ap->number) {
			safe_str((char *)ap->name, boolexp_buf, &buftop);
			safe_chr(sep_ch, boolexp_buf, &buftop);
			safe_str((char *)b->sub1, boolexp_buf, &buftop);
		} else if (b->thing > 0) {
			tbuf = alloc_sbuf("unparse_boolexp1.atr_num");
			sprintf(tbuf, "%d", b->thing);
			safe_str(tbuf, boolexp_buf, &buftop);
			safe_chr(sep_ch, boolexp_buf, &buftop);
			safe_str((char *)b->sub1, boolexp_buf, &buftop);
			free_sbuf(tbuf);
		} else {
			safe_str((char *)b->sub2, boolexp_buf, &buftop);
			safe_chr(sep_ch, boolexp_buf, &buftop);
			safe_str((char *)b->sub1, boolexp_buf, &buftop);
		}
		break;
	default:
		fprintf(stderr,
		      "Fell off the end of switch in unparse_boolexp1()\n");
		abort();	/*
				 * bad type 
				 */
		break;
	}
}

char *unparse_boolexp_quiet(player, b)
dbref player;
BOOLEXP *b;
{
	buftop = boolexp_buf;
	unparse_boolexp1(player, b, BOOLEXP_CONST, F_QUIET);
	*buftop++ = '\0';
	return boolexp_buf;
}

#ifndef STANDALONE

char *unparse_boolexp(player, b)
dbref player;
BOOLEXP *b;
{
	buftop = boolexp_buf;
	unparse_boolexp1(player, b, BOOLEXP_CONST, F_EXAMINE);
	*buftop++ = '\0';
	return boolexp_buf;
}

char *unparse_boolexp_decompile(player, b)
dbref player;
BOOLEXP *b;
{
	buftop = boolexp_buf;
	unparse_boolexp1(player, b, BOOLEXP_CONST, F_DECOMPILE);
	*buftop++ = '\0';
	return boolexp_buf;
}

char *unparse_boolexp_function(player, b)
dbref player;
BOOLEXP *b;
{
	buftop = boolexp_buf;
	unparse_boolexp1(player, b, BOOLEXP_CONST, F_FUNCTION);
	*buftop++ = '\0';
	return boolexp_buf;
}

#endif
