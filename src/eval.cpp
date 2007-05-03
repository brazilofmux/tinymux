/*
 * eval.c - command evaluation and cracking 
 */
/*
 * $Id: eval.c,v 1.2 1997/04/16 06:00:56 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "db.h"
#include "externs.h"
#include "attrs.h"
#include "functions.h"
#include "alloc.h"
#include "ansi.h"

/*
 * ---------------------------------------------------------------------------
 * * parse_to: Split a line at a character, obeying nesting.  The line is
 * * destructively modified (a null is inserted where the delimiter was found)
 * * dstr is modified to point to the char after the delimiter, and the function
 * * return value points to the found string (space compressed if specified).
 * * If we ran off the end of the string without finding the delimiter, dstr is
 * * returned as NULL.
 */

static char *parse_to_cleanup(eval, first, cstr, rstr, zstr)
int eval, first;
char *cstr, *rstr, *zstr;
{
	if ((mudconf.space_compress || (eval & EV_STRIP_TS)) &&
	    !(eval & EV_NO_COMPRESS) && !first && (cstr[-1] == ' '))
		zstr--;
	if ((eval & EV_STRIP_AROUND) && (*rstr == '{') && (zstr[-1] == '}')) {
		rstr++;
		if (mudconf.space_compress && !(eval & EV_NO_COMPRESS) ||
		    (eval & EV_STRIP_LS))
			while (*rstr && isspace(*rstr))
				rstr++;
		rstr[-1] = '\0';
		zstr--;
		if (mudconf.space_compress && !(eval & EV_NO_COMPRESS) ||
		    (eval & EV_STRIP_TS))
			while (zstr[-1] && isspace(zstr[-1]))
				zstr--;
		*zstr = '\0';
	}
	*zstr = '\0';
	return rstr;
}

/* We can't change this to just '*zstr++ = *cstr++', because of the inherent
problems with copying a memory location to itself. */

#define NEXTCHAR \
	if (cstr == zstr) { \
		cstr++; \
		zstr++; \
	} else \
		*zstr++ = *cstr++


char *parse_to(dstr, delim, eval)
char **dstr, delim;
int eval;
{
#define stacklim 32
	char stack[stacklim];
	char *rstr, *cstr, *zstr;
	int sp, tp, first, bracketlev;

	if ((dstr == NULL) || (*dstr == NULL))
		return NULL;
	if (**dstr == '\0') {
		rstr = *dstr;
		*dstr = NULL;
		return rstr;
	}
	sp = 0;
	first = 1;
	rstr = *dstr;
	if ((mudconf.space_compress || (eval & EV_STRIP_LS)) &&
	    !(eval & EV_NO_COMPRESS)) {
		while (*rstr && isspace(*rstr))
			rstr++;
		*dstr = rstr;
	}
	zstr = cstr = rstr;
	while (*cstr) {
		switch (*cstr) {
		case '\\':	/*
				 * general escape 
				 */
		case '%':	/*
				 * also escapes chars 
				 */
			if ((*cstr == '\\') && (eval & EV_STRIP_ESC))
				cstr++;
			else
				NEXTCHAR;
			if (*cstr)
				NEXTCHAR;
			first = 0;
			break;
		case ']':
		case ')':
			for (tp = sp - 1; (tp >= 0) && (stack[tp] != *cstr); tp--) ;

			/*
			 * If we hit something on the stack, unwind to it 
			 * Otherwise (it's not on stack), if it's our
			 * delim  we are done, and we convert the 
			 * delim to a null and return a ptr to the
			 * char after the null. If it's not our
			 * delimiter, skip over it normally  
			 */

			if (tp >= 0)
				sp = tp;
			else if (*cstr == delim) {
				rstr = parse_to_cleanup(eval, first,
							cstr, rstr, zstr);
				*dstr = ++cstr;
				return rstr;
			}
			first = 0;
			NEXTCHAR;
			break;
		case '{':
			bracketlev = 1;
			if (eval & EV_STRIP) {
				cstr++;
			} else {
				NEXTCHAR;
			}
			while (*cstr && (bracketlev > 0)) {
				switch (*cstr) {
				case '\\':
				case '%':
					if (cstr[1]) {
						if ((*cstr == '\\') &&
						    (eval & EV_STRIP_ESC))
							cstr++;
						else
							NEXTCHAR;
					}
					break;
				case '{':
					bracketlev++;
					break;
				case '}':
					bracketlev--;
					break;
				}
				if (bracketlev > 0) {
					NEXTCHAR;
				}
			}
			if ((eval & EV_STRIP) && (bracketlev == 0)) {
				cstr++;
			} else if (bracketlev == 0) {
				NEXTCHAR;
			}
			first = 0;
			break;
		default:
			if ((*cstr == delim) && (sp == 0)) {
				rstr = parse_to_cleanup(eval, first,
							cstr, rstr, zstr);
				*dstr = ++cstr;
				return rstr;
			}
			switch (*cstr) {
			case ' ':	/*
					 * space 
					 */
				if (mudconf.space_compress &&
				    !(eval & EV_NO_COMPRESS)) {
					if (first)
						rstr++;
					else if (cstr[-1] == ' ')
						zstr--;
				}
				break;
			case '[':
				if (cstr[-1] == ESC_CHAR) {
					first = 0;
					break;
				}
				if (sp < stacklim)
					stack[sp++] = ']';
				first = 0;
				break;
			case '(':
				if (sp < stacklim)
					stack[sp++] = ')';
				first = 0;
				break;
			default:
				first = 0;
			}
			NEXTCHAR;
		}
	}
	rstr = parse_to_cleanup(eval, first, cstr, rstr, zstr);
	*dstr = NULL;
	return rstr;
}

