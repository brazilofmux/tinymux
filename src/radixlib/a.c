/*
 * Analyse a MUSH database, we chew it in on stdin, extract
 * attributes, and stuff them into a radix tree. We accept a single
 * parameter telling us what the maximum length word to store is, and
 * then we dump the whole tree out. This gives a complete list of all
 * substrings of length N or less found in a MUSH database, complete
 * with counts, for post-processing.
 * 
 */

#include <stdio.h>
#include "radix.h"

/*
 * forward 
 */
void copyattr();

char attr[4002];

main(ac, av)
int ac;
char *av[];
{
	struct r_node *root;
	int ch, i, len, sslen, max_len;
	char substr[128];
	int objcnt = 0;

	if (ac != 2) {
		fprintf(stderr, "usage: %s <max string length>\n", av[0]);
		exit(1);
	}
	max_len = atoi(av[1]);
	if (max_len <= 0) {
		fprintf(stderr, "usage: %s <positive max string length>\n",
			av[0]);
		exit(1);
	}
	if (max_len >= 128) {
		fprintf(stderr, "A max string length os over 128 is insane.\n");
		exit(1);
	}
	switch (max_len) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		break;
	case 6:
	case 7:
	case 8:
		fprintf(stderr,
			"counting all strings up to %s long is going to be expensive\n",
			av[1]);
		break;
	default:
		fprintf(stderr,
		"All strings of length %s? I hope you have some real iron\n",
			av[1]);
		break;
	}

	root = (struct r_node *)malloc(sizeof(struct r_node));

	root->count = 0;

	while ((ch = getchar()) != EOF) {
		switch (ch) {
		case '>':
			eatline();
			copyattr(attr);
			len = strlen(attr);
			for (i = 0; i < len; i++) {
				sslen = ((len - i) < max_len ?
					 (len - i) : max_len);
				bcopy(attr + i, substr, sslen);
				substr[sslen] = '\0';
				r_insert(&root, substr);
			}
			break;
		case '!':
			objcnt++;
			if ((objcnt & 0x1ff) == 0)
				fprintf(stderr, ".");
			fflush(stderr);
		default:
			eatline();
		}
	}

	r_dump(root);
}

/*
 * Consume a line up to and including the newline 
 */

eatline()
{
	int ch;

	while ((ch = getchar()) != '\n') {
		if (ch == EOF)
			break;
	}
}
/*
 * Read in a string on stdin and stuff it into the passed down array, observing
 * * the strange rules for attribute escaping.
 */

void copyattr(buff)
char *buff;
{
	char last;
	int i;
	char ch = '\0';		/*

				 * anything other than a \r 
				 */

	i = 0;
	do {
		last = ch;
		ch = getchar();
		buff[i++] = ch;

		/*
		 * Internal newlines are escaped as \r\n 
		 */

		if (ch == '\n' && last == '\r') {
			buff[i - 2] = ' ';
			i--;
		}
	} while ((ch != '\n' || last == '\r') && ch != EOF && i < 4000);
	buff[i - 1] = '\0';	/*
				 * Whack trailing newline 
				 */
}
