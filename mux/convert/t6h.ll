%{
#include "omega.h"
#include "t6hgame.h"
#include "t6h.tab.hpp"
static int  iLockNest;

// Because the lock does not start with an open paren,
// we rely on the position of the lock within the object header.
//
static int  flags;
static int  iObjectField = -1;

static int ObjectToken()
{
    if (flags & T6H_V_TIMESTAMPS)
    {
        if (flags & T6H_V_CREATETIME)
        {
            return OBJECT_V1_TS_CT;
        }
        else
        {
            return OBJECT_V1_TS;
        }
    }
    else
    {
        return OBJECT_V1;
    }
}
%}

%option 8bit
%option yylineno
%option noyywrap
%option prefix="t6h"

%s afterhdr object
%x lock
%x str
%%

                 char aQuotedString[65536];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+T[0-9]+[\n] {
                     t6hlval.i = atoi(t6htext+2);
                     flags = t6hlval.i;
                     BEGIN(afterhdr);
                     return THDR;
                 }
}
<afterhdr>{
  ^\+S[0-9]+[\n] {
                     t6hlval.i = atoi(t6htext+2);
                     return SIZEHINT;
                 }
  ^\+N[0-9]+[\n] {
                     t6hlval.i = atoi(t6htext+2);
                     return NEXTATTR;
                 }
  ^\-R[0-9]+[\n] {
                     t6hlval.i = atoi(t6htext+2);
                     return RECORDPLAYERS;
                 }
  ^\+A[0-9]+[\n] {
                     t6hlval.i = atoi(t6htext+2);
                     return ATTRNUM;
                 }
  ![0-9]+[\n]    {
                     t6hlval.i = atoi(t6htext+1);
                     BEGIN(object);
                     iObjectField = 1;
                     return ObjectToken();
                 }
  "***END OF DUMP***" {
                     return EOD;
                 }
}

<lock>{
 [^:/&|()\n]+\/[^&|()\n]+ {
                     char *p = strchr(t6htext, '/');
                     T6H_LOCKEXP *ple1 = new T6H_LOCKEXP;
                     ple1->SetText(StringCloneLen(t6htext, p-t6htext));
                     T6H_LOCKEXP *ple2 = new T6H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     T6H_LOCKEXP *ple3 = new T6H_LOCKEXP;
                     ple3->SetEval(ple1, ple2);
                     t6hlval.ple = ple3;
                     return EVALLIT;
                 }
 [^:/&|()\n]+:[^&|()\n]+ {
                     char *p = strchr(t6htext, ':');
                     T6H_LOCKEXP *ple1 = new T6H_LOCKEXP;
                     ple1->SetText(StringCloneLen(t6htext, p-t6htext));
                     T6H_LOCKEXP *ple2 = new T6H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     T6H_LOCKEXP *ple3 = new T6H_LOCKEXP;
                     ple3->SetAttr(ple1, ple2);
                     t6hlval.ple = ple3;
                     return ATTRLIT;
                 }
 [0-9]+          {
                     T6H_LOCKEXP *ple = new T6H_LOCKEXP;
                     ple->SetRef(atoi(t6htext));
                     t6hlval.ple = ple;
                     return DBREF;
                 }
 \(              {
                     iLockNest++;
                     return '(';
                 }
 \)              {
                     if (0 == --iLockNest)
                     {
                         BEGIN(object);
                     }
                     return ')';
                 }
 \=              {
                     return '=';
                 }
 \+              {
                     return '+';
                 }
 \@              {
                     return '@';
                 }
 \$              {
                     return '$';
                 }
 \&              {
                     return '&';
                 }
 \|              {
                     return '|';
                 }
 \!              {
                     return '!';
                 }
 [\n]            {
                     if (0 == iLockNest)
                     {
                         BEGIN(object);
                     }
                 }
}

<object>{
  ![0-9]+[\n]    {
                     t6hlval.i = atoi(t6htext+1);
                     iObjectField = 1;
                     return ObjectToken();
                 }
  -?[0-9]+[\n]   {
                     t6hlval.i = atoi(t6htext);
                     if (0 < iObjectField)
                     {
                         iObjectField++;
                         if (7 == iObjectField)
                         {
                             iObjectField = -1;
                             BEGIN(lock);
                             return INTEGER;
                         }
                     }
                     return INTEGER;
                 }
 \>[0-9]+[\n]    {
                     t6hlval.i = atoi(t6htext+1);
                     iObjectField = -1;
                     return ATTRREF;
                 }
 \<[\n]          {
                     iObjectField = -1;
                     BEGIN(afterhdr);
                     return '<';
                 }
}

\"               {
                     pQuotedString = aQuotedString;
                     iPreStrContext = YY_START;
                     BEGIN(str);
                 }
<str>{
  \"[\t ]*[\n]   {
                     *pQuotedString = '\0';
                     t6hlval.p = StringClone(aQuotedString);
                     BEGIN(iPreStrContext);
                     return STRING;
                 }
  \\[enrt\\\"] {
                     if (pQuotedString < aQuotedString + sizeof(aQuotedString) - 1)
                     {
                         switch (t6htext[1])
                         {
                         case 'r':
                             g_t6hgame.SawExtraEscapes();
                             *pQuotedString++ = '\r';
                             break;

                         case 'n':
                             g_t6hgame.SawExtraEscapes();
                             *pQuotedString++ = '\n';
                             break;

                         case 'e':
                             g_t6hgame.SawExtraEscapes();
                             *pQuotedString++ = 0x1B;
                             break;

                         case 't':
                             g_t6hgame.SawExtraEscapes();
                             *pQuotedString++ = '\t';
                             break;

                         default:
                             *pQuotedString++ = t6htext[1];
                             break;
                         }
                     }
                 }
  [^\\\"]+       {
                     char *p = t6htext;
                     while (  '\0' != *p
                           && pQuotedString < aQuotedString + sizeof(aQuotedString) - 1)
                     {
                         *pQuotedString++ = *p++;
                     }
                 }
}
[\n]             {
                     iObjectField = -1;
                 }
[\t ]+           /* ignore whitespace */ ;
.                { return EOF; }
%%
