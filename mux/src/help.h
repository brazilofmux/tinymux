/*! \file help.h
 * \brief In-game help system.
 *
 */

#define  TOPIC_NAME_LEN     30

void helpindex_clean(int);
void helpindex_load(dbref);
void helpindex_init(void);
void help_helper(dbref executor, int iHelpfile, UTF8 *topic_arg, UTF8 *buff, UTF8 **bufc);
