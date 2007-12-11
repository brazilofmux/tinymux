/*! \file dbpuller.c
 * \brief Offline flatfile-to-softcode converter.
 *
 * $Id$
 *
 * Db puller - used to pull data from a MUX flatfile and dump it
 * into a file in \@decompile format.
 *
 * \version 1.01
 * \author Ashen-Shugar (08/16/2005)
 *
 * \verbatim
 * Modifications: List modifications below
 *      11/02/05 : filenames are now saved with _<dbref> extensions.
 *                     the name of the object starts the file with a '@@' comment prefix.
 * \endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Standard MUX 2.x definitions */
/* This should be over twice the size of the LBUF.  If it's not, it'll misbehave */
#define MALSIZE 16535
/* This should be SBUF_SIZE + 1. If it's not, it'll coredump*/
#define SBUFSIZE 65
#define ESC_CHAR '\033'

int stricmp(const char *buf1, const char *buf2)
{
    const char *p1 = buf1;
    const char *p2 = buf2;

    while (  '\0' != *p1
          && '\0' != *p2
          && tolower(*p1) == tolower(*p2))
    {
        p1++;
        p2++;
    }

    if (  '\0' == *p1
       && '\0' == *p2)
    {
        return 0;
    }

    if ('\0' == *p1)
    {
        return -1;
    }

    if ('\0' == *p2)
    {
        return 1;
    }

    if (*p1 < *p2)
    {
        return -1;
    }
    return 1;
}

char g_line[MALSIZE];

