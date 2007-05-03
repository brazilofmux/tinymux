
/*
 * look.c -- commands which look at things 
 */
/*
 * $Id: look.c,v 1.2 1997/04/16 06:01:13 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "mudconf.h"
#include "config.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "externs.h"
#include "flags.h"
#include "powers.h"
#include "attrs.h"
#include "command.h"
#include "alloc.h"
#include "ansi.h"
#include "dspace.h"

extern void FDECL(ufun, (char *, char *, int, int, int, dbref, dbref));

static void look_exits(player, loc, exit_name)
dbref player, loc;
const char *exit_name;
{
	dbref thing, parent, aowner;
	char *buff, *e, *s, *buff1, *e1;

#ifdef DSPACE
	char *buff2, x, y, z;

#endif

	int foundany, lev, key, aflags;

	/*
	 * make sure location has exits 
	 */

	if (!Good_obj(loc) || !Has_exits(loc))
		return;

	/*
	 * make sure there is at least one visible exit 
	 */

	foundany = 0;
	key = 0;
	if (Dark(loc))
		key |= VE_BASE_DARK;
	ITER_PARENTS(loc, parent, lev) {
		key &= ~VE_LOC_DARK;
		if (Dark(parent))
			key |= VE_LOC_DARK;
		DOLIST(thing, Exits(parent)) {
			if (exit_displayable(thing, player, key)) {
				foundany = 1;
				break;
			}
		}
		if (foundany)
			break;
	}

	if (!foundany)
		return;
#ifdef DSPACE
	if (Dynamic(loc)) {
		x = atoi_clean(atr_get_raw(loc, get_atr("XCOOR")));
		y = atoi_clean(atr_get_raw(loc, get_atr("YCOOR")));
		z = atoi_clean(atr_get_raw(loc, get_atr("ZCOOR")));
	}
#endif
	/*
	 * Display the list of exit names 
	 */

	notify(player, exit_name);
	e = buff = alloc_lbuf("look_exits");
	e1 = buff1 = alloc_lbuf("look_exits2");
	ITER_PARENTS(loc, parent, lev) {
		key &= ~VE_LOC_DARK;
		if (Dark(parent))
			key |= VE_LOC_DARK;
		if (Transparent(loc)) {
			DOLIST(thing, Exits(parent)) {
				if (exit_displayable(thing, player, key)) {
					StringCopy(buff, Name(thing));
					for (e = buff; *e && (*e != ';'); e++) ;
					*e = '\0';
					notify(player, tprintf("%s leads to %s.",
					      buff, Name(Location(thing))));
				}
			}
		} else {
			DOLIST(thing, Exits(parent)) {
				if (exit_displayable(thing, player, key)) {
					e1 = buff1;
					/* Put the exit name in buff1 */
#ifdef DSPACE
					if (Dynamic(loc)) {
						buff2 = alloc_lbuf("look_exits.buff2");
						ufun(buff2, "EXITDARK", x, y, z,
						     thing, player);
						if (!*buff2) {
#endif
							/*
							 * chop off first * * 
							 * 
							 * * exit alias to *
							 * * * display 
							 */

							if (buff != e)
								safe_str((char *)"  ", buff, &e);
							for (s = Name(thing); *s && (*s != ';'); s++)
								safe_chr(*s, buff1, &e1);
#ifdef DSPACE
							free_lbuf(buff2);
						}
					} else {
						if (buff != e)
							safe_str((char *)"  ", buff, &e);
						for (s = Name(thing); *s && (*s != ';'); s++)
							safe_chr(*s, buff1, &e1);
					}
#endif
					*e1 = 0;
					/* Copy the exit name into 'buff' */
					if (Html(player)) {
						/* XXX The exit name needs to be HTML escaped. */
						safe_str((char *) "<a xch_cmd=\"", buff, &e);
						safe_str(buff1, buff, &e);
						safe_str((char *) "\"> ", buff, &e);
						html_escape(buff1, buff, &e);
						safe_str((char *) " </a>", buff, &e);
					} else {
						/* Append this exit to the list */
						safe_str(buff1, buff, &e);
					}
				}
			}
		}
	}

	if (!(Transparent(loc))) {
		safe_str((char *)"\r\n", buff, &e);
		*e = 0;
		notify_html(player, buff);
	}
	free_lbuf(buff);
	free_lbuf(buff1);
}

#define CONTENTS_LOCAL  0
#define CONTENTS_NESTED 1
#define CONTENTS_REMOTE 2

static void look_contents(player, loc, contents_name, style)
dbref player, loc;
const char *contents_name;
int style;
{
	dbref thing;
	dbref can_see_loc;
	char *buff;
	char *html_buff, *html_cp;
	char remote_num[32];

	html_buff = html_cp = alloc_lbuf("look_contents");

	/*
	 * check to see if he can see the location 
	 */

	can_see_loc = (!Dark(loc) ||
		       (mudconf.see_own_dark && Examinable(player, loc)));

	/*
	 * check to see if there is anything there 
	 */

	DOLIST(thing, Contents(loc)) {
		if (can_see(player, thing, can_see_loc)) {

			/*
			 * something exists!  show him everything 
			 */

			notify(player, contents_name);
			DOLIST(thing, Contents(loc)) {
				if (can_see(player, thing, can_see_loc)) {
					buff = unparse_object(player, thing, 1);
					html_cp = html_buff;
					if (Html(player)) {
					    safe_str("<a xch_cmd=\"look ", html_buff, &html_cp);
					    switch (style) {
					      case CONTENTS_LOCAL:
						safe_str(Name(thing), html_buff, &html_cp);
						break;
					      case CONTENTS_NESTED:
						safe_str(Name(Location(thing)), html_buff, &html_cp);
						safe_str("'s ", html_buff, &html_cp);
						safe_str(Name(thing), html_buff, &html_cp);
						break;
					      case CONTENTS_REMOTE:
						sprintf(remote_num, "#%d", thing);
						safe_str(remote_num, html_buff, &html_cp);
						break;
					      default:
						break;
					    }
					    safe_str("\">", html_buff, &html_cp);
					    html_escape(buff, html_buff, &html_cp);
					    safe_str("</a>\r\n", html_buff, &html_cp);
					    *html_cp = 0;
					    notify_html(player, html_buff);
					} else {
					    notify(player, buff);
					}
					free_lbuf(buff);

				}
			}
			break;	/*
				 * we're done 
				 */
		}
	}
	free_lbuf(html_buff);
}


