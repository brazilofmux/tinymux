// help.h
//
// $Id: help.h,v 1.2 2001-11-19 19:37:44 sdennis Exp $
//

#define  TOPIC_NAME_LEN     30

typedef struct
{
  long pos;         /* index into help file */
  int len;          /* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];   /* topic of help entry */
} help_indx;
