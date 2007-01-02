// help.h
//
// $Id$
//

#define  TOPIC_NAME_LEN     30

void helpindex_clean(int);
void helpindex_load(dbref);
void helpindex_init(void);
void help_helper(dbref executor, int iHelpfile, char *topic_arg, char *buff, char **bufc);
