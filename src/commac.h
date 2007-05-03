/* commac.h */
/* $Id: commac.h,v 1.1.1.1 1997/04/16 03:36:51 dpassmor Exp $ */

#ifndef __COMMAC_H__
#define __COMMAC_H__


struct commac
{
    dbref who;

    int numchannels;
    int maxchannels;
    char *alias;
    char **channels;

    int curmac;
    int macros[5];

    struct commac *next;
};

#define NUM_COMMAC 500

struct commac *commac_table[NUM_COMMAC];

char *load_commac ();
char *save_commac ();
char *purge_commac();

char *sort_com_aliases();
struct commac *get_commac ();
struct commac *create_new_commac ();
char *destroy_commac ();
char *add_commac ();
char *del_commac ();
char *save_comsys_and_macros ();
char *load_comsys_and_macros ();
#endif /* __COMMAC_H__ */
