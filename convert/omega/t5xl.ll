%{
#include "omega.h"
#include "t5xgame.h"
#include "t5xl.tab.hpp"
%}

%option 8bit 
%option noyywrap
%option prefix="t5xl"

%x str
%%

\#-?[0-9]+     {
                   t5xllval.i = atoi(t5xltext+1);
                   return DBREF;
               }
\=             {
                   return '=';
               }
\+             {
                   return '+';
               }
\@             {
                   return '@';
               }
\$             {
                   return '$';
               }
\&             {
                   return '&';
               }
\|             {
                   return '|';
               }
\!             {
                   return '!';
               }
\:             {
                   return ':';
               }
\/             {
                   return '/';
               }
\^             {
                   return '^';
               }
\(             {
                   return '(';
               }
\)             {
                   return ')';
               }
[^()=+@$&|!:/\n\t ]+  {
                   t5xllval.p = StringClone(t5xltext);
                   return LTEXT;
               }
[\n\t ]+       /* ignore whitespace */ ;
%%

extern T5X_LOCKEXP *g_t5xKeyExp;
int t5xlparse();

T5X_LOCKEXP *t5xl_ParseKey(char *pKey)
{
    //extern int t5xl_flex_debug;
    //extern int t5xldebug;
    //t5xl_flex_debug = 1;
    //t5xldebug = 1;

    delete g_t5xKeyExp;
    g_t5xKeyExp = NULL;
    
    YY_BUFFER_STATE bp = t5xl_scan_string(pKey);
    t5xl_switch_to_buffer(bp);
    T5X_LOCKEXP *ple = NULL;
    if (t5xlparse())
    {
        delete g_t5xKeyExp;
    }
    else
    {
        ple = g_t5xKeyExp;
    }
    g_t5xKeyExp = NULL;
    return ple;
}