static void view_atr(player, thing, ap, text, aowner, aflags, skip_tag)
dbref player, thing, aowner;
int aflags, skip_tag;
ATTR *ap;
char *text;
{
	char *buf;
	char xbuf[6];
	char *xbufp;
	BOOLEXP *bool;

	if (ap->flags & AF_IS_LOCK) {
		bool = parse_boolexp(player, text, 1);
		text = unparse_boolexp(player, bool);
		free_boolexp(bool);
	}
	/*
	 * If we don't control the object or own the attribute, hide the * *
	 * * * attr owner and flag info. 
	 */

	if (!Controls(player, thing) && (Owner(player) != aowner)) {
		if (skip_tag && (ap->number == A_DESC))
			buf = text;
		else
			buf = tprintf("\033[1m%s:\033[0m %s", ap->name, text);
		notify(player, buf);
		return;
	}
	/*
	 * Generate flags 
	 */

	xbufp = xbuf;
	if (aflags & AF_LOCK)
		*xbufp++ = '+';
	if (aflags & AF_NOPROG)
		*xbufp++ = '$';
	if (aflags & AF_PRIVATE)
		*xbufp++ = 'I';
	if (aflags & AF_REGEXP)
		*xbufp++ = 'R';
	if (aflags & AF_VISUAL)
		*xbufp++ = 'V';
	if (aflags & AF_MDARK)
		*xbufp++ = 'M';
	if (aflags & AF_WIZARD)
		*xbufp++ = 'W';

	*xbufp = '\0';

	if ((aowner != Owner(thing)) && (aowner != NOTHING)) {
		buf = tprintf("\033[1m%s [#%d%s]:\033[0m %s",
			      ap->name, aowner, xbuf, text);
	} else if (*xbuf) {
		buf = tprintf("\033[1m%s [%s]:\033[0m %s",
			      ap->name, xbuf, text);
	} else if (!skip_tag || (ap->number != A_DESC)) {
		buf = tprintf("\033[1m%s:\033[0m %s", ap->name, text);
	} else {
		buf = text;
	}
	notify(player, buf);
}

static void look_atrs1(player, thing, othing, check_exclude, hash_insert)
dbref player, thing, othing;
int check_exclude, hash_insert;
{
	dbref aowner;
	int ca, aflags;
	ATTR *attr, *cattr;
	char *as, *buf;

	cattr = (ATTR *)malloc(sizeof(ATTR));
	for (ca = atr_head(thing, &as); ca; ca = atr_next(&as)) {
		if ((ca == A_DESC) || (ca == A_LOCK))
			continue;
		attr = atr_num(ca);
		if (!attr)
			continue;

		bcopy((char *)attr, (char *)cattr, sizeof(ATTR));

		/*
		 * Should we exclude this attr? 
		 */

		if (check_exclude &&
		    ((attr->flags & AF_PRIVATE) ||
		     nhashfind(ca, &mudstate.parent_htab)))
			continue;

		buf = atr_get(thing, ca, &aowner, &aflags);
		if (Read_attr(player, othing, attr, aowner, aflags)) {
                       /* check_zone/atr_num overwrites attr!! */

                       if (attr->number != cattr->number)
                               bcopy((char *)cattr, (char *)attr, sizeof(ATTR));

			if (!(check_exclude && (aflags & AF_PRIVATE))) {
				if (hash_insert)
					nhashadd(ca, (int *)attr,
						 &mudstate.parent_htab);
				view_atr(player, thing, attr, buf,
					 aowner, aflags, 0);
			}
		}
		free_lbuf(buf);
	}
	free(cattr);
}

static void look_atrs(player, thing, check_parents)
dbref player, thing;
int check_parents;
{
	dbref parent;
	int lev, check_exclude, hash_insert;

	if (!check_parents) {
		look_atrs1(player, thing, thing, 0, 0);
	} else {
		hash_insert = 1;
		check_exclude = 0;
		nhashflush(&mudstate.parent_htab, 0);
		ITER_PARENTS(thing, parent, lev) {
			if (!Good_obj(Parent(parent)))
				hash_insert = 0;
			look_atrs1(player, parent, thing,
				   check_exclude, hash_insert);
			check_exclude = 1;
		}
	}
}

static void look_simple(player, thing, obey_terse)
dbref player, thing;
int obey_terse;
{
	int pattr;
	char *buff;

	/*
	 * Only makes sense for things that can hear 
	 */

	if (!Hearer(player))
		return;

	/*
	 * Get the name and db-number if we can examine it. 
	 */

	if (Examinable(player, thing)) {
		buff = unparse_object(player, thing, 1);
		notify(player, buff);
		free_lbuf(buff);

	}
	pattr = (obey_terse && Terse(player)) ? 0 : A_DESC;
	did_it(player, thing, pattr, "You see nothing special.",
	       A_ODESC, NULL, A_ADESC, (char **)NULL, 0);

	if (!mudconf.quiet_look && (!Terse(player) || mudconf.terse_look)) {
		look_atrs(player, thing, 0);
	}
}

static void
show_a_desc(player, loc)
dbref player, loc;
{
	char *got2;
	dbref aowner;
	int aflags, indent = 0;
	
	indent = (isRoom(loc) && mudconf.indent_desc && atr_get_raw(loc, A_DESC));
	
	if (Html(player)) {
		got2 = atr_pget(loc, A_HTDESC, &aowner, &aflags);
		if (*got2)
			did_it(player, loc, A_HTDESC, NULL, A_ODESC, NULL,
			       A_ADESC, (char **) NULL, 0);
		else {
		    	if (indent)
		    		raw_notify_newline(player); 
		    	did_it(player, loc, A_DESC, NULL, A_ODESC, NULL,
			       A_ADESC, (char **) NULL, 0);
			if (indent)
				raw_notify_newline(player);
		}
		free_lbuf(got2);
	}
	else {
		if (indent)
			raw_notify_newline(player);
		did_it(player, loc, A_DESC, NULL, A_ODESC, NULL,
		       A_ADESC, (char **) NULL, 0);
		if (indent)
			raw_notify_newline(player);
	}
}