/*
 * ---------------------------------------------------------------------------
 * * parse_arglist: Parse a line into an argument list contained in lbufs.
 * * A pointer is returned to whatever follows the final delimiter.
 * * If the arglist is unterminated, a NULL is returned.  The original arglist 
 * * is destructively modified.
 */

char *parse_arglist(player, cause, dstr, delim, eval,
		    fargs, nfargs, cargs, ncargs)
dbref player, cause, eval, nfargs, ncargs;
char *dstr, delim, *fargs[], *cargs[];
{
	char *rstr, *tstr, *bp, *str;
	int arg, peval;

	for (arg = 0; arg < nfargs; arg++)
		fargs[arg] = NULL;
	if (dstr == NULL)
		return NULL;
	rstr = parse_to(&dstr, delim, 0);
	arg = 0;

	peval = (eval & ~EV_EVAL);

	while ((arg < nfargs) && rstr) {
		if (arg < (nfargs - 1))
			tstr = parse_to(&rstr, ',', peval);
		else
			tstr = parse_to(&rstr, '\0', peval);
		if (eval & EV_EVAL) {
			bp = fargs[arg] = alloc_lbuf("parse_arglist");
			str = tstr;
			exec(fargs[arg], &bp, 0, player, cause, eval | EV_FCHECK, &str,
			     cargs, ncargs);
			*bp = '\0';
		} else {
			fargs[arg] = alloc_lbuf("parse_arglist");
			StringCopy(fargs[arg], tstr);
		}
		arg++;
	}
	return dstr;
}

/*
 * ---------------------------------------------------------------------------
 * * exec: Process a command line, evaluating function calls and %-substitutions.
 */

int get_gender(player)
dbref player;
{
	char first, *atr_gotten;
	dbref aowner;
	int aflags;

	atr_gotten = atr_pget(player, A_SEX, &aowner, &aflags);
	first = *atr_gotten;
	free_lbuf(atr_gotten);
	switch (first) {
	case 'P':
	case 'p':
		return 4;
	case 'M':
	case 'm':
		return 3;
	case 'F':
	case 'f':
	case 'W':
	case 'w':
		return 2;
	default:
		return 1;
	}
}

/*
 * ---------------------------------------------------------------------------
 * * Trace cache routines.
 */

typedef struct tcache_ent TCENT;
struct tcache_ent {
	char *orig;
	char *result;
	struct tcache_ent *next;
} *tcache_head;
int tcache_top, tcache_count;

void NDECL(tcache_init)
{
	tcache_head = NULL;
	tcache_top = 1;
	tcache_count = 0;
}

int NDECL(tcache_empty)
{
	if (tcache_top) {
		tcache_top = 0;
		tcache_count = 0;
		return 1;
	}
	return 0;
}

