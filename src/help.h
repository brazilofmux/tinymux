/* help.h */
/* $Id: help.h,v 1.2 1997/04/16 06:01:09 dpassmor Exp $ */

#define  LINE_SIZE		90

#define  TOPIC_NAME_LEN		30

typedef struct {
  long pos;			/* index into help file */
  int len;			/* length of help entry */
  char topic[TOPIC_NAME_LEN + 1];	/* topic of help entry */
} help_indx;