static void show_desc(player, loc, key)
dbref player, loc;
int key;
{
	char *got;
	dbref aowner;
	int aflags;

	if ((key & LK_OBEYTERSE) && Terse(player))
		did_it(player, loc, 0, NULL, A_ODESC, NULL,
		       A_ADESC, (char **)NULL, 0);
	else if ((Typeof(loc) != TYPE_ROOM) && (key & LK_IDESC)) {
		if (*(got = atr_pget(loc, A_IDESC, &aowner, &aflags)))
			did_it(player, loc, A_IDESC, NULL, A_ODESC, NULL,
			       A_ADESC, (char **)NULL, 0);
		else
			show_a_desc(player, loc);
		free_lbuf(got);
	} else {
		show_a_desc(player, loc);
	}
}

void look_in(player, loc, key)
dbref player, loc;
int key;
{
	int pattr, oattr, aattr, is_terse, showkey;
	char *buff;

	is_terse = (key & LK_OBEYTERSE) ? Terse(player) : 0;

	/*
	 * Only makes sense for things that can hear 
	 */

	if (!Hearer(player))
		return;

	/* If he needs the VMRL URL, send it: */
	if (key & LK_SHOWVRML)
		show_vrml_url(player, loc);

	/*
	 * tell him the name, and the number if he can link to it 
	 */

	buff = unparse_object(player, loc, 1);
	notify(player, buff);
	free_lbuf(buff);


	if (!Good_obj(loc))
		return;		/*
				 * If we went to NOTHING et al,  skip the * * 
				 * 
				 * * rest 
				 */

	/*
	 * tell him the description 
	 */

	showkey = 0;
	if (loc == Location(player))
		showkey |= LK_IDESC;
	if (key & LK_OBEYTERSE)
		showkey |= LK_OBEYTERSE;
	show_desc(player, loc, showkey);

	/*
	 * tell him the appropriate messages if he has the key 
	 */

	if (Typeof(loc) == TYPE_ROOM) {
		if (could_doit(player, loc, A_LOCK)) {
			pattr = A_SUCC;
			oattr = A_OSUCC;
			aattr = A_ASUCC;
		} else {
			pattr = A_FAIL;
			oattr = A_OFAIL;
			aattr = A_AFAIL;
		}
		if (is_terse)
			pattr = 0;
		did_it(player, loc, pattr, NULL, oattr, NULL,
		       aattr, (char **)NULL, 0);
	}
	/*
	 * tell him the attributes, contents and exits 
	 */

	if ((key & LK_SHOWATTR) && !mudconf.quiet_look && !is_terse)
		look_atrs(player, loc, 0);
	if (!is_terse || mudconf.terse_contents)
		look_contents(player, loc, "Contents:", CONTENTS_LOCAL);
	if ((key & LK_SHOWEXIT) && (!is_terse || mudconf.terse_exits))
		look_exits(player, loc, "Obvious exits:");
}

void do_look(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
	dbref thing, loc, look_key;

	look_key = LK_SHOWATTR | LK_SHOWEXIT;
	if (!mudconf.terse_look)
		look_key |= LK_OBEYTERSE;

	loc = Location(player);
	if (!name || !*name) {
		thing = loc;
		if (Good_obj(thing)) {
			if (key & LOOK_OUTSIDE) {
				if ((Typeof(thing) == TYPE_ROOM) ||
				    Opaque(thing)) {
					notify_quiet(player,
						 "You can't look outside.");
					return;
				}
				thing = Location(thing);
			}
			look_in(player, thing, look_key);
		}
		return;
	}
	/*
	 * Look for the target locally 
	 */

	thing = (key & LOOK_OUTSIDE) ? loc : player;
	init_match(thing, name, NOTYPE);
	match_exit_with_parents();
	match_neighbor();
	match_possession();
	if (Long_Fingers(player)) {
		match_absolute();
		match_player();
	}
	match_here();
	match_me();
	match_master_exit();
	thing = match_result();

	/*
	 * Not found locally, check possessive 
	 */

	if (!Good_obj(thing)) {
		thing = match_status(player,
				     match_possessed(player,
				      ((key & LOOK_OUTSIDE) ? loc : player),
						   (char *)name, thing, 0));
	}
	/*
	 * If we found something, go handle it 
	 */

	if (Good_obj(thing)) {
		switch (Typeof(thing)) {
		case TYPE_ROOM:
			look_in(player, thing, look_key);
			break;
		case TYPE_THING:
		case TYPE_PLAYER:
			look_simple(player, thing, !mudconf.terse_look);
			if (!Opaque(thing) &&
			    (!Terse(player) || mudconf.terse_contents)) {
				look_contents(player, thing, "Carrying:", CONTENTS_NESTED);
			}
			break;
		case TYPE_EXIT:
			look_simple(player, thing, !mudconf.terse_look);
			if (Transparent(thing) &&
			    (Location(thing) != NOTHING)) {
				look_key &= ~LK_SHOWATTR;
				look_in(player, Location(thing), look_key);
			}
			break;
		default:
			look_simple(player, thing, !mudconf.terse_look);
		}
	}
}

