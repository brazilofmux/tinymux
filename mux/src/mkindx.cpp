// mkindx.cpp -- Make help/news file indexes.
//
// $Id: mkindx.cpp,v 1.1 2002-05-24 06:53:15 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#include "help.h"
#include "_build.h"

#define LINE_SIZE 4096

char line[LINE_SIZE + 1];
int DCL_CDECL main(int argc, char *argv[])
{
    long pos;
    int i, n, lineno, ntopics;
    help_indx entry;
    FILE *rfp, *wfp;

    if (argc < 2 || argc > 3)
    {
#ifdef WIN32
#ifdef BETA
        printf("%s from MUX %s for Win32 #%s [BETA]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM);
#else // BETA
        printf("%s from MUX %s for Win32 #%s [%s]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif // BETA
#else // WIN32
#ifdef BETA
        printf("%s from MUX %s #%s [BETA]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM);
#else // BETA
        printf("%s from MUX %s #%s [%s]\n", argv[0], MUX_VERSION, MUX_BUILD_NUM,
            MUX_RELEASE_DATE);
#endif // BETA
#endif // WIN32
        printf("Usage:\tmkindx <file_to_be_indexed> <output_index_filename>\n");
        exit(-1);
    }
    if ((rfp = fopen(argv[1], "rb")) == NULL)
    {
        fprintf(stderr, "can't open %s for reading\n", argv[1]);
        exit(-1);
    }
    if ((wfp = fopen(argv[2], "wb")) == NULL)
    {
        fprintf(stderr, "can't open %s for writing\n", argv[2]);
        exit(-1);
    }
    pos = 0L;
    lineno = 0;
    ntopics = 0;
    while (fgets(line, LINE_SIZE, rfp) != NULL)
    {
        ++lineno;

        n = strlen(line);
        if (n == 0)
        {
            continue;
        }
        else if (line[n - 1] != '\n')
        {
            fprintf(stderr, "line %d: line too long\n", lineno);
        }
        if (line[0] == '&')
        {
            ++ntopics;

            if (ntopics > 1)
            {
                entry.len = (int)(pos - entry.pos);
                if (fwrite(&entry, sizeof(help_indx), 1, wfp) < 1)
                {
                    fprintf(stderr, "error writing %s\n", argv[2]);
                    exit(-1);
                }
            }
            char *topic = line+1;
            while (*topic == ' ' || *topic == '\t' || *topic == '\r')
            {
                topic++;
            }
            char *s;
            memset(entry.topic, 0, sizeof(entry.topic));
            for (i = -1, s = topic; *s != '\n' && *s != '\r' && *s != '\0'; s++)
            {
                if (i >= TOPIC_NAME_LEN - 1)
                {
                    break;
                }
                if (*s != ' ' || entry.topic[i] != ' ')
                {
                    entry.topic[++i] = *s;
                }
            }
            entry.topic[++i] = '\0';
            entry.pos = pos + (long)n;
        }
        pos += n;
    }
    entry.len = (int)(pos - entry.pos);
    if (fwrite(&entry, sizeof(help_indx), 1, wfp) < 1)
    {
        fprintf(stderr, "error writing %s\n", argv[2]);
        exit(-1);
    }
    fclose(rfp);
    fclose(wfp);

    printf("%d topics indexed\n", ntopics);
    return 0;
}
