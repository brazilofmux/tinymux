// unsplit.cpp -- Filter for re-combining continuation lines.
//
// $Id: unsplit.cpp,v 1.3 2001-11-18 20:48:08 sdennis Exp $
//

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
    return 0;
}