static void debug_examine(player, thing)
dbref player, thing;
{
	dbref aowner;
	char *buf;
	int aflags, ca;
	BOOLEXP *bool;
	ATTR *attr;
	char *as, *cp;

	notify(player, tprintf("Number  = %d", thing));
	if (!Good_obj(thing))
		return;

	notify(player, tprintf("Name    = %s", Name(thing)));
	notify(player, tprintf("Location= %d", Location(thing)));
	notify(player, tprintf("Contents= %d", Contents(thing)));
	notify(player, tprintf("Exits   = %d", Exits(thing)));
	notify(player, tprintf("Link    = %d", Link(thing)));
	notify(player, tprintf("Next    = %d", Next(thing)));
	notify(player, tprintf("Owner   = %d", Owner(thing)));
	notify(player, tprintf("Pennies = %d", Pennies(thing)));
	notify(player, tprintf("Zone    = %d", Zone(thing)));
	buf = flag_description(player, thing);
	notify(player, tprintf("Flags   = %s", buf));
	free_mbuf(buf);
	buf = power_description(player, thing);
	notify(player, tprintf("Powers  = %s", buf));
	free_mbuf(buf);
	buf = atr_get(thing, A_LOCK, &aowner, &aflags);
	bool = parse_boolexp(player, buf, 1);
	free_lbuf(buf);
	notify(player, tprintf("Lock    = %s", unparse_boolexp(player, bool)));
	free_boolexp(bool);

	buf = alloc_lbuf("debug_dexamine");
	cp = buf;
	safe_str((char *)"Attr list: ", buf, &cp);

	for (ca = atr_head(thing, &as); ca; ca = atr_next(&as)) {
		attr = atr_num(ca);
		if (!attr)
			continue;

		atr_get_info(thing, ca, &aowner, &aflags);
		if (Read_attr(player, thing, attr, aowner, aflags)) {
			if (attr) {	/*
					 * Valid attr. 
					 */
				safe_str((char *)attr->name, buf, &cp);
				safe_chr(' ', buf, &cp);
			} else {
				safe_str(tprintf("%d ", ca), buf, &cp);
			}
		}
	}
	*cp = '\0';
	notify(player, buf);
	free_lbuf(buf);

	for (ca = atr_head(thing, &as); ca; ca = atr_next(&as)) {
		attr = atr_num(ca);
		if (!attr)
			continue;

		buf = atr_get(thing, ca, &aowner, &aflags);
		if (Read_attr(player, thing, attr, aowner, aflags))
			view_atr(player, thing, attr, buf, aowner, aflags, 0);
		free_lbuf(buf);
	}
}

static void exam_wildattrs(player, thing, do_parent)
dbref player, thing;
int do_parent;
{
	int atr, aflags, got_any;
	char *buf;
	dbref aowner;
	ATTR *ap;

	got_any = 0;
	for (atr = olist_first(); atr != NOTHING; atr = olist_next()) {
		ap = atr_num(atr);
		if (!ap)
			continue;

		if (do_parent && !(ap->flags & AF_PRIVATE))
			buf = atr_pget(thing, atr, &aowner, &aflags);
		else
			buf = atr_get(thing, atr, &aowner, &aflags);

		/*
		 * Decide if the player should see the attr: * If obj is * *
		 * * Examinable and has rights to see, yes. * If a player and 
		 * *  *  * * has rights to see, yes... *   except if faraway, 
		 * * * * attr=DESC, and *   remote DESC-reading is not turned 
		 * on. *  *  * *  * * If I own the attrib and have rights to
		 * see, yes... * * * * except if faraway, attr=DESC, and *
		 * remote * DESC-reading * * is not turned on. 
		 */

		if (Examinable(player, thing) &&
		    Read_attr(player, thing, ap, aowner, aflags)) {
			got_any = 1;
			view_atr(player, thing, ap, buf,
				 aowner, aflags, 0);
		} else if ((Typeof(thing) == TYPE_PLAYER) &&
			   Read_attr(player, thing, ap, aowner, aflags)) {
			got_any = 1;
			if (aowner == Owner(player)) {
				view_atr(player, thing, ap, buf,
					 aowner, aflags, 0);
			} else if ((atr == A_DESC) &&
				   (mudconf.read_rem_desc ||
				    nearby(player, thing))) {
				show_desc(player, thing, 0);
			} else if (atr != A_DESC) {
				view_atr(player, thing, ap, buf,
					 aowner, aflags, 0);
			} else {
				notify(player,
				       "<Too far away to get a good look>");
			}
		} else if (Read_attr(player, thing, ap, aowner, aflags)) {
			got_any = 1;
			if (aowner == Owner(player)) {
				view_atr(player, thing, ap, buf,
					 aowner, aflags, 0);
			} else if ((atr == A_DESC) &&
				   (mudconf.read_rem_desc ||
				    nearby(player, thing))) {
				show_desc(player, thing, 0);
			} else if (nearby(player, thing)) {
				view_atr(player, thing, ap, buf,
					 aowner, aflags, 0);
			} else {
				notify(player,
				       "<Too far away to get a good look>");
			}
		}
		free_lbuf(buf);
	}
	olist_init();
	if (!got_any)
		notify_quiet(player, "No matching attributes found.");

}

