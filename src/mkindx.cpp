//
// mkindx.cpp -- make help/news file indexes 
//
// $Id: mkindx.cpp,v 1.4 2000-10-25 04:36:21 sdennis Exp $ 
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
    char *s, *topic;
    help_indx entry;
    FILE *rfp, *wfp;

    if (argc < 2 || argc > 3)
    {
#ifdef WIN32
#ifdef ALPHA
#if PATCHLEVEL > 0
        printf("%s from MUX %sp%d for Win32 #%s [ALPHA]\n", argv[0],
            MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM);
#else // PATCHLEVEL
        printf("%s from MUX %s for Win32 #%s [ALPHA]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM);
#endif // PATCHLEVEL
#else // ALPHA
#if PATCHLEVEL > 0 
        printf("%s from MUX %sp%d for Win32 #%s [%s]\n", argv[0], MUX_VERSION,
            PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#else // PATCHLEVEL
        printf("%s from MUX %s for Win32 #%s [%s]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif // PATCHLEVEL
#endif // ALPHA
#else // WIN32
#ifdef ALPHA
#if PATCHLEVEL > 0
        printf("%s from MUX %sp%d #%s [ALPHA]\n", argv[0], MUX_VERSION,
            PATCHLEVEL, MUX_BUILD_NUM);
#else // PATCHLEVEL
        printf("%s from MUX %s #%s [ALPHA]\n", argv[0], MUX_VERSION,
            MUX_BUILD_NUM);
#endif // PATCHLEVEL
#else // ALPHA
#if PATCHLEVEL > 0 
        printf("%s from MUX %sp%d #%s [%s]\n", argv[0], MUX_VERSION,
            PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#else // PATCHLEVEL
        printf("%s from MUX %s #%s [%s]\n", argv[0], MUX_VERSION, MUX_BUILD_NUM,
            MUX_RELEASE_DATE);
#endif // PATCHLEVEL
#endif // ALPHA
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
    return 0;
}
