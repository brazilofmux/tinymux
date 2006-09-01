// help.h
//
// $Id: help.h,v 1.2 2006/01/11 20:51:31 sdennis Exp $
//

#define  TOPIC_NAME_LEN     30

typedef struct
{
  size_t pos;         /* index into help file */
  size_t len;         /* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];   /* topic of help entry */
} help_indx;

