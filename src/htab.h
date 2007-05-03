/* htab.h - Structures and declarations needed for table hashing */
/* $Id: htab.h,v 1.2 1997/04/16 06:01:11 dpassmor Exp $ */

#include "copyright.h"

#ifndef __HTAB_H
#define __HTAB_H

#include "db.h"

typedef struct hashentry HASHENT;
struct hashentry {
	char			*target;
	int			*data;
	int			checks;
	struct hashentry	*next;
};

typedef struct num_hashentry NHSHENT;
struct num_hashentry {
	int			target;
	int			*data;
	int			checks;
	struct num_hashentry	*next;
};

typedef struct hasharray HASHARR;
struct hasharray {
	HASHENT		*element[800];
};

typedef struct num_hasharray NHSHARR;
struct num_hasharray {
	NHSHENT		*element[800];
};

typedef struct hashtable HASHTAB;
struct hashtable {
	int		hashsize;
	int		mask;
	int		checks;
	int		scans;
	int		max_scan;
	int		hits;
	int		entries;
	int		deletes;
	int		nulls;
	HASHARR		*entry;
	int		last_hval;  /* Used for hashfirst & hashnext. */
	HASHENT		*last_entry;   /* like last_hval */	
};

typedef struct num_hashtable NHSHTAB;
struct num_hashtable {
	int		hashsize;
	int		mask;
	int		checks;
	int		scans;
	int		max_scan;
	int		hits;
	int		entries;
	int		deletes;
	int		nulls;
	NHSHARR		*entry;
	int		last_hval;
	NHSHENT		*last_entry;
};

typedef struct name_table NAMETAB;
struct name_table {
	char	*name;
	int	minlen;
	int	perm;
	int	flag;
};

/* BQUE - Command queue */

typedef struct bque BQUE;
struct bque {
	BQUE	*next;
	dbref	player;		/* player who will do command */
	dbref	cause;		/* player causing command (for %N) */
	dbref	sem;		/* blocking semaphore */
	int	waittime;	/* time to run command */
	int	attr;		/* blocking attribute */
	char	*text;		/* buffer for comm, env, and scr text */
	char	*comm;		/* command */
	char	*env[NUM_ENV_VARS];	/* environment vars */
	char	*scr[NUM_ENV_VARS];	/* temp vars */
	int	nargs;		/* How many args I have */
};

extern void	FDECL(hashinit, (HASHTAB *, int));
extern void	FDECL(hashreset, (HASHTAB *));
extern int	FDECL(hashval, (char *, int));
extern int	FDECL(get_hashmask, (int *));
extern int	FDECL(*hashfind, (char *, HASHTAB *));
extern int	FDECL(hashadd, (char *, int *, HASHTAB *));
extern void	FDECL(hashdelete, (char *, HASHTAB *));
extern void	FDECL(hashflush, (HASHTAB *, int));
extern int	FDECL(hashrepl, (char *, int *, HASHTAB *));
extern void	FDECL(hashreplall, (int *, int *, HASHTAB *));
extern char	FDECL(*hashinfo, (const char *, HASHTAB *));
extern int	FDECL(*nhashfind, (int, NHSHTAB *));
extern int	FDECL(nhashadd, (int, int *, NHSHTAB *));
extern void	FDECL(nhashdelete, (int, NHSHTAB *));
extern void	FDECL(nhashflush, (NHSHTAB *, int));
extern int	FDECL(nhashrepl, (int, int *, NHSHTAB *));
extern int	FDECL(search_nametab, (dbref, NAMETAB *, char *));
extern NAMETAB	FDECL(*find_nametab_ent, (dbref, NAMETAB *, char *));
extern void	FDECL(display_nametab, (dbref, NAMETAB *, char *, int));
extern void	FDECL(interp_nametab, (dbref, NAMETAB *, int, char *, char *,
			char *));
extern void	FDECL(listset_nametab, (dbref, NAMETAB *, int, char *, int));
extern int	FDECL(*hash_nextentry, (HASHTAB *htab));
extern int	FDECL(*hash_firstentry, (HASHTAB *htab));
extern char	FDECL(*hash_firstkey, (HASHTAB *htab));
extern char	FDECL(*hash_nextkey, (HASHTAB *htab));
extern int	FDECL(*nhash_nextentry, (NHSHTAB *htab));
extern int	FDECL(*nhash_firstentry, (NHSHTAB *htab));

extern NAMETAB powers_nametab[];
#define nhashinit(h,s) hashinit((HASHTAB *)h, s)
#define nhashreset(h) hashreset((HASHTAB *)h)
#define nhashinfo(t,h) hashinfo(t,(HASHTAB *)h)

#endif
