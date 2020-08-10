#ifndef _OMEGA_H_
#define _OMEGA_H_

#include "autoconf.h"

#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include <vector>
#include <map>
using namespace std;

#define LBUF_SIZE    65536
#define ESC_CHAR     '\x1b'

char *StringClone(const char *str);
char *StringCloneLen(const char *str, size_t nStr);
bool ConvertTimeString(char *pTime, time_t *pt);

typedef struct
{
    const char *pName;
    const unsigned mask;
} NameMask;

struct ltstr
{
    bool operator()(const char* s1, const char* s2) const
    {
        return strcmp(s1, s2) < 0;
    }
};

struct lti
{
    bool operator()(int i1, int i2) const
    {
        return i1 < i2;
    }
};

#define OMEGA_VERSION "1.2.1.0"

#endif
