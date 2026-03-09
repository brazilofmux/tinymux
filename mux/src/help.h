/*! \file help.h
 * \brief In-game help system.
 *
 */

#ifndef HELP_H
#define HELP_H

#define  TOPIC_NAME_LEN     30

void helpindex_clean(int);
DCL_EXPORT void helpindex_load(dbref);
DCL_EXPORT void helpindex_init(void);
DCL_EXPORT void help_helper(dbref executor, int iHelpfile, UTF8 *topic_arg, UTF8 *buff, UTF8 **bufc);

#endif // !HELP_H