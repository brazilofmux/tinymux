/*
 * unsplit.c -- filter for re-combining continuation lines 
 */
/*
 * $Id: unsplit.cpp,v 1.1 2000-04-11 07:14:48 sdennis Exp $ 
 */

#include "copyright.h"

#include <stdio.h>
#include <ctype.h>

int main(int argc, char *argv[])
{
    int c, numcr;

    while ((c = getchar()) != EOF)
    {
        if (c == '\\')
         {
            numcr = 0;
            do
            {
                c = getchar();
                if (c == '\n')
                    numcr++;
            } while ((c != EOF) && isspace(c));
            if (numcr > 1)
            {
                putchar('\n');
            }
            ungetc(c, stdin);
        }
        else
        {
            putchar(c);
        }
    }
    fflush(stdout);
    exit(0);
}