void do_examine(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
	dbref thing, content, exit, aowner, loc;
	char savec;
	char *temp, *buf, *buf2;
	BOOLEXP *bool;
	int control, aflags, do_parent;

	/*
	 * This command is pointless if the player can't hear. 
	 */

	if (!Hearer(player))
		return;

	do_parent = key & EXAM_PARENT;
	thing = NOTHING;
	if (!name || !*name) {
		if ((thing = Location(player)) == NOTHING)
			return;
	} else {
		/*
		 * Check for obj/attr first. 
		 */

		if (parse_attrib_wild(player, name, &thing, do_parent, 1, 0)) {
			exam_wildattrs(player, thing, do_parent);
			return;
		}
		/*
		 * Look it up 
		 */

		init_match(player, name, NOTYPE);
		match_everything(MAT_EXIT_PARENTS);
		thing = noisy_match_result();
		if (!Good_obj(thing))
			return;
	}

	/*
	 * Check for the /debug switch 
	 */

	if (key == EXAM_DEBUG) {
		if (!Examinable(player, thing)) {
			notify_quiet(player, "Permission denied.");
		} else {
			debug_examine(player, thing);
		}
		return;
	}
	control = (Examinable(player, thing) || Link_exit(player, thing));

	if (control) {
		buf2 = unparse_object(player, thing, 0);
		notify(player, buf2);
		free_lbuf(buf2);
		if (mudconf.ex_flags) {
			buf2 = flag_description(player, thing);
			notify(player, buf2);
			free_mbuf(buf2);
		}
	} else {
		if ((key == EXAM_DEFAULT) && !mudconf.exam_public) {
			if (mudconf.read_rem_name) {
				buf2 = alloc_lbuf("do_examine.pub_name");
				StringCopy(buf2, Name(thing));
				notify(player,
				       tprintf("%s is owned by %s",
					       buf2, Name(Owner(thing))));
				free_lbuf(buf2);
			} else {
				notify(player,
				       tprintf("Owned by %s",
					       Name(Owner(thing))));
			}
			return;
		}
	}

	temp = alloc_lbuf("do_examine.info");

	if (control || mudconf.read_rem_desc || nearby(player, thing)) {
		temp = atr_get_str(temp, thing, A_DESC, &aowner, &aflags);
		if (*temp) {
			if (Examinable(player, thing) ||
			    (aowner == Owner(player))) {
				view_atr(player, thing, atr_num(A_DESC), temp,
					 aowner, aflags, 1);
			} else {
				show_desc(player, thing, 0);
			}
		}
	} else {
		notify(player, "<Too far away to get a good look>");
	}

	if (control) {

		/*
		 * print owner, key, and value 
		 */

		savec = mudconf.many_coins[0];
		mudconf.many_coins[0] =
			(islower(savec) ? toupper(savec) : savec);
		buf2 = atr_get(thing, A_LOCK, &aowner, &aflags);
		bool = parse_boolexp(player, buf2, 1);
		buf = unparse_boolexp(player, bool);
		free_boolexp(bool);
		StringCopy(buf2, Name(Owner(thing)));
		notify(player,
		       tprintf("Owner: %s  Key: %s %s: %d", buf2, buf,
			       mudconf.many_coins, Pennies(thing)));
		free_lbuf(buf2);
		mudconf.many_coins[0] = savec;

		if (mudconf.have_zones) {
			buf2 = unparse_object(player, Zone(thing), 0);
			notify(player, tprintf("Zone: %s", buf2));
			free_lbuf(buf2);
		}
		/*
		 * print parent 
		 */

		loc = Parent(thing);
		if (loc != NOTHING) {
			buf2 = unparse_object(player, loc, 0);
			notify(player, tprintf("Parent: %s", buf2));
			free_lbuf(buf2);
		}
		buf2 = power_description(player, thing);
		notify(player, buf2);
		free_mbuf(buf2);

	}
	if (key != EXAM_BRIEF)
		look_atrs(player, thing, do_parent);

	/*
	 * show him interesting stuff 
	 */

	if (control) {

		/*
		 * Contents 
		 */

		if (Contents(thing) != NOTHING) {
			notify(player, "Contents:");
			DOLIST(content, Contents(thing)) {
				buf2 = unparse_object(player, content, 0);
				notify(player, buf2);
				free_lbuf(buf2);
			}
		}
		/*
		 * Show stuff that depends on the object type 
		 */

		switch (Typeof(thing)) {
		case TYPE_ROOM:

			/*
			 * tell him about exits 
			 */

			if (Exits(thing) != NOTHING) {
				notify(player, "Exits:");
				DOLIST(exit, Exits(thing)) {
					buf2 = unparse_object(player, exit, 0);
					notify(player, buf2);
					free_lbuf(buf2);
				}
			} else {
				notify(player, "No exits.");
			}

			/*
			 * print dropto if present 
			 */

			if (Dropto(thing) != NOTHING) {
				buf2 = unparse_object(player,
						      Dropto(thing), 0);
				notify(player,
				       tprintf("Dropped objects go to: %s",
					       buf2));
				free_lbuf(buf2);
			}
			break;
		case TYPE_THING:
		case TYPE_PLAYER:

			/*
			 * tell him about exits 
			 */

			if (Exits(thing) != NOTHING) {
				notify(player, "Exits:");
				DOLIST(exit, Exits(thing)) {
					buf2 = unparse_object(player, exit, 0);
					notify(player, buf2);
					free_lbuf(buf2);
				}
			} else {
				notify(player, "No exits.");
			}

			/*
			 * print home 
			 */

			loc = Home(thing);
			buf2 = unparse_object(player, loc, 0);
			notify(player, tprintf("Home: %s", buf2));
			free_lbuf(buf2);

			/*
			 * print location if player can link to it 
			 */

			loc = Location(thing);
			if ((Location(thing) != NOTHING) &&
			    (Examinable(player, loc) ||
			     Examinable(player, thing) ||
			     Linkable(player, loc))) {
				buf2 = unparse_object(player, loc, 0);
				notify(player, tprintf("Location: %s", buf2));
				free_lbuf(buf2);
			}
			break;
		case TYPE_EXIT:
			buf2 = unparse_object(player, Exits(thing), 0);
			notify(player, tprintf("Source: %s", buf2));
			free_lbuf(buf2);

			/*
			 * print destination 
			 */

			switch (Location(thing)) {
			case NOTHING:
				break;
			case HOME:
				notify(player, "Destination: *HOME*");
				break;
			default:
				buf2 = unparse_object(player,
						      Location(thing), 0);
				notify(player, tprintf("Destination: %s", buf2));
				free_lbuf(buf2);
				break;
			}
			break;
		default:
			break;
		}
	} else if (!Opaque(thing) && nearby(player, thing)) {
		if (Has_contents(thing))
			look_contents(player, thing, "Contents:", CONTENTS_REMOTE);
		if (Typeof(thing) != TYPE_EXIT)
			look_exits(player, thing, "Obvious exits:");
	}
	free_lbuf(temp);

	if (!control)
		if (mudconf.read_rem_name) {
			buf2 = alloc_lbuf("do_examine.pub_name");
			StringCopy(buf2, Name(thing));
			notify(player,
			       tprintf("%s is owned by %s",
				       buf2, Name(Owner(thing))));
			free_lbuf(buf2);
		} else {
			notify(player,
			       tprintf("Owned by %s",
				       Name(Owner(thing))));
		}
}

