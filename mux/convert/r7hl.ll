%{
#include "omega.h"
#include "r7hgame.h"
#include "r7hl.tab.hpp"
%}

%option 8bit
%option noyywrap
%option prefix="r7hl"

%%

\#-?[0-9]+     {
                   R7H_LOCKEXP *ple = new R7H_LOCKEXP;
                   ple->SetRef(atoi(r7hltext+1));
                   r7hllval.ple = ple;
                   return DBREF;
               }
[^:/&|()]+\/[^&|()]+ {
                   char *p = strchr(r7hltext, '/');
                   R7H_LOCKEXP *ple1 = new R7H_LOCKEXP;
                   ple1->SetText(StringCloneLen(r7hltext, p-r7hltext));
                   R7H_LOCKEXP *ple2 = new R7H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   R7H_LOCKEXP *ple3 = new R7H_LOCKEXP;
                   ple3->SetEval(ple1, ple2);
                   r7hllval.ple = ple3;
                   return EVALLIT;
               }
[^:/&|()]+:[^&|()]+ {
                   char *p = strchr(r7hltext, ':');
                   R7H_LOCKEXP *ple1 = new R7H_LOCKEXP;
                   ple1->SetText(StringCloneLen(r7hltext, p-r7hltext));
                   R7H_LOCKEXP *ple2 = new R7H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   R7H_LOCKEXP *ple3 = new R7H_LOCKEXP;
                   ple3->SetAttr(ple1, ple2);
                   r7hllval.ple = ple3;
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

extern R7H_LOCKEXP *g_r7hKeyExp;
int r7hlparse();

R7H_LOCKEXP *r7hl_ParseKey(char *pKey)
{
    delete g_r7hKeyExp;
    g_r7hKeyExp = NULL;

    YY_BUFFER_STATE bp = r7hl_scan_string(pKey);
    r7hl_switch_to_buffer(bp);
    R7H_LOCKEXP *ple = NULL;
    if (r7hlparse())
    {
        delete g_r7hKeyExp;
    }
    else
    {
        ple = g_r7hKeyExp;
    }
    r7hl_delete_buffer(bp);
    g_r7hKeyExp = NULL;
    return ple;
}
