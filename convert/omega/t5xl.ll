%{
#include "omega.h"
#include "t5xgame.h"
#include "t5xl.tab.hpp"
%}

%option 8bit
%option noyywrap
%option prefix="t5xl"

%%

\#-?[0-9]+     {
                   T5X_LOCKEXP *ple = new T5X_LOCKEXP;
                   ple->SetRef(atoi(t5xltext+1));
                   t5xllval.ple = ple;
                   return DBREF;
               }
[^:/&|()]+\/[^&|()]+ {
                   char *p = strchr(t5xltext, '/');
                   T5X_LOCKEXP *ple1 = new T5X_LOCKEXP;
                   ple1->SetText(StringCloneLen(t5xltext, p-t5xltext));
                   T5X_LOCKEXP *ple2 = new T5X_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   T5X_LOCKEXP *ple3 = new T5X_LOCKEXP;
                   ple3->SetEval(ple1, ple2);
                   t5xllval.ple = ple3;
                   return EVALLIT;
               }
[^:/&|()]+:[^&|()]+ {
                   char *p = strchr(t5xltext, ':');
                   T5X_LOCKEXP *ple1 = new T5X_LOCKEXP;
                   ple1->SetText(StringCloneLen(t5xltext, p-t5xltext));
                   T5X_LOCKEXP *ple2 = new T5X_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   T5X_LOCKEXP *ple3 = new T5X_LOCKEXP;
                   ple3->SetAttr(ple1, ple2);
                   t5xllval.ple = ple3;
                   return ATTRLIT;
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
\(             {
                   return '(';
               }
\)             {
                   return ')';
               }
[\n\t ]+       /* ignore whitespace */ ;
.              { return EOF; }
%%

extern T5X_LOCKEXP *g_t5xKeyExp;
int t5xlparse();

T5X_LOCKEXP *t5xl_ParseKey(char *pKey)
{
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
    t5xl_delete_buffer(bp);
    g_t5xKeyExp = NULL;
    return ple;
}