void do_score(player, cause, key)
dbref player, cause;
int key;
{
	notify(player,
	       tprintf("You have %d %s.", Pennies(player),
		       (Pennies(player) == 1) ?
		       mudconf.one_coin : mudconf.many_coins));
}

void do_inventory(player, cause, key)
dbref player, cause;
int key;
{
	dbref thing;
	char *buff, *s, *e;

	thing = Contents(player);
	if (thing == NOTHING) {
		notify(player, "You aren't carrying anything.");
	} else {
		notify(player, "You are carrying:");
		DOLIST(thing, thing) {
			buff = unparse_object(player, thing, 1);
			notify(player, buff);
			free_lbuf(buff);
		}
	}

	thing = Exits(player);
	if (thing != NOTHING) {
		notify(player, "Exits:");
		e = buff = alloc_lbuf("look_exits");
		DOLIST(thing, thing) {

			/*
			 * chop off first exit alias to display 
			 */

			for (s = Name(thing); *s && (*s != ';'); s++)
				safe_chr(*s, buff, &e);
			safe_str((char *)"  ", buff, &e);
		}
		*e = 0;
		notify(player, buff);
		free_lbuf(buff);
	}
	do_score(player, player, 0);
}

void do_entrances(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
	dbref thing, i, j;
	char *exit, *message;
	int control_thing, count, low_bound, high_bound;
	FWDLIST *fp;

	parse_range(&name, &low_bound, &high_bound);
	if (!name || !*name) {
		if (Has_location(player))
			thing = Location(player);
		else
			thing = player;
		if (!Good_obj(thing))
			return;
	} else {
		init_match(player, name, NOTYPE);
		match_everything(MAT_EXIT_PARENTS);
		thing = noisy_match_result();
		if (!Good_obj(thing))
			return;
	}

	if (!payfor(player, mudconf.searchcost)) {
		notify(player,
		       tprintf("You don't have enough %s.",
			       mudconf.many_coins));
		return;
	}
	message = alloc_lbuf("do_entrances");
	control_thing = Examinable(player, thing);
	count = 0;
	for (i = low_bound; i <= high_bound; i++) {
		if (control_thing || Examinable(player, i)) {
			switch (Typeof(i)) {
			case TYPE_EXIT:
				if (Location(i) == thing) {
					exit = unparse_object(player,
							      Exits(i), 0);
					notify(player,
					       tprintf("%s (%s)",
						       exit, Name(i)));
					free_lbuf(exit);
					count++;
				}
				break;
			case TYPE_ROOM:
				if (Dropto(i) == thing) {
					exit = unparse_object(player, i, 0);
					notify(player,
					       tprintf("%s [dropto]", exit));
					free_lbuf(exit);
					count++;
				}
				break;
			case TYPE_THING:
			case TYPE_PLAYER:
				if (Home(i) == thing) {
					exit = unparse_object(player, i, 0);
					notify(player,
					       tprintf("%s [home]", exit));
					free_lbuf(exit);
					count++;
				}
				break;
			}

			/*
			 * Check for parents 
			 */

			if (Parent(i) == thing) {
				exit = unparse_object(player, i, 0);
				notify(player,
				       tprintf("%s [parent]", exit));
				free_lbuf(exit);
				count++;
			}
			/*
			 * Check for forwarding 
			 */

			if (H_Fwdlist(i)) {
				fp = fwdlist_get(i);
				if (!fp)
					continue;
				for (j = 0; j < fp->count; j++) {
					if (fp->data[j] != thing)
						continue;
					exit = unparse_object(player, i, 0);
					notify(player,
					     tprintf("%s [forward]", exit));
					free_lbuf(exit);
					count++;
				}
			}
		}
	}
	free_lbuf(message);
	notify(player, tprintf("%d entrance%s found.", count,
			       (count == 1) ? "" : "s"));
}

/*
 * check the current location for bugs 
 */

static void sweep_check(player, what, key, is_loc)
dbref player, what;
int key, is_loc;
{
	dbref aowner, parent;
	int canhear, cancom, isplayer, ispuppet, isconnected, attr, aflags;
	int is_parent, lev;
	char *buf, *buf2, *bp, *as, *buff, *s;
	ATTR *ap;

	canhear = 0;
	cancom = 0;
	isplayer = 0;
	ispuppet = 0;
	isconnected = 0;
	is_parent = 0;

	if ((key & SWEEP_LISTEN) &&
	    (((Typeof(what) == TYPE_EXIT) || is_loc) && Audible(what))) {
		canhear = 1;
	} else if (key & SWEEP_LISTEN) {
		if (Monitor(what))
			buff = alloc_lbuf("Hearer");
		else
			buff = NULL;

		for (attr = atr_head(what, &as); attr; attr = atr_next(&as)) {
			if (attr == A_LISTEN) {
				canhear = 1;
				break;
			}
			if (Monitor(what)) {
				ap = atr_num(attr);
				if (!ap || (ap->flags & AF_NOPROG))
					continue;

				atr_get_str(buff, what, attr, &aowner,
					    &aflags);

				/*
				 * Make sure we can execute it 
				 */

				if ((buff[0] != AMATCH_LISTEN) ||
				    (aflags & AF_NOPROG))
					continue;

				/*
				 * Make sure there's a : in it 
				 */

				for (s = buff + 1; *s && (*s != ':'); s++) ;
				if (s) {
					canhear = 1;
					break;
				}
			}
		}
		if (buff)
			free_lbuf(buff);
	}
	if ((key & SWEEP_COMMANDS) && (Typeof(what) != TYPE_EXIT)) {

		/*
		 * Look for commands on the object and parents too 
		 */

		ITER_PARENTS(what, parent, lev) {
			if (Commer(parent)) {
				cancom = 1;
				if (lev) {
					is_parent = 1;
					break;
				}
			}
		}
	}
	if (key & SWEEP_CONNECT) {
		if (Connected(what) ||
		    (Puppet(what) && Connected(Owner(what))) ||
		  (mudconf.player_listen && (Typeof(what) == TYPE_PLAYER) &&
		   canhear && Connected(Owner(what))))
			isconnected = 1;
	}
	if (key & SWEEP_PLAYER || isconnected) {
		if (Typeof(what) == TYPE_PLAYER)
			isplayer = 1;
		if (Puppet(what))
			ispuppet = 1;
	}
	if (canhear || cancom || isplayer || ispuppet || isconnected) {
		buf = alloc_lbuf("sweep_check.types");
		bp = buf;

		if (cancom)
			safe_str((char *)"commands ", buf, &bp);
		if (canhear)
			safe_str((char *)"messages ", buf, &bp);
		if (isplayer)
			safe_str((char *)"player ", buf, &bp);
		if (ispuppet) {
			safe_str((char *)"puppet(", buf, &bp);
			safe_str(Name(Owner(what)), buf, &bp);
			safe_str((char *)") ", buf, &bp);
		}
		if (isconnected)
			safe_str((char *)"connected ", buf, &bp);
		if (is_parent)
			safe_str((char *)"parent ", buf, &bp);
		bp[-1] = '\0';
		if (Typeof(what) != TYPE_EXIT) {
			notify(player,
			       tprintf("  %s is listening. [%s]",
				       Name(what), buf));
		} else {
			buf2 = alloc_lbuf("sweep_check.name");
			StringCopy(buf2, Name(what));
			for (bp = buf2; *bp && (*bp != ';'); bp++) ;
			*bp = '\0';
			notify(player,
			     tprintf("  %s is listening. [%s]", buf2, buf));
			free_lbuf(buf2);
		}
		free_lbuf(buf);
	}
}

