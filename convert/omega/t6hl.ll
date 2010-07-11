%{
#include "omega.h"
#include "t6hgame.h"
#include "t6hl.tab.hpp"
%}

%option 8bit
%option noyywrap
%option prefix="t6hl"

%%

\#-?[0-9]+     {
                   T6H_LOCKEXP *ple = new T6H_LOCKEXP;
                   ple->SetRef(atoi(t6hltext+1));
                   t6hllval.ple = ple;
                   return DBREF;
               }
[^:/&|()]+\/[^&|()]+ {
                   char *p = strchr(t6hltext, '/');
                   T6H_LOCKEXP *ple1 = new T6H_LOCKEXP;
                   ple1->SetText(StringCloneLen(t6hltext, p-t6hltext));
                   T6H_LOCKEXP *ple2 = new T6H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   T6H_LOCKEXP *ple3 = new T6H_LOCKEXP;
                   ple3->SetEval(ple1, ple2);
                   t6hllval.ple = ple3;
                   return EVALLIT;
               }
[^:/&|()]+:[^&|()]+ {
                   char *p = strchr(t6hltext, ':');
                   T6H_LOCKEXP *ple1 = new T6H_LOCKEXP;
                   ple1->SetText(StringCloneLen(t6hltext, p-t6hltext));
                   T6H_LOCKEXP *ple2 = new T6H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   T6H_LOCKEXP *ple3 = new T6H_LOCKEXP;
                   ple3->SetAttr(ple1, ple2);
                   t6hllval.ple = ple3;
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

extern T6H_LOCKEXP *g_t6hKeyExp;
int t6hlparse();

T6H_LOCKEXP *t6hl_ParseKey(char *pKey)
{
    delete g_t6hKeyExp;
    g_t6hKeyExp = NULL;

    YY_BUFFER_STATE bp = t6hl_scan_string(pKey);
    t6hl_switch_to_buffer(bp);
    T6H_LOCKEXP *ple = NULL;
    if (t6hlparse())
    {
        delete g_t6hKeyExp;
    }
    else
    {
        ple = g_t6hKeyExp;
    }
    t6hl_delete_buffer(bp);
    g_t6hKeyExp = NULL;
    return ple;
}
