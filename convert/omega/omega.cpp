#include "omega.h"
#include "p6hgame.h"
#include "t5xgame.h"

// --------------------------------------------------------------------------
// StringCloneLen: allocate memory and copy string
//
char *StringCloneLen(const char *str, size_t nStr)
{
    char *buff = (char *)malloc(nStr+1);
    if (buff)
    {
        memcpy(buff, str, nStr);
        buff[nStr] = '\0';
    }
    else
    {
        exit(1);
    }
    return buff;
}

// --------------------------------------------------------------------------
// StringClone: allocate memory and copy string
//
char *StringClone(const char *str)
{
    return StringCloneLen(str, strlen(str));
}

int main(int argc, const char *argv[])
{
    if (2 == argc)
    {
        FILE *fp = fopen(argv[1], "rb+");
        if (NULL != fp)
        {
#if 0
            extern int t5xdebug;
            t5xdebug = 1;
            extern int t5x_flex_debug;
            t5x_flex_debug = 1;
#endif
            extern int t5xparse();
            extern FILE *t5xin;
            t5xin = fp;
            t5xparse();

            g_t5xgame.Validate();
            g_t5xgame.Write(stdout);
            t5xin = NULL;
       }
    }
    return 0;
}