void do_sweep(player, cause, key, where)
dbref player, cause;
int key;
char *where;
{
	dbref here, sweeploc;
	int where_key, what_key;

	where_key = key & (SWEEP_ME | SWEEP_HERE | SWEEP_EXITS);
	what_key =
		key & (SWEEP_COMMANDS | SWEEP_LISTEN | SWEEP_PLAYER | SWEEP_CONNECT);

	if (where && *where) {
		sweeploc = match_controlled(player, where);
		if (!Good_obj(sweeploc))
			return;
	} else {
		sweeploc = player;
	}

	if (!where_key)
		where_key = -1;
	if (!what_key)
		what_key = -1;
	else if (what_key == SWEEP_VERBOSE)
		what_key = SWEEP_VERBOSE | SWEEP_COMMANDS;

	/*
	 * Check my location.  If I have none or it is dark, check just me. 
	 */

	if (where_key & SWEEP_HERE) {
		notify(player, "Sweeping location...");
		if (Has_location(sweeploc)) {
			here = Location(sweeploc);
			if ((here == NOTHING) ||
			    (Dark(here) && !mudconf.sweep_dark &&
			     !Examinable(player, here))) {
				notify_quiet(player,
					     "Sorry, it is dark here and you can't search for bugs");
				sweep_check(player, sweeploc, what_key, 0);
			} else {
				sweep_check(player, here, what_key, 1);
				for (here = Contents(here); here != NOTHING; here = Next(here))
					sweep_check(player, here, what_key, 0);
			}
		} else {
			sweep_check(player, sweeploc, what_key, 0);
		}
	}
	/*
	 * Check exits in my location 
	 */

	if ((where_key & SWEEP_EXITS) && Has_location(sweeploc)) {
		notify(player, "Sweeping exits...");
		for (here = Exits(Location(sweeploc)); here != NOTHING; here = Next(here))
			sweep_check(player, here, what_key, 0);
	}
	/*
	 * Check my inventory 
	 */

	if ((where_key & SWEEP_ME) && Has_contents(sweeploc)) {
		notify(player, "Sweeping inventory...");
		for (here = Contents(sweeploc); here != NOTHING; here = Next(here))
			sweep_check(player, here, what_key, 0);
	}
	/*
	 * Check carried exits 
	 */

	if ((where_key & SWEEP_EXITS) && Has_exits(sweeploc)) {
		notify(player, "Sweeping carried exits...");
		for (here = Exits(sweeploc); here != NOTHING; here = Next(here))
			sweep_check(player, here, what_key, 0);
	}
	notify(player, "Sweep complete.");
}

/*
 * Output the sequence of commands needed to duplicate the specified
 * * object.  If you're moving things to another system, your milage
 * * will almost certainly vary.  (i.e. different flags, etc.)
 */

extern NAMETAB indiv_attraccess_nametab[];