static void tcache_add(orig, result)
char *orig, *result;
{
	char *tp;
	TCENT *xp;

	if (strcmp(orig, result)) {
		tcache_count++;
		if (tcache_count <= mudconf.trace_limit) {
			xp = (TCENT *) alloc_sbuf("tcache_add.sbuf");
			tp = alloc_lbuf("tcache_add.lbuf");
			StringCopy(tp, result);
			xp->orig = orig;
			xp->result = tp;
			xp->next = tcache_head;
			tcache_head = xp;
		} else {
			free_lbuf(orig);
		}
	} else {
		free_lbuf(orig);
	}
}

static void tcache_finish(player)
dbref player;
{
	TCENT *xp;

	while (tcache_head != NULL) {
		xp = tcache_head;
		tcache_head = xp->next;
		notify(Owner(player),
		       tprintf("%s(#%d)} '%s' -> '%s'", Name(player), player,
			       xp->orig, xp->result));
		free_lbuf(xp->orig);
		free_lbuf(xp->result);
		free_sbuf(xp);
	}
	tcache_top = 1;
	tcache_count = 0;
}

void exec(buff, bufc, tflags, player, cause, eval, dstr, cargs, ncargs)
char *buff, **bufc;
int tflags;
dbref player, cause;
int eval, ncargs;
char **dstr;
char *cargs[];
{
#define	NFARGS	30
	char *fargs[NFARGS];
	char *preserve[MAX_GLOBAL_REGS];
	char *tstr, *tbuf, *tbufc, *savepos, *atr_gotten, *start, *oldp,
	*savestr;
	char savec, ch, *str;
	char *realbuff = NULL, *realbp = NULL;
	dbref aowner;
	int at_space, nfargs, gender, i, j, alldone, aflags, feval, arg;
	int is_trace, is_top, save_count, peval, temp_flags;
	int ansi;
	FUN *fp;
	UFUN *ufp;

	static const char *subj[5] =
	{"", "it", "she", "he", "they"};
	static const char *poss[5] =
	{"", "its", "her", "his", "their"};
	static const char *obj[5] =
	{"", "it", "her", "him", "them"};
	static const char *absp[5] =
	{"", "its", "hers", "his", "theirs"};


	if (*dstr == NULL)
		return;

	at_space = 1;
	gender = -1;
	alldone = 0;
	ansi = 0;

	is_trace = Trace(player) && !(eval & EV_NOTRACE);
	is_top = 0;

	/* Extend the buffer if we need to. */
	
	if (((*bufc) - buff) > (LBUF_SIZE - SBUF_SIZE)) {
		realbuff = buff;
		realbp = *bufc;
		buff = (char *)malloc(LBUF_SIZE);
		*bufc = buff;
	}
	
	oldp = start = *bufc;
	
	/*
	 * If we are tracing, save a copy of the starting buffer 
	 */

	savestr = NULL;
	if (is_trace) {
		is_top = tcache_empty();
		savestr = alloc_lbuf("exec.save");
		StringCopy(savestr, *dstr);
	}
	while (**dstr && !alldone) {
		switch (**dstr) {
		case ' ':
			/*
			 * A space.  Add a space if not compressing or if * * 
			 * 
			 * *  * * previous char was not a space 
			 */

			if (!(mudconf.space_compress && at_space) ||
			    (eval & EV_NO_COMPRESS)) {
				safe_chr(' ', buff, bufc);
				at_space = 1;
			}
			break;
		case '\\':
			/*
			 * General escape.  Add the following char without *
			 * * * * special processing 
			 */

			at_space = 0;
			(*dstr)++;
			if (**dstr)
				safe_chr(**dstr, buff, bufc);
			else
				(*dstr)--;
			break;
		case '[':
			/*
			 * Function start.  Evaluate the contents of the * *
			 * * * square brackets as a function.  If no closing
			 * * * * * bracket, insert the [ and continue. 
			 */

			at_space = 0;
			tstr = (*dstr)++;
			if (eval & EV_NOFCHECK) {
				safe_chr('[', buff, bufc);
				*dstr = tstr;
				break;
			}
			tbuf = parse_to(dstr, ']', 0);
			if (*dstr == NULL) {
				safe_chr('[', buff, bufc);
				*dstr = tstr;
			} else {
				str = tbuf;
				exec(buff, bufc, 0, player, cause,
				     (eval | EV_FCHECK | EV_FMAND),
				     &str, cargs, ncargs);
				(*dstr)--;
			}
			break;
		case '{':
			/*
			 * Literal start.  Insert everything up to the * * *
			 * * terminating } without parsing.  If no closing *
			 * * * * brace, insert the { and continue. 
			 */

			at_space = 0;
			tstr = (*dstr)++;
			tbuf = parse_to(dstr, '}', 0);
			if (*dstr == NULL) {
				safe_chr('{', buff, bufc);
				*dstr = tstr;
			} else {
				if (!(eval & EV_STRIP)) {
					safe_chr('{', buff, bufc);
				}
				/*
				 * Preserve leading spaces (Felan) 
				 */

				if (*tbuf == ' ') {
					safe_chr(' ', buff, bufc);
					tbuf++;
				}
				str = tbuf;
				exec(buff, bufc, 0, player, cause,
				     (eval & ~(EV_STRIP | EV_FCHECK)),
				     &str, cargs, ncargs);
				if (!(eval & EV_STRIP)) {
					safe_chr('}', buff, bufc);
				}
				(*dstr)--;
			}
			break;
		case '%':
			/*
			 * Percent-replace start.  Evaluate the chars * * *
			 * following * and perform the appropriate * * *
			 * substitution. 
			 */

			at_space = 0;
			(*dstr)++;
			savec = **dstr;
			savepos = *bufc;
			switch (savec) {
			case '\0':	/*
					 * Null - all done 
					 */
				(*dstr)--;
				break;
			case '|':       /* piped command output */
				safe_str(mudstate.pout, buff, bufc);
				break;
			case '%':	/*
					 * Percent - a literal % 
					 */
				safe_chr('%', buff, bufc);
				break;
			case 'c':
			case 'C':
				(*dstr)++;
				if (!**dstr)
					(*dstr)--;
				ansi = 1;
				switch (**dstr) {
				case 'h':	/*
						 * hilite 
						 */
					safe_str(ANSI_HILITE, buff, bufc);
					break;
				case 'i':	/*
						 * inverse 
						 */
					safe_str(ANSI_INVERSE, buff, bufc);
					break;
				case 'f':	/*
						 * flash 
						 */
					safe_str(ANSI_BLINK, buff, bufc);
					break;
				case 'n':	/*
						 * normal 
						 */
					safe_str(ANSI_NORMAL, buff, bufc);
					ansi = 0;
					break;
				case 'x':	/*
						 * black fg 
						 */
					safe_str(ANSI_BLACK, buff, bufc);
					break;
				case 'r':	/*
						 * red fg 
						 */
					safe_str(ANSI_RED, buff, bufc);
					break;
				case 'g':	/*
						 * green fg 
						 */
					safe_str(ANSI_GREEN, buff, bufc);
					break;
				case 'y':	/*
						 * yellow fg 
						 */
					safe_str(ANSI_YELLOW, buff, bufc);
					break;
				case 'b':	/*
						 * blue fg 
						 */
					safe_str(ANSI_BLUE, buff, bufc);
					break;
				case 'm':	/*
						 * magenta fg 
						 */
					safe_str(ANSI_MAGENTA, buff, bufc);
					break;
				case 'c':	/*
						 * cyan fg 
						 */
					safe_str(ANSI_CYAN, buff, bufc);
					break;
				case 'w':	/*
						 * white fg 
						 */
					safe_str(ANSI_WHITE, buff, bufc);
					break;
				case 'X':	/*
						 * black bg 
						 */
					safe_str(ANSI_BBLACK, buff, bufc);
					break;
				case 'R':	/*
						 * red bg 
						 */
					safe_str(ANSI_BRED, buff, bufc);
					break;
				case 'G':	/*
						 * green bg 
						 */
					safe_str(ANSI_BGREEN, buff, bufc);
					break;
				case 'Y':	/*
						 * yellow bg 
						 */
					safe_str(ANSI_BYELLOW, buff, bufc);
					break;
				case 'B':	/*
						 * blue bg 
						 */
					safe_str(ANSI_BBLUE, buff, bufc);
					break;
				case 'M':	/*
						 * magenta bg 
						 */
					safe_str(ANSI_BMAGENTA, buff, bufc);
					break;
				case 'C':	/*
						 * cyan bg 
						 */
					safe_str(ANSI_BCYAN, buff, bufc);
					break;
				case 'W':	/*
						 * white bg 
						 */
					safe_str(ANSI_BWHITE, buff, bufc);
					break;
				default:
					safe_chr(**dstr, buff, bufc);
				}
				break;
			case 'r':	/*
					 * Carriage return 
					 */
			case 'R':
				safe_str((char *)"\r\n", buff, bufc);
				break;
			case 't':	/*
					 * Tab 
					 */
			case 'T':
				safe_chr('\t', buff, bufc);
				break;
			case 'B':	/*
					 * Blank 
					 */
			case 'b':
				safe_chr(' ', buff, bufc);
				break;
			case '0':	/*
					 * Command argument number N 
					 */
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				i = (**dstr - '0');
				if ((i < ncargs) && (cargs[i] != NULL))
					safe_str(cargs[i], buff, bufc);
				break;
			case 'V':	/*
					 * Variable attribute 
					 */
			case 'v':
				(*dstr)++;
				ch = ToUpper(**dstr);
				if (!**dstr)
					(*dstr)--;
				if ((ch < 'A') || (ch > 'Z'))
					break;
				i = 100 + ch - 'A';
				atr_gotten = atr_pget(player, i, &aowner,
						      &aflags);
				safe_str(atr_gotten, buff, bufc);
				free_lbuf(atr_gotten);
				break;
			case 'Q':
			case 'q':
				(*dstr)++;
				i = (**dstr - '0');
				if ((i >= 0) && (i <= 9) &&
				    mudstate.global_regs[i]) {
					safe_str(mudstate.global_regs[i],
						 buff, bufc);
				}
				if (!**dstr)
					(*dstr)--;
				break;
			case 'O':	/*
					 * Objective pronoun 
					 */
			case 'o':
				if (gender < 0)
					gender = get_gender(cause);
				if (!gender)
					tbuf = Name(cause);
				else
					tbuf = (char *)obj[gender];
				safe_str(tbuf, buff, bufc);
				break;
			case 'P':	/*
					 * Personal pronoun 
					 */
			case 'p':
				if (gender < 0)
					gender = get_gender(cause);
				if (!gender) {
					safe_str(Name(cause), buff, bufc);
					safe_chr('s', buff, bufc);
				} else {
					safe_str((char *)poss[gender],
						 buff, bufc);
				}
				break;
			case 'S':	/*
					 * Subjective pronoun 
					 */
			case 's':
				if (gender < 0)
					gender = get_gender(cause);
				if (!gender)
					tbuf = Name(cause);
				else
					tbuf = (char *)subj[gender];
				safe_str(tbuf, buff, bufc);
				break;
			case 'A':	/*
					 * Absolute posessive 
					 */
			case 'a':	/*
					 * idea from Empedocles 
					 */
				if (gender < 0)
					gender = get_gender(cause);
				if (!gender) {
					safe_str(Name(cause), buff, bufc);
					safe_chr('s', buff, bufc);
				} else {
					safe_str((char *)absp[gender],
						 buff, bufc);
				}
				break;
			case '#':	/*
					 * Invoker DB number 
					 */
				tbuf = alloc_sbuf("exec.invoker");
				sprintf(tbuf, "#%d", cause);
				safe_str(tbuf, buff, bufc);
				free_sbuf(tbuf);
				break;
			case '!':	/*
					 * Executor DB number 
					 */
				tbuf = alloc_sbuf("exec.executor");
				sprintf(tbuf, "#%d", player);
				safe_str(tbuf, buff, bufc);
				free_sbuf(tbuf);
				break;
			case 'N':	/*
					 * Invoker name 
					 */
			case 'n':
				safe_str(Name(cause), buff, bufc);
				break;
			case 'L':	/*
					 * Invoker location db# 
					 */
			case 'l':
				if (!(eval & EV_NO_LOCATION)) {
					tbuf = alloc_sbuf("exec.exloc");
					sprintf(tbuf, "#%d", where_is(cause));
					safe_str(tbuf, buff, bufc);
					free_sbuf(tbuf);
				}
				
				break;
			default:	/*
					 * Just copy 
					 */
				safe_chr(**dstr, buff, bufc);
			}
			if (isupper(savec))
				*savepos = ToUpper(*savepos);
			break;
		case '(':
			/*
			 * Arglist start.  See if what precedes is a * * *
			 * function. * If so, execute it if we should. 
			 */

			at_space = 0;
			if (!(eval & EV_FCHECK)) {
				safe_chr('(', buff, bufc);
				break;
			}
			/*
			 * Load an sbuf with an uppercase version of the func
			 * * * * * name, and see if the func exists.  Trim * * 
			 * trailing * * spaces from the name if configured. 
			 */

			**bufc = '\0';
			tbufc = tbuf = alloc_sbuf("exec.tbuf");
			safe_sb_str(oldp, tbuf, &tbufc);
			*tbufc = '\0';
			if (mudconf.space_compress) {
				while ((--tbufc >= tbuf) && isspace(*tbufc)) ;
				tbufc++;
				*tbufc = '\0';
			}
			for (tbufc = tbuf; *tbufc; tbufc++)
				*tbufc = ToLower(*tbufc);
			fp = (FUN *) hashfind(tbuf, &mudstate.func_htab);

			/*
			 * If not a builtin func, check for global func 
			 */

			ufp = NULL;
			if (fp == NULL) {
				ufp = (UFUN *) hashfind(tbuf,
						      &mudstate.ufunc_htab);
			}
			/*
			 * Do the right thing if it doesn't exist 
			 */

			if (!fp && !ufp) {
				if (eval & EV_FMAND) {
					*bufc = oldp;
					safe_str((char *)"#-1 FUNCTION (",
						 buff, bufc);
					safe_str(tbuf, buff, bufc);
					safe_str((char *)") NOT FOUND",
						 buff, bufc);
					alldone = 1;
				} else {
					safe_chr('(', buff, bufc);
				}
				free_sbuf(tbuf);
				eval &= ~EV_FCHECK;
				break;
			}
			free_sbuf(tbuf);

			/*
			 * Get the arglist and count the number of args * Neg 
			 * 
			 * *  * *  * * # of args means catenate subsequent
			 * args 
			 */

			if (ufp)
				nfargs = NFARGS;
			else if (fp->nargs < 0)
				nfargs = -fp->nargs;
			else
				nfargs = NFARGS;
			tstr = *dstr;
			if (fp && (fp->flags & FN_NO_EVAL))
				feval = (eval & ~EV_EVAL) | EV_STRIP_ESC;
			else
				feval = eval;
			*dstr = parse_arglist(player, cause, *dstr + 1,
					      ')', feval, fargs, nfargs,
					      cargs, ncargs);

			/*
			 * If no closing delim, just insert the '(' and * * * 
			 * 
			 * * continue normally 
			 */

			if (!*dstr) {
				*dstr = tstr;
				safe_chr(**dstr, buff, bufc);
				for (i = 0; i < nfargs; i++)
					if (fargs[i] != NULL)
						free_lbuf(fargs[i]);
				eval &= ~EV_FCHECK;
				break;
			}
			/*
			 * Count number of args returned 
			 */

			(*dstr)--;
			j = 0;
			for (i = 0; i < nfargs; i++)
				if (fargs[i] != NULL)
					j = i + 1;
			nfargs = j;

			/*
			 * If it's a user-defined function, perform it now. 
			 */

			if (ufp) {
				mudstate.func_nest_lev++;
				if (!check_access(player, ufp->perms)) {
					safe_str("#-1 PERMISSION DENIED", buff, &oldp);
					*bufc = oldp;
				} else {
					tstr = atr_get(ufp->obj, ufp->atr,
						       &aowner, &aflags);
					if (ufp->flags & FN_PRIV)
						i = ufp->obj;
					else
						i = player;
					str = tstr;
					
					if (ufp->flags & FN_PRES) {
						for (j = 0; j < MAX_GLOBAL_REGS; j++) {
							if (!mudstate.global_regs[j])
								preserve[j] = NULL;
							else {
								preserve[j] = alloc_lbuf("eval_regs");
								StringCopy(preserve[j], mudstate.global_regs[j]);
							}
						}
					}
					
					exec(buff, &oldp, 0, i, cause, feval,
					     &str, fargs, nfargs);
					*bufc = oldp;
					
					if (ufp->flags & FN_PRES) {
						for (j = 0; j < MAX_GLOBAL_REGS; j++) {
							if (preserve[j]) {
								if (!mudstate.global_regs[j])
									mudstate.global_regs[j] = alloc_lbuf("eval_regs");
								StringCopy(mudstate.global_regs[j], preserve[j]);
								free_lbuf(preserve[j]);
							} else {
								if (mudstate.global_regs[j])
									*(mudstate.global_regs[i]) = '\0';
							}
						}
					}

					free_lbuf(tstr);
				}

				/*
				 * Return the space allocated for the args 
				 */

				mudstate.func_nest_lev--;
				for (i = 0; i < nfargs; i++)
					if (fargs[i] != NULL)
						free_lbuf(fargs[i]);
				eval &= ~EV_FCHECK;
				break;
			}
			/*
			 * If the number of args is right, perform the func.
			 * Otherwise return an error message.  Note
			 * that parse_arglist returns zero args as one
			 * null arg, so we have to handle that case
			 * specially. 
			 */

			if ((fp->nargs == 0) && (nfargs == 1)) {
				if (!*fargs[0]) {
					free_lbuf(fargs[0]);
					fargs[0] = NULL;
					nfargs = 0;
				}
			}
			if ((nfargs == fp->nargs) ||
			    (nfargs == -fp->nargs) ||
			    (fp->flags & FN_VARARGS)) {

				/*
				 * Check recursion limit 
				 */

				mudstate.func_nest_lev++;
				mudstate.func_invk_ctr++;
				if (mudstate.func_nest_lev >=
				    mudconf.func_nest_lim) {
					safe_str("#-1 FUNCTION RECURSION LIMIT EXCEEDED", buff, bufc);
				} else if (mudstate.func_invk_ctr ==
					   mudconf.func_invk_lim) {
					safe_str("#-1 FUNCTION INVOCATION LIMIT EXCEEDED", buff, bufc);
				} else if (!check_access(player, fp->perms)) {
					safe_str("#-1 PERMISSION DENIED", buff, &oldp);
					*bufc = oldp;
				} else if (mudstate.func_invk_ctr <
					   mudconf.func_invk_lim) {
					fp->fun(buff, &oldp, player, cause,
					      fargs, nfargs, cargs, ncargs);
					*bufc = oldp;
				} else {
					**bufc = '\0';
				}
				mudstate.func_nest_lev--;
			} else {
				*bufc = oldp;
				tstr = alloc_sbuf("exec.funcargs");
				sprintf(tstr, "%d", fp->nargs);
				safe_str((char *)"#-1 FUNCTION (",
					 buff, bufc);
				safe_str((char *)fp->name, buff, bufc);
				safe_str((char *)") EXPECTS ",
					 buff, bufc);
				safe_str(tstr, buff, bufc);
				safe_str((char *)" ARGUMENTS",
					 buff, bufc);
				free_sbuf(tstr);
			}

			/*
			 * Return the space allocated for the arguments 
			 */

			for (i = 0; i < nfargs; i++)
				if (fargs[i] != NULL)
					free_lbuf(fargs[i]);
			eval &= ~EV_FCHECK;
			break;
		default:
			/*
			 * A mundane character.  Just copy it 
			 */

			at_space = 0;
			safe_chr(**dstr, buff, bufc);
		}
		(*dstr)++;
	}

	/*
	 * If we're eating spaces, and the last thing was a space, eat it
	 * up. Complicated by the fact that at_space is initially
	 * true. So check to see if we actually put something in the
	 * buffer, too. 
	 */


	if (mudconf.space_compress && at_space && !(eval & EV_NO_COMPRESS)
	    && (start != *bufc))
		(*bufc)--;

	/*
	 * The ansi() function knows how to take care of itself. However, 
	 * if the player used a %c sub in the string, and hasn't yet
	 * terminated the color with a %cn yet, we'll have to do it for 
	 * them. 
	 */

	if (ansi == 1)
		safe_str(ANSI_NORMAL, buff, bufc);

	**bufc = '\0';

	/*
	 * Report trace information 
	 */

	if (realbuff) {
		**bufc = '\0';
		*bufc = realbp;
		safe_str(buff, realbuff, bufc);
		free(buff);
		buff = realbuff;
	}
	
	if (is_trace) {
		tcache_add(savestr, start);
		save_count = tcache_count - mudconf.trace_limit;;
		if (is_top || !mudconf.trace_topdown)
			tcache_finish(player);
		if (is_top && (save_count > 0)) {
			tbuf = alloc_mbuf("exec.trace_diag");
			sprintf(tbuf,
				"%d lines of trace output discarded.",
				save_count);
			notify(player, tbuf);
			free_mbuf(tbuf);
		}
	}
}
