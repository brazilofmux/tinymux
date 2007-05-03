/* powers.h - object powers */
/* $Id: powers.h,v 1.2 1997/04/16 06:01:33 dpassmor Exp $ */

#include "copyright.h"

#ifndef __POWERS_H
#define	__POWERS_H

#include "htab.h"
#include "db.h"



#define	POWER_EXT	0x1	/* Lives in extended powers word */


/* First word of powers */
#define	POW_CHG_QUOTAS	0x00000001	/* May change and see quotas */
#define	POW_CHOWN_ANY	0x00000002	/* Can @chown anything or to anyone */
#define	POW_ANNOUNCE	0x00000004	/* May use @wall */
#define	POW_BOOT	0x00000008	/* May use @boot */
#define	POW_HALT	0x00000010	/* May @halt on other's objects */
#define	POW_CONTROL_ALL	0x00000020	/* I control everything */
#define	POW_WIZARD_WHO	0x00000040	/* See extra WHO information */
#define	POW_EXAM_ALL	0x00000080	/* I can examine everything */
#define	POW_FIND_UNFIND	0x00000100	/* Can find unfindable players */
#define	POW_FREE_MONEY	0x00000200	/* I have infinite money */
#define	POW_FREE_QUOTA	0x00000400	/* I have infinite quota */
#define	POW_HIDE	0x00000800	/* Can set themselves DARK */
#define	POW_IDLE	0x00001000	/* No idle limit */
#define	POW_SEARCH	0x00002000	/* Can @search anyone */
#define	POW_LONGFINGERS	0x00004000	/* Can get/whisper/etc from a distance */
#define POW_PROG	0x00008000	/* Can use the @prog command */

/* FREE FREE FREE FREE */

#define	POW_COMM_ALL	0x00080000	/* Channel wiz */
#define	POW_SEE_QUEUE	0x00100000	/* Player can see the entire queue */
#define	POW_SEE_HIDDEN	0x00200000	/* Player can see hidden players on WHO list */
#define	POW_MONITOR	0x00400000	/* Player can set or clear MONITOR */
#define	POW_POLL	0x00800000	/* Player can set the doing poll */
#define	POW_NO_DESTROY	0x01000000	/* Cannot be destroyed */
#define POW_GUEST	0x02000000	/* Player is a guest */
#define	POW_PASS_LOCKS	0x04000000	/* Player can pass any lock */
#define	POW_STAT_ANY	0x08000000	/* Can @stat anyone */
#define	POW_STEAL	0x10000000	/* Can give negative money */
#define	POW_TEL_ANYWHR	0x20000000	/* Teleport anywhere */
#define	POW_TEL_UNRST	0x40000000	/* Teleport anything */
#define	POW_UNKILLABLE	0x80000000	/* Can't be killed */

/* Second word of powers */
#define POW_BUILDER	0x00000001	/* Can build */

/* ---------------------------------------------------------------------------
 * POWERENT: Information about object powers.
 */

typedef struct power_entry {
	const char *powername;	/* Name of the flag */
	int	powervalue;	/* Which bit in the object is the flag */
	int	powerpower;	/* Ctrl flags for this power (recursive? :-) */
	int	listperm;	/* Who sees this flag when set */
	int	(*handler)();	/* Handler for setting/clearing this flag */
} POWERENT;

typedef struct powerset {
	POWER	word1;
	POWER	word2;
} POWERSET;

extern void	NDECL(init_powertab);
extern void	FDECL(display_powertab, (dbref));
extern void	FDECL(power_set, (dbref, dbref, char *, int));
extern char *	FDECL(power_description, (dbref, dbref));
extern POWERENT *FDECL(find_power, (dbref, char *));
extern int	FDECL(has_power, (dbref, dbref, char *));
extern void	FDECL(decompile_powers, (dbref, dbref, char *));


