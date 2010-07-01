/*! \file dbpuller.c
 * \brief Offline flatfile-to-softcode converter.
 *
 * $Id$
 *
 * Db puller - used to pull data from a TinyMUX flatfile and dump it into a
 * file in \@decompile format.
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

// The following should match LBUF_SIZE and SBUF_SIZE in alloc.h
//
#define LBUF_SIZE 8000
#define SBUF_SIZE 64
#define ESC_CHAR '\033'

// As buffers are written to a flatfile, some characters are escaped. The
// worst-case contents of an lbuf can be twice as long as what is allowed within
// the game.  Further, there are four additional characters plus trailing null.
//
// "            ==> 1
// Encoded LBUF ==> 2*LBUF_SIZE
// "\r\n\0      ==> 4
// 
#define MALSIZE_IN (2*LBUF_SIZE+5)

// 3-byte color code points turn into 3-byte %x-subs.
// 5-byte ANSI sequences also turn into 3-byte %x-subs.
// There is some punctuation and the attribute name in front.
//
#define MALSIZE_OUT (2*LBUF_SIZE + SBUF_SIZE + 6)

#define COLOR_RESET      256
#define COLOR_INTENSE    257
#define COLOR_UNDERLINE  258
#define COLOR_BLINK      259
#define COLOR_INVERSE    260
#define COLOR_FG_BLACK   261
#define COLOR_FG_RED     262
#define COLOR_FG_GREEN   263
#define COLOR_FG_YELLOW  264
#define COLOR_FG_BLUE    265
#define COLOR_FG_MAGENTA 266
#define COLOR_FG_CYAN    267
#define COLOR_FG_WHITE   268
#define COLOR_BG_BLACK   269
#define COLOR_BG_RED     270
#define COLOR_BG_GREEN   271
#define COLOR_BG_YELLOW  272
#define COLOR_BG_BLUE    273
#define COLOR_BG_MAGENTA 274
#define COLOR_BG_CYAN    275
#define COLOR_BG_WHITE   276

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

char g_line[MALSIZE_IN];

int main(int argc, char **argv)
{
    FILE *f_muxflat    = NULL;
    FILE *f_mymuxfile = NULL;
    FILE *f_muxattrs  = NULL;
    FILE *f_muxout     = NULL;
    FILE *f_muxlock    = NULL;
    char *spt3;
    char *pt3;
    char s_attrib[SBUF_SIZE+1];
    char s_filename[80];
    char s_attrval[SBUF_SIZE+1];
    char s_attr[SBUF_SIZE+1];
    char s_finattr[SBUF_SIZE+1];
    int i_chk = 0;
    int i_lck = 1;
    int i_atrcntr = 0;
    int i_atrcntr2 = 0;
    int i_pullname = 0;
    int i;

    if (argc < 3)
    {
        fprintf(stderr, "Syntax: %s mux-flatfile dbref# (no preceeding # character) [optional attribute-name]\n", argv[0]);
        exit(1);
    }

    f_muxflat = fopen(argv[1], "r");
    if (NULL == f_muxflat)
    {
        fprintf(stderr, "ERROR: Unable to open %s for reading.\n", argv[1]);
        exit(1);
    }

    for (i = 0; argv[2][i]; i++)
    {
        if (!isdigit(argv[2][i]))
        {
            fprintf(stderr, "ERROR: Dbref# must be an integer (no # preceeding) [optional attribute-name]\n");
            fclose(f_muxflat);
            exit(1);
        }
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
        strncpy(s_attrib, argv[3], SBUF_SIZE);
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

    spt3 = (char *)malloc(MALSIZE_OUT);
    memset(spt3, '\0', MALSIZE_OUT);
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
            memset(spt3, '\0', MALSIZE_OUT);
            pt3 = spt3;
            while (  '\0' != g_line[i]
                  && '"'  != g_line[i])
            {
                int ch = g_line[i++];
                if ('\\' == ch)
                {
                    ch = g_line[i++];
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
                else if ('\xEF' == ch)
                {
                    if ('\x94' == g_line[i])
                    {
                        if ('\x80' == g_line[i+1])
                        {
                            ch = COLOR_RESET;
                            i += 2;
                        }
                        else if ('\x81' == g_line[i+1])
                        {
                            ch = COLOR_INTENSE;
                            i += 2;
                        }
                        else if ('\x84' == g_line[i+1])
                        {
                            ch = COLOR_UNDERLINE;
                            i += 2;
                        }
                        else if ('\x85' == g_line[i+1])
                        {
                            ch = COLOR_BLINK;
                            i += 2;
                        }
                        else if ('\x87' == g_line[i+1])
                        {
                            ch = COLOR_INVERSE;
                            i += 2;
                        }
                    }
                    else if ('\x98' == g_line[i])
                    {
                        if ('\x80' == g_line[i+1])
                        {
                            ch = COLOR_FG_BLACK;
                            i += 2;
                        }
                        else if ('\x81' == g_line[i+1])
                        {
                            ch = COLOR_FG_RED;
                            i += 2;
                        }
                        else if ('\x82' == g_line[i+1])
                        {
                            ch = COLOR_FG_GREEN;
                            i += 2;
                        }
                        else if ('\x83' == g_line[i+1])
                        {
                            ch = COLOR_FG_YELLOW;
                            i += 2;
                        }
                        else if ('\x84' == g_line[i+1])
                        {
                            ch = COLOR_FG_BLUE;
                            i += 2;
                        }
                        else if ('\x85' == g_line[i+1])
                        {
                            ch = COLOR_FG_MAGENTA;
                            i += 2;
                        }
                        else if ('\x86' == g_line[i+1])
                        {
                            ch = COLOR_FG_CYAN;
                            i += 2;
                        }
                        else if ('\x87' == g_line[i+1])
                        {
                            ch = COLOR_FG_WHITE;
                            i += 2;
                        }
                    }
                    else if ('\x9C' == g_line[i])
                    {
                        if ('\x80' == g_line[i+1])
                        {
                            ch = COLOR_BG_BLACK;
                            i += 2;
                        }
                        else if ('\x81' == g_line[i+1])
                        {
                            ch = COLOR_BG_RED;
                            i += 2;
                        }
                        else if ('\x82' == g_line[i+1])
                        {
                            ch = COLOR_BG_GREEN;
                            i += 2;
                        }
                        else if ('\x83' == g_line[i+1])
                        {
                            ch = COLOR_BG_YELLOW;
                            i += 2;
                        }
                        else if ('\x84' == g_line[i+1])
                        {
                            ch = COLOR_BG_BLUE;
                            i += 2;
                        }
                        else if ('\x85' == g_line[i+1])
                        {
                            ch = COLOR_BG_MAGENTA;
                            i += 2;
                        }
                        else if ('\x86' == g_line[i+1])
                        {
                            ch = COLOR_BG_CYAN;
                            i += 2;
                        }
                        else if ('\x87' == g_line[i+1])
                        {
                            ch = COLOR_BG_WHITE;
                            i += 2;
                        }
                    }
                }

                if (ESC_CHAR == ch)
                {
                    if (  '[' == g_line[i]
                       && '\0' != g_line[i+1]
                       && 'm' == g_line[i+2])
                    {
                        if ('0' == g_line[i+1])
                        {
                            ch = COLOR_RESET;
                        }
                        else if ('1' == g_line[i+1])
                        {
                            ch = COLOR_INTENSE;
                        }
                        else if ('4' == g_line[i+1])
                        {
                            ch = COLOR_UNDERLINE;
                        }
                        else if ('5' == g_line[i+1])
                        {
                            ch = COLOR_BLINK;
                        }
                        else if ('7' == g_line[i+1])
                        {
                            ch = COLOR_INVERSE;
                        }
                        else
                        {
                            continue;
                        }
                        i += 3;
                    }
                    else if (  '[' == g_line[i]
                            && '\0' != g_line[i+1]
                            && '\0' != g_line[i+2]
                            && 'm' == g_line[i+3])
                    {
                        if ('3' == g_line[i+1])
                        {
                            if ('0' == g_line[i+2])
                            {
                                ch = COLOR_FG_BLACK;
                            }
                            else if ('1' == g_line[i+2])
                            {
                                ch = COLOR_FG_RED;
                            }
                            else if ('2' == g_line[i+2])
                            {
                                ch = COLOR_FG_GREEN;
                            }
                            else if ('3' == g_line[i+2])
                            {
                                ch = COLOR_FG_YELLOW;
                            }
                            else if ('4' == g_line[i+2])
                            {
                                ch = COLOR_FG_BLUE;
                            }
                            else if ('5' == g_line[i+2])
                            {
                                ch = COLOR_FG_MAGENTA;
                            }
                            else if ('6' == g_line[i+2])
                            {
                                ch = COLOR_FG_CYAN;
                            }
                            else if ('7' == g_line[i+2])
                            {
                                ch = COLOR_FG_WHITE;
                            }
                            else
                            {
                                continue;
                            }
                        }
                        else if ('4' == g_line[i+1])
                        {
                            if ('0' == g_line[i+2])
                            {
                                ch = COLOR_BG_BLACK;
                            }
                            else if ('1' == g_line[i+2])
                            {
                                ch = COLOR_BG_RED;
                            }
                            else if ('2' == g_line[i+2])
                            {
                                ch = COLOR_BG_GREEN;
                            }
                            else if ('3' == g_line[i+2])
                            {
                                ch = COLOR_BG_YELLOW;
                            }
                            else if ('4' == g_line[i+2])
                            {
                                ch = COLOR_BG_BLUE;
                            }
                            else if ('5' == g_line[i+2])
                            {
                                ch = COLOR_BG_MAGENTA;
                            }
                            else if ('6' == g_line[i+2])
                            {
                                ch = COLOR_BG_CYAN;
                            }
                            else if ('7' == g_line[i+2])
                            {
                                ch = COLOR_BG_WHITE;
                            }
                            else
                            {
                                continue;
                            }
                        }
                        else
                        {
                            continue;
                        }
                        i += 4;
                    }
                    else
                    {
                        continue;
                    }
                }

                switch (ch)
                {
                default:
                     *pt3 = ch;
                     pt3++;
                     break;

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

                case COLOR_RESET:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'n';
                     pt3++;
                     break;

                case COLOR_INTENSE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'h';
                     pt3++;
                     break;

                case COLOR_UNDERLINE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'u';
                     pt3++;
                     break;

                case COLOR_BLINK:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'f';
                     pt3++;
                     break;

                case COLOR_INVERSE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'i';
                     pt3++;
                     break;

                case COLOR_FG_BLACK:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     break;

                case COLOR_FG_RED:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'r';
                     pt3++;
                     break;

                case COLOR_FG_GREEN:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'g';
                     pt3++;
                     break;

                case COLOR_FG_YELLOW:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'y';
                     pt3++;
                     break;

                case COLOR_FG_BLUE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'b';
                     pt3++;
                     break;

                case COLOR_FG_MAGENTA:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'm';
                     pt3++;
                     break;

                case COLOR_FG_CYAN:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'c';
                     pt3++;
                     break;

                case COLOR_FG_WHITE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'w';
                     pt3++;
                     break;

                case COLOR_BG_BLACK:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'X';
                     pt3++;
                     break;

                case COLOR_BG_RED:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'R';
                     pt3++;
                     break;

                case COLOR_BG_GREEN:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'G';
                     pt3++;
                     break;

                case COLOR_BG_YELLOW:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'Y';
                     pt3++;
                     break;

                case COLOR_BG_BLUE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'B';
                     pt3++;
                     break;

                case COLOR_BG_MAGENTA:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'M';
                     pt3++;
                     break;

                case COLOR_BG_CYAN:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'C';
                     pt3++;
                     break;

                case COLOR_BG_WHITE:
                     *pt3 = '%';
                     pt3++;
                     *pt3 = 'x';
                     pt3++;
                     *pt3 = 'W';
                     pt3++;
                     break;
                }
            }
            *pt3 = '\0';

            if (  '\0' == *s_attrib
               || !stricmp(s_finattr, s_attrib)
               || strstr(s_finattr, s_attrib))
            {
                fprintf(f_muxout, "%s\n", spt3);
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