int main(int argc, char **argv)
{
    FILE *f_muxflat, *f_mymuxfile, *f_muxattrs, *f_muxout, *f_muxlock;
    char *pt1, *spt3, *pt3, s_attrib[SBUFSIZE], s_filename[80],
          s_attrval[SBUFSIZE], s_attr[SBUFSIZE], s_finattr[SBUFSIZE];
    int i_chk = 0, i_lck = 1, i_atrcntr = 0, i_atrcntr2 = 0, i_pullname = 0;

    if (argc < 3)
    {
        fprintf(stderr, "Syntax: %s mux-flatfile dbref# (no preceeding # character) [optional attribute-name]\n", argv[0]);
        exit(1);
    }

    f_muxflat = fopen(argv[1], "r");
    if (NULL == f_muxflat)
    {
        fprintf(stderr, "ERROR: Unable to open %s for reading.", argv[1]);
        exit(1);
    }

    pt1 = argv[2];
    while ('\0' != *pt1)
    {
        if (!isdigit(*pt1))
        {
            fprintf(stderr, "ERROR: Dbref# must be an integer (no # preceeding) [optional attribute-name]\n");
            fclose(f_muxflat);
            exit(1);
        }
        pt1++;
    }

    f_mymuxfile = fopen("mymuxfile.dat", "w");
    if (NULL == f_mymuxfile)
    {
        fprintf(stderr, "ERROR: Unable to open output file for attribute header information (mymuxfile.dat)\n");
        fclose(f_muxflat);
        exit(1);
    }
    memset(s_attrib, '\0', sizeof(s_attrib));
    if (  4 <= argc
       && '\0' != *argv[3])
    {
        strncpy(s_attrib, argv[3], SBUFSIZE-1);
    }

    memset(s_attr, '\0', sizeof(s_attr));
    memset(s_attrval, '\0', sizeof(s_attr));

    while (!feof(f_muxflat))
    {
        fgets(g_line, sizeof(g_line), f_muxflat);

        if (i_chk)
        {
            i_chk = 0;
            strtok(g_line, ":");
            sprintf(s_attr, "%s", strtok(NULL, ":"));
            s_attr[strlen(s_attr)-2]='\0';
            fprintf(f_mymuxfile, "%s %d \n", s_attr, atoi(s_attrval));
        }

        if (  3 < strlen(g_line)
           && '+' == g_line[0]
           && 'A' == g_line[1]
           && isdigit(g_line[2]))
        {
            i_chk = 1;
            sprintf(s_attrval, "%s", &g_line[2]);
        }

        if ('!' == g_line[0])
        {
            break;
        }
    }
    fclose(f_mymuxfile);

    f_mymuxfile = fopen("mymuxfile.dat", "r");
    if (NULL == f_mymuxfile)
    {
        fclose(f_muxflat);
        fprintf(stderr, "ERROR: Unable to open attribute header information (mymuxfile.dat)\n");
        exit(1);
    }

    f_muxattrs = fopen("muxattrs.dat", "r");
    if (NULL == f_muxattrs)
    {
        fclose(f_muxflat);
        fclose(f_mymuxfile);
        fprintf(stderr, "ERROR: Unable to open attribute header information (muxattrs.dat)\n");
        exit(1);
    }
    memset(s_filename, '\0', sizeof(s_filename));
    sprintf(s_filename, "muxout_%d.txt", atoi(argv[2]));

    f_muxout = fopen(s_filename, "w");
    if (NULL == f_muxout)
    {
        fclose(f_muxflat);
        fclose(f_mymuxfile);
        fclose(f_muxattrs);
        fprintf(stderr, "ERROR: Unable to open output file (%s)\n", s_filename);
        exit(1);
    }

    f_muxlock = fopen("muxlocks.dat", "r");
    if (NULL == f_muxlock)
    {
        fclose(f_muxflat);
        fclose(f_mymuxfile);
        fclose(f_muxattrs);
        fclose(f_muxout);
        fprintf(stderr, "ERROR: Unable to open mux lock file (muxlocks.dat)\n");
        exit(1);
    }

    spt3 = malloc(MALSIZE);
    memset(spt3, '\0', MALSIZE);
    pt3 = spt3;
    fseek(f_muxflat, 0L, SEEK_SET);
    fprintf(stderr, "Step 1: Quering for dbref #%d\n", atoi(argv[2]));
    i_chk = 0;

    while (!feof(f_muxflat))
    {
        fgets(g_line, sizeof(g_line), f_muxflat);
        if (i_pullname)
        {
            i_pullname = 0;
            fprintf(f_muxout, "@@ %s\n", g_line);
        }

        if (  '<' == g_line[0]
           && i_chk)
        {
            break;
        }

        if (  '!' == g_line[0]
           && (atoi(&g_line[1]) == atoi(argv[2])))
        {
            i_chk = 1;
            i_pullname = 1;
            continue;
        }

        if (  i_chk
           && '>' == g_line[0]
           && isdigit(g_line[1]))
        {
            i_chk = 2;
            i_atrcntr++;
            sprintf(s_attrval, " %d ", atoi(&g_line[1]));
            memset(s_finattr, '\0', sizeof(s_finattr));
            fseek(f_muxattrs, 0L, SEEK_SET);
            while (!feof(f_muxattrs))
            {
                fgets(g_line, sizeof(g_line), f_muxattrs);
                if (strstr(g_line, s_attrval) != NULL)
                {
                    strcpy(s_finattr, (char *)strtok(g_line, " "));
                    break;
                }
            }

            if ('\0' == s_finattr[0])
            {
                fseek(f_mymuxfile, 0L, SEEK_SET);
                while (!feof(f_mymuxfile))
                {
                    fgets(g_line, sizeof(g_line), f_mymuxfile);
                    if (strstr(g_line, s_attrval) != NULL)
                    {
                        strcpy(s_finattr, (char *)strtok(g_line, " "));
                        break;
                    }
                }
            }

            if ('\0' == s_finattr[0])
            {
                fprintf(stderr, "ERROR: Unknown error in attribute handler.");
                exit(1);
            }

            fseek(f_muxlock, 0L, SEEK_SET);
            i_lck = 0;

            while (!feof(f_muxlock))
            {
                fgets(g_line, sizeof(g_line), f_muxlock);
                if (NULL != strstr(g_line, s_attrval))
                {
                    i_lck = 1;
                    break;
                }
            }

            if (  '\0' == *s_attrib
               || !stricmp(s_finattr, s_attrib)
               || strstr(s_finattr, s_attrib))
            {
                i_atrcntr2++;
                if (i_lck)
                {
                    fprintf(f_muxout, "@lock/%s #%s=", s_finattr, argv[2]);
                }
                else if (atoi(s_attrval) < 256)
                {
                    fprintf(f_muxout, "@%s #%s=", s_finattr, argv[2]);
                }
                else
                {
                    fprintf(f_muxout, "&%s #%s=", s_finattr, argv[2]);
                }
            }
        }
        else if (2 == i_chk)
        {
            int i = 0;
            if ('"' == g_line[i])
            {
                i++;
            }

            if ('\001' == g_line[i])
            {
                while (  '\0' != g_line[i]
                      && ':'  != g_line[i])
                {
                    i++;
                }
                i++;
                while (  '\0' != g_line[i]
                      && ':'  != g_line[i])
                {
                    i++;
                }
                i++;
            }
            memset(spt3, '\0', MALSIZE);
            pt3 = spt3;
            while ('\0' != g_line[i])
            {
                char ch;
                if ('\\' == g_line[i])
                {
                    i++;
                    ch = g_line[i];
                    switch (ch)
                    {
                    case 'n':
                        ch = '\n';
                        break;

                    case 'r':
                        ch = '\r';
                        break;

                    case 'e':
                        ch = ESC_CHAR;
                        break;

                    case 't':
                        ch = '\t';
                        break;
                    }
                }
                else
                {
                    ch = g_line[i];
                    if ('"' == ch)
                    {
                         break;
                    }
                }
                i++;

                switch (ch)
                {
                case '\t':
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 't';
                     pt3++;
                     break;

                case '\n':
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'r';
                     pt3++;
                     break;

                case '\r':
                     break;

                default:
                     *pt3 = ch;
                     pt3++;
                     break;
                }
            }
            *pt3 = '\0';
            if (2 < strlen(spt3))
            {
                 *pt3 = '\n';
                 pt3++;
                 *pt3 = '\0';
            }
            if (  '\r' == *spt3
               && strlen(spt3) <= 2)
            {
                 strcpy(spt3, "%r");
            }
            if (  '\0' == *s_attrib
               || !stricmp(s_finattr, s_attrib)
               || strstr(s_finattr, s_attrib))
            {
                fprintf(f_muxout, "%s", spt3);
            }
        }
    }

    if ('\0' == *s_attrib)
    {
        fprintf(stderr, "Step 2: Writing %d attributes\n", i_atrcntr);
    }
    else
    {
        fprintf(stderr, "Step 2: Writing %d (of %d) attributes\n", i_atrcntr2, i_atrcntr);
    }
    fclose(f_muxlock);
    fclose(f_muxout);
    fclose(f_muxattrs);
    fclose(f_mymuxfile);
    fclose(f_muxflat);
    free(spt3);
    fprintf(stderr, "Step 3: Completed (file is: %s).\n", s_filename);
    return 0;
}