#define s_Change_Quotas(c)	s_Powers((c), Powers(c) | POW_CHG_QUOTAS)
#define s_Chown_Any(c)		s_Powers((c), Powers(c) | POW_CHOWN_ANY)
#define s_Announce(c)		s_Powers((c), Powers(c) | POW_ANNOUNCE)
#define s_Can_Boot(c)		s_Powers((c), Powers(c) | POW_BOOT)
#define s_Can_Halt(c)		s_Powers((c), Powers(c) | POW_HALT)
#define s_Control_All(c)	s_Powers((c), Powers(c) | POW_CONTROL_ALL)
#define s_Wizard_Who(c)		s_Powers((c), Powers(c) | POW_WIZARD_WHO)
#define s_See_All(c)		s_Powers((c), Powers(c) | POW_EXAM_ALL)
#define s_Find_Unfindable(c)	s_Powers((c), Powers(c) | POW_FIND_UNFIND)
#define s_Free_Money(c)		s_Powers((c), Powers(c) | POW_FREE_MONEY)
#define s_Free_Quota(c)		s_Powers((c), Powers(c) | POW_FREE_QUOTA)
#define s_Can_Hide(c)		s_Powers((c), Powers(c) | POW_HIDE)
#define s_Can_Idle(c)		s_Powers((c), Powers(c) | POW_IDLE)
#define s_Search(c)		s_Powers((c), Powers(c) | POW_SEARCH)
#define s_Long_Fingers(c)	s_Powers((c), Powers(c) | POW_LONGFINGERS)
#define s_Prog(c)		s_Powers((c), Powers(c) | POW_PROG)
#define s_Comm_All(c)		s_Powers((c), Powers(c) | POW_COMM_ALL)
#define s_See_Queue(c)		s_Powers((c), Powers(c) | POW_SEE_QUEUE)
#define s_See_Hidden(c)		s_Powers((c), Powers(c) | POW_SEE_HIDDEN)
#define s_Can_Monitor(c)	s_Powers((c), Powers(c) | POW_MONITOR)
#define s_Can_Poll(c)		s_Powers((c), Powers(c) | POW_POLL)
#define s_No_Destroy(c)		s_Powers((c), Powers(c) | POW_NO_DESTROY)
#define s_Guest(c)		s_Powers((c), Powers(c) | POW_GUEST)
#define s_Set_Maint_Flags(c)	s_Powers((c), Powers(c) | POW_SET_MFLAGS)
#define s_Stat_Any(c)		s_Powers((c), Powers(c) | POW_STAT_ANY)
#define s_Steal(c)		s_Powers((c), Powers(c) | POW_STEAL)
#define s_Tel_Anywhere(c)	s_Powers((c), Powers(c) | POW_TEL_ANYWHR)
#define s_Tel_Anything(c)	s_Powers((c), Powers(c) | POW_TEL_UNRST)
#define s_Unkillable(c)		s_Powers((c), Powers(c) | POW_UNKILLABLE)
#define s_Builder(c)		s_Powers2((c), Powers2(c) | POW_BUILDER)

#define Quota(c)		(((Powers(c) & POW_CHG_QUOTAS) != 0) || Wizard(c))
#define Chown_Any(c)		(((Powers(c) & POW_CHOWN_ANY) != 0) || Wizard(c))
#define Announce(c)		(((Powers(c) & POW_ANNOUNCE) != 0) || Wizard(c))
#define Can_Boot(c)		(((Powers(c) & POW_BOOT) != 0) || Wizard(c))
#define Can_Halt(c)		(((Powers(c) & POW_HALT) != 0) || Wizard(c))
#define Control_All(c)		(((Powers(c) & POW_CONTROL_ALL) != 0) || Wizard(c))
#define Wizard_Who(c)		(((Powers(c) & POW_WIZARD_WHO) != 0) || WizRoy(c))
#define See_All(c)		(((Powers(c) & POW_EXAM_ALL) != 0) || WizRoy(c))
#define Find_Unfindable(c)	((Powers(c) & POW_FIND_UNFIND) != 0)
#define Free_Money(c)		(((Powers(c) & POW_FREE_MONEY) != 0) || Immortal(c))
#define Free_Quota(c)		(((Powers(c) & POW_FREE_QUOTA) != 0) || Wizard(c))
#define Can_Hide(c)		((Powers(c) & POW_HIDE) != 0)
#define Can_Idle(c)		(((Powers(c) & POW_IDLE) != 0) || Wizard(c))
#define Search(c)		(((Powers(c) & POW_SEARCH) != 0) || WizRoy(c))
#define Long_Fingers(c)		(((Powers(c) & POW_LONGFINGERS) != 0) || Wizard(c))
#define Comm_All(c)		(((Powers(c) & POW_COMM_ALL) != 0) || Wizard(c))
#define See_Queue(c)		(((Powers(c) & POW_SEE_QUEUE) != 0) || WizRoy(c))
#define See_Hidden(c)		((Powers(c) & POW_SEE_HIDDEN) != 0)
#define Can_Monitor(c)		(((Powers(c) & POW_MONITOR) != 0) || Wizard(c))
#define Can_Poll(c)		(((Powers(c) & POW_POLL) != 0) || Wizard(c))
#define No_Destroy(c)		(((Powers(c) & POW_NO_DESTROY) != 0) || Wizard(c))
#define Guest(c)		((Powers(c) & POW_GUEST) != 0)
#define Set_Maint_Flags(c)	((Powers(c) & POW_SET_MFLAGS) != 0)
#define Stat_Any(c)		((Powers(c) & POW_STAT_ANY) != 0)
#define Steal(c)		(((Powers(c) & POW_STEAL) != 0) || Wizard(c))
#define Tel_Anywhere(c)		(((Powers(c) & POW_TEL_ANYWHR) != 0) || Tel_Anything(c))
#define Tel_Anything(c)		(((Powers(c) & POW_TEL_UNRST) != 0) || WizRoy(c))
#define Unkillable(c)		(((Powers(c) & POW_UNKILLABLE) != 0) || Immortal(c))
#define Prog(c)			(((Powers(c) & POW_PROG) != 0) || Wizard(c))
#define Pass_Locks(c)		((Powers(c) & POW_PASS_LOCKS) != 0)
#define Builder(c)		(((Powers2(c) & POW_BUILDER) != 0) || WizRoy(c))
#endif /* POWERS_H */
