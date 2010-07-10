%{
#include "omega.h"
#include "r6hgame.h"
#include "r6hl.tab.hpp"
%}

%option 8bit 
%option noyywrap
%option prefix="r6hl"

%%

\#-?[0-9]+     {
                   R6H_LOCKEXP *ple = new R6H_LOCKEXP;
                   ple->SetRef(atoi(r6hltext+1));
                   r6hllval.ple = ple;
                   return DBREF;
               }
[^:/&|()]+\/[^&|()]+ {
                   char *p = strchr(r6hltext, '/');
                   R6H_LOCKEXP *ple1 = new R6H_LOCKEXP;
                   ple1->SetText(StringCloneLen(r6hltext, p-r6hltext));
                   R6H_LOCKEXP *ple2 = new R6H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   R6H_LOCKEXP *ple3 = new R6H_LOCKEXP;
                   ple3->SetEval(ple1, ple2);
                   r6hllval.ple = ple3;
                   return EVALLIT;
               }
[^:/&|()]+:[^&|()]+ {
                   char *p = strchr(r6hltext, ':');
                   R6H_LOCKEXP *ple1 = new R6H_LOCKEXP;
                   ple1->SetText(StringCloneLen(r6hltext, p-r6hltext));
                   R6H_LOCKEXP *ple2 = new R6H_LOCKEXP;
                   ple2->SetText(StringClone(p+1));
                   R6H_LOCKEXP *ple3 = new R6H_LOCKEXP;
                   ple3->SetAttr(ple1, ple2);
                   r6hllval.ple = ple3;
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

extern R6H_LOCKEXP *g_r6hKeyExp;
int r6hlparse();

R6H_LOCKEXP *r6hl_ParseKey(char *pKey)
{
    //extern int r6hl_flex_debug;
    //extern int r6hldebug;
    //r6hl_flex_debug = 1;
    //r6hldebug = 1;

    delete g_r6hKeyExp;
    g_r6hKeyExp = NULL;
    
    YY_BUFFER_STATE bp = r6hl_scan_string(pKey);
    r6hl_switch_to_buffer(bp);
    R6H_LOCKEXP *ple = NULL;
    if (r6hlparse())
    {
        delete g_r6hKeyExp;
    }
    else
    {
        ple = g_r6hKeyExp;
    }
    g_r6hKeyExp = NULL;
    return ple;
}
