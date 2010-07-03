#ifndef _OMEGA_H_
#define _OMEGA_H_

#include "autoconf.h"

#include <stdio.h>
#include <memory.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <vector>
#include <map>
using namespace std;

char *StringClone(const char *str);
char *StringCloneLen(const char *str, size_t nStr);

typedef struct
{
    const char *pName;
    const int  mask;
} NameMask;

#define OMEGA_VERSION "1.0.0.0"

#endif
