/* macro.h */
/* $Id: macro.h,v 1.1 2000-04-11 07:14:45 sdennis Exp $ */

#ifndef __MACRO_H
#define __MACRO_H

#include <stdio.h>

#include "db.h"
#include "interface.h"
#include "match.h"
#include "config.h"
#include "externs.h"

#define MACRO_L 1
#define MACRO_R 2
#define MACRO_W 4
#define MAX_SLOTS 5  /* Number of macro slots a person can have. */

typedef struct macroentry MACENT;
struct macroentry {
    char *cmdname;
    void (*handler)();
};

struct macros
{
    int player;
    char status;
    char *desc;
    int nummacros;
    int maxmacros;
    char *alias;    /* Chopped into 5 byte sections.  Macro can have  */
    char **string;  /* at most a 4 letter alias               */
};

int nummacros;
int maxmacros;
struct macros **macros;

struct macros *get_macro_set ();
int can_write_macros ();
int can_read_macros ();

void do_sort_macro_set ();
void save_macros ();
void load_macros ();

int do_macro ();
void do_add_macro ();

void do_chown_macro();
void do_clear_macro ();
void do_chmod_macro ();
void do_create_macro ();
void do_def_macro ();
void do_del_macro ();
void do_desc_macro ();
void do_edit_macro ();
void do_ex_macro ();
void do_list_macro ();
void do_status_macro ();
void do_undef_macro ();
void do_gex_macro ();
char *do_process_macro ();

#endif /* __MACRO_H */
