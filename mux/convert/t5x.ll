%{
#include "omega.h"
#include "t5xgame.h"
#include "t5x.tab.hpp"
static int  iLockNest;

// Because the lock does not start with an open paren in v1 and v2 flatfiles,
// we rely on the position of the lock within the object header.
//
static int  ver;
static int  iObjectField = -1;
%}

%option 8bit
%option yylineno
%option noyywrap
%option prefix="t5x"

%s afterhdr object
%x lock
%x str
%%

                 char aQuotedString[65536];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+X[0-9]+[\n] {
                     t5xlval.i = atoi(t5xtext+2);
                     ver = (t5xlval.i & T5X_V_MASK);
                     BEGIN(afterhdr);
                     return XHDR;
                 }
}
<afterhdr>{
  ^\+S[0-9]+[\n] {
                     t5xlval.i = atoi(t5xtext+2);
                     return SIZEHINT;
                 }
  ^\+N[0-9]+[\n] {
                     t5xlval.i = atoi(t5xtext+2);
                     return NEXTATTR;
                 }
  ^\-R[0-9]+[\n] {
                     t5xlval.i = atoi(t5xtext+2);
                     return RECORDPLAYERS;
                 }
  ^\+A[0-9]+[\n] {
                     t5xlval.i = atoi(t5xtext+2);
                     return ATTRNUM;
                 }
  ![0-9]+[\n]    {
                     t5xlval.i = atoi(t5xtext+1);
                     BEGIN(object);
                     if (ver <= 2)
                     {
                         iObjectField = 1;
                     }
                     return (3 == ver || 4 == ver)? OBJECT_V34 : OBJECT_V12;
                 }
  "***END OF DUMP***" {
                     return EOD;
                 }
}

<lock>{
 [^:/&|()\n]+\/[^&|()\n]+ {
                     char *p = strchr(t5xtext, '/');
                     T5X_LOCKEXP *ple1 = new T5X_LOCKEXP;
                     ple1->SetText(StringCloneLen(t5xtext, p-t5xtext));
                     T5X_LOCKEXP *ple2 = new T5X_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     T5X_LOCKEXP *ple3 = new T5X_LOCKEXP;
                     ple3->SetEval(ple1, ple2);
                     t5xlval.ple = ple3;
                     return EVALLIT;
                 }
 [^:/&|()\n]+:[^&|()\n]+ {
                     char *p = strchr(t5xtext, ':');
                     T5X_LOCKEXP *ple1 = new T5X_LOCKEXP;
                     ple1->SetText(StringCloneLen(t5xtext, p-t5xtext));
                     T5X_LOCKEXP *ple2 = new T5X_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     T5X_LOCKEXP *ple3 = new T5X_LOCKEXP;
                     ple3->SetAttr(ple1, ple2);
                     t5xlval.ple = ple3;
                     return ATTRLIT;
                 }
 [0-9]+          {
                     T5X_LOCKEXP *ple = new T5X_LOCKEXP;
                     ple->SetRef(atoi(t5xtext));
                     t5xlval.ple = ple;
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
                     if (  ver <= 2
                        && 0 == iLockNest)
                     {
                         BEGIN(object);
                     }
                 }
}

<object>{
  ![0-9]+[\n]    {
                     t5xlval.i = atoi(t5xtext+1);
                     if (ver <= 2)
                     {
                         iObjectField = 1;
                     }
                     return (3 == ver || 4 == ver)? OBJECT_V34 : OBJECT_V12;
                 }
  -?[0-9]+[\n]   {
                     t5xlval.i = atoi(t5xtext);
                     if (ver <= 2)
                     {
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
                     }
                     return INTEGER;
                 }
 \>[0-9]+[\n]    {
                     t5xlval.i = atoi(t5xtext+1);
                     if (ver <= 2)
                     {
                         iObjectField = -1;
                     }
                     return ATTRREF;
                 }
 \<[\n]          {
                     if (ver <= 2)
                     {
                         iObjectField = -1;
                     }
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
                     t5xlval.p = StringClone(aQuotedString);
                     BEGIN(iPreStrContext);
                     return STRING;
                 }
  \\[enrt\\\"] {
                     if (pQuotedString < aQuotedString + sizeof(aQuotedString) - 1)
                     {
                         switch (t5xtext[1])
                         {
                         case 'r':
                             *pQuotedString++ = '\r';
                             break;
                         case 'n':
                             *pQuotedString++ = '\n';
                             break;
                         case 'e':
                             *pQuotedString++ = 0x1B;
                             break;
                         case 't':
                             *pQuotedString++ = '\t';
                             break;
                         default:
                             *pQuotedString++ = t5xtext[1];
                             break;
                         }
                     }
                 }
  [^\\\"]+       {
                     char *p = t5xtext;
                     while (  '\0' != *p
                           && pQuotedString < aQuotedString + sizeof(aQuotedString) - 1)
                     {
                         *pQuotedString++ = *p++;
                     }
                 }
}
[\n]             {
                     if (ver <= 2)
                     {
                         iObjectField = -1;
                     }
                 }
[\t ]+           /* ignore whitespace */ ;
.                { return EOF; }
%%
