// help.h
//
// $Id: help.h,v 1.2 2003-01-06 04:18:04 sdennis Exp $
//

#define  TOPIC_NAME_LEN     30

typedef struct
{
  long pos;         /* index into help file */
  int len;          /* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];   /* topic of help entry */
} help_indx;

