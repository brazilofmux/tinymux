/*
 * mkindx.c -- make help/news file indexes 
 */
/*
 * $Id: mkindx.c,v 1.2 1997/04/16 06:01:24 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include  "help.h"

char line[LINE_SIZE + 1];
void main(argc, argv)
int argc;
char *argv[];
{
	long pos;
	int i, n, lineno, ntopics;
	char *s, *topic;
	help_indx entry;
	FILE *rfp, *wfp;

	if (argc < 2 || argc > 3) {
		printf("Usage:\tmkindx <file_to_be_indexed> <output_index_filename>\n");
		exit(-1);
	}
	if ((rfp = fopen(argv[1], "r")) == NULL) {
		fprintf(stderr, "can't open %s for reading\n", argv[1]);
		exit(-1);
	}
	if ((wfp = fopen(argv[2], "w")) == NULL) {
		fprintf(stderr, "can't open %s for writing\n", argv[2]);
		exit(-1);
	}
	pos = 0L;
	lineno = 0;
	ntopics = 0;
	while (fgets(line, LINE_SIZE, rfp) != NULL) {
		++lineno;

		n = strlen(line);
		if (line[n - 1] != '\n') {
			fprintf(stderr, "line %d: line too long\n", lineno);
		}
		if (line[0] == '&') {
			++ntopics;

			if (ntopics > 1) {
				entry.len = (int)(pos - entry.pos);
				if (fwrite(&entry, sizeof(help_indx), 1, wfp) < 1) {
					fprintf(stderr, "error writing %s\n", argv[2]);
					exit(-1);
				}
			}
			for (topic = &line[1];
			     (*topic == ' ' || *topic == '\t') && *topic != '\0'; topic++) ;
			for (i = -1, s = topic; *s != '\n' && *s != '\0'; s++) {
				if (i >= TOPIC_NAME_LEN - 1)
					break;
				if (*s != ' ' || entry.topic[i] != ' ')
					entry.topic[++i] = *s;
			}
			entry.topic[++i] = '\0';
			entry.pos = pos + (long)n;
		}
		pos += n;
	}
	entry.len = (int)(pos - entry.pos);
	if (fwrite(&entry, sizeof(help_indx), 1, wfp) < 1) {
		fprintf(stderr, "error writing %s\n", argv[2]);
		exit(-1);
	}
	fclose(rfp);
	fclose(wfp);

	printf("%d topics indexed\n", ntopics);
	exit(0);
}
