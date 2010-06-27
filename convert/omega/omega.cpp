#include "omega.h"
#include "p6hgame.h"

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
            extern int p6hdebug;
            p6hdebug = 1;
#endif
            extern int p6hparse();
            extern FILE *p6hin;
            p6hin = fp;
            p6hparse();

            g_p6hgame.Validate();
            g_p6hgame.Write(stdout);
            p6hin = NULL;
       }
    }
    return 0;
}
