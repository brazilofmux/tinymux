// help.h
//
// $Id: help.h,v 1.1 2003-01-22 19:58:25 sdennis Exp $
//

#define  TOPIC_NAME_LEN     30

typedef struct
{
  long pos;         /* index into help file */
  int len;          /* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];   /* topic of help entry */
} help_indx;