void do_decomp(player, cause, key, name, qual)
dbref player, cause;
int key;
char *name, *qual;
{
	BOOLEXP *bool;
	char *got, *thingname, *as, *ltext, *buff, *s;
	dbref aowner, thing;
	int val, aflags, ca, atr;
	ATTR *attr;
	NAMETAB *np;

	/* Check for obj/attr first */
	
	if (parse_attrib_wild(player, name, &thing, 0, 1, 0)) {
		buff = alloc_mbuf("do_decomp.attr_name");
		thingname = alloc_lbuf("do_decomp");
		if (key & DECOMP_DBREF) {
			sprintf(thingname, "%d", (int)thing);
		} else {
			StringCopy(thingname, Name(thing));
		}
		for (atr = olist_first(); atr != NOTHING; atr = olist_next()) {
			if ((atr == A_NAME || atr == A_LOCK))
				continue;
			attr = atr_num(atr);
			if (!attr)
				continue;
			
			got = atr_get(thing, atr, &aowner, &aflags);
			if (Read_attr(player, thing, attr, aowner, aflags)) {
				if (attr->flags & AF_IS_LOCK) {
					bool = parse_boolexp(player, got, 1);
					ltext = unparse_boolexp_decompile(player, bool);
					free_boolexp(bool);
					notify(player, tprintf("@lock/%s %s=%s",
						  attr->name, strip_ansi(thingname), ltext));
				} else {
					StringCopy(buff, attr->name);
					for (s = thingname; *s; s++) {
						if (*s == EXIT_DELIMITER) {
							*s = '\0';
							break;
						}
					}
					notify(player, tprintf("%c%s %s=%s",
						((atr < A_USER_START) ?
						'@' : '&'),
						buff, strip_ansi(thingname),
						got));
						
					if (aflags & AF_LOCK) {
						notify(player, tprintf("@lock %s/%s",
							strip_ansi(thingname), buff));
					}
					
					for (np = indiv_attraccess_nametab;
					     np->name;
					     np++) {
	
						if ((aflags & np->flag) &&
						    check_access(player, np->perm) &&
						    (!(np->perm & CA_NO_DECOMP))) {
	
							notify(player,
							  tprintf("@set %s/%s = %s",
							      strip_ansi(thingname),
								  buff,
								  np->name));
						}
					}
				}
			}
			free_lbuf(got);
		}
		free_mbuf(buff);
		free_lbuf(thingname);
		return;
	}
					
	
	init_match(player, name, TYPE_THING);
	match_everything(MAT_EXIT_PARENTS);
	thing = noisy_match_result();

	/*
	 * get result 
	 */
	if (thing == NOTHING)
		return;

	if (!Examinable(player, thing)) {
		notify_quiet(player,
			  "You can only decompile things you can examine.");
		return;
	}
	thingname = atr_get(thing, A_LOCK, &aowner, &aflags);
	bool = parse_boolexp(player, thingname, 1);

	/*
	 * Determine the name of the thing to use in reporting and then
	 * report the command to make the thing. 
	 */

	if (qual && *qual) {
		StringCopy(thingname, qual);
	} else {
		switch (Typeof(thing)) {
		case TYPE_THING:
			StringCopy(thingname, Name(thing));
			val = OBJECT_DEPOSIT(Pennies(thing));
			notify(player,
			     tprintf("@create %s=%d", translate_string(thingname, 1),
				     val));
			break;
		case TYPE_ROOM:
			StringCopy(thingname, "here");
			notify(player,
			       tprintf("@dig/teleport %s",
				       translate_string(Name(thing), 1)));
			break;
		case TYPE_EXIT:
			StringCopy(thingname, Name(thing));
			notify(player,
			       tprintf("@open %s", translate_string(Name(thing), 1)));
			for (got = thingname; *got; got++) {
				if (*got == EXIT_DELIMITER) {
					*got = '\0';
					break;
				}
			}
			break;
		case TYPE_PLAYER:
			StringCopy(thingname, "me");
			break;
		}
	}

	/*
	 * Report the lock (if any) 
	 */

	if (bool != TRUE_BOOLEXP) {
		notify(player, tprintf("@lock %s=%s", strip_ansi(thingname),
				  unparse_boolexp_decompile(player, bool)));
	}
	free_boolexp(bool);

	/*
	 * Report attributes 
	 */

	buff = alloc_mbuf("do_decomp.attr_name");
	for (ca = atr_head(thing, &as); ca; ca = atr_next(&as)) {
		if ((ca == A_NAME) || (ca == A_LOCK))
			continue;
		attr = atr_num(ca);
		if (!attr)
			continue;
		if ((attr->flags & AF_NOCMD) && !(attr->flags & AF_IS_LOCK))
			continue;

		got = atr_get(thing, ca, &aowner, &aflags);
		if (Read_attr(player, thing, attr, aowner, aflags)) {
			if (attr->flags & AF_IS_LOCK) {
				bool = parse_boolexp(player, got, 1);
				ltext = unparse_boolexp_decompile(player,
								  bool);
				free_boolexp(bool);
				notify(player,
				       tprintf("@lock/%s %s=%s",
					     attr->name, thingname, ltext));
			} else {
				StringCopy(buff, attr->name);
				notify(player,
				       tprintf("%c%s %s=%s",
					       ((ca < A_USER_START) ?
						'@' : '&'),
					       buff, strip_ansi(thingname),
					       got));

				if (aflags & AF_LOCK) {
					notify(player, tprintf("@lock %s/%s",
						      strip_ansi(thingname),
							       buff));
				}
				for (np = indiv_attraccess_nametab;
				     np->name;
				     np++) {

					if ((aflags & np->flag) &&
					    check_access(player, np->perm) &&
					    (!(np->perm & CA_NO_DECOMP))) {

						notify(player,
						  tprintf("@set %s/%s = %s",
						      strip_ansi(thingname),
							  buff,
							  np->name));
					}
				}
			}
		}
		free_lbuf(got);
	}
	free_mbuf(buff);

	decompile_flags(player, thing, thingname);
	decompile_powers(player, thing, thingname);

	/*
	 * If the object has a parent, report it 
	 */

	if (Parent(thing) != NOTHING)
		notify(player,
		       tprintf("@parent %s=#%d", strip_ansi(thingname), Parent(thing)));

	/*
	 * If the object has a zone, report it 
	 */
	if (Zone(thing) != NOTHING)
		notify(player,
		       tprintf("@chzone %s=#%d", strip_ansi(thingname), Zone(thing)));

	free_lbuf(thingname);
}

/* show_vrml_url
 */
void
show_vrml_url(thing, loc)
dbref thing, loc;
{
    char *vrml_url;
    dbref aowner;
    int aflags;

    /* If they don't care about HTML, just return. */
    if (!Html(thing))
	return;

    vrml_url = atr_pget(loc, A_VRML_URL, &aowner, &aflags);
    if (*vrml_url) {
	char *vrml_message, *vrml_cp;

	vrml_message = vrml_cp = alloc_lbuf("show_vrml_url");
	safe_str("<img xch_graph=load href=\"", vrml_message, &vrml_cp);
	safe_str(vrml_url, vrml_message, &vrml_cp);
	safe_str("\">", vrml_message, &vrml_cp);
	*vrml_cp = 0;
	notify_html(thing, vrml_message);
	free_lbuf(vrml_message);
    } else {
	notify_html(thing, "<img xch_graph=hide>");
    }
    free_lbuf(vrml_url);
}

