// help.h
//
// $Id: help.h,v 1.1 2002-05-24 06:53:15 sdennis Exp $
//

#define  TOPIC_NAME_LEN     30

typedef struct
{
  long pos;         /* index into help file */
  int len;          /* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];   /* topic of help entry */
} help_indx;
