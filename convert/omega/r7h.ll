%{
#include "omega.h"
#include "r7hgame.h"
#include "r7h.tab.hpp"
static int  iLockNest;

// Because the lock does not start with an open paren,
// we rely on the position of the lock within the object header.
//
static int  flags;
static int  iObjectField = -1;

%}

%option 8bit
%option yylineno
%option noyywrap
%option prefix="r7h"

%s afterhdr object
%x lock name value
%%

                 char aQuotedString[65536];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+V[0-9]+[\n] {
                     r7hlval.i = atoi(r7htext+2);
                     flags = r7hlval.i;
                     BEGIN(afterhdr);
                     return VHDR;
                 }
}
<afterhdr>{
  ^\+S[0-9]+[\n] {
                     r7hlval.i = atoi(r7htext+2);
                     return SIZEHINT;
                 }
  ^\+N[0-9]+[\n] {
                     r7hlval.i = atoi(r7htext+2);
                     return NEXTATTR;
                 }
  ^\-R[0-9]+[\n] {
                     r7hlval.i = atoi(r7htext+2);
                     return RECORDPLAYERS;
                 }
  ^\+A[0-9]+[\n] {
                     r7hlval.i = atoi(r7htext+2);
                     return ATTRNUM;
                 }
  ^[0-9+]+:[^\n]+[\n] {
                     r7hlval.p = StringCloneLen(r7htext, strlen(r7htext)-1);
                     return ATTRNAME;
                 }
  ![0-9]+[\n]    {
                     r7hlval.i = atoi(r7htext+1);
                     BEGIN(name);
                     iObjectField = 1;
                     return OBJECT;
                 }
  "***END OF DUMP***" {
                     return EOD;
                 }
}

<name>{
  ^[^\n]+[\n]    {
                     r7hlval.p = StringCloneLen(r7htext, strlen(r7htext)-1);
                     BEGIN(object);
                     return OBJNAME;
                 }
}

<value>{
  ^([^\n]|\r\n)+[\n]    {
                     r7hlval.p = StringCloneLen(r7htext, strlen(r7htext)-1);
                     BEGIN(object);
                     return STRING;
                 }
}

<lock>{
 [^:/&|()\n]+\/[^&|()\n]+ {
                     char *p = strchr(r7htext, '/');
                     R7H_LOCKEXP *ple1 = new R7H_LOCKEXP;
                     ple1->SetText(StringCloneLen(r7htext, p-r7htext));
                     R7H_LOCKEXP *ple2 = new R7H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     R7H_LOCKEXP *ple3 = new R7H_LOCKEXP;
                     ple3->SetEval(ple1, ple2);
                     r7hlval.ple = ple3;
                     return EVALLIT;
                 }
 [^:/&|()\n]+:[^&|()\n]+ {
                     char *p = strchr(r7htext, ':');
                     R7H_LOCKEXP *ple1 = new R7H_LOCKEXP;
                     ple1->SetText(StringCloneLen(r7htext, p-r7htext));
                     R7H_LOCKEXP *ple2 = new R7H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     R7H_LOCKEXP *ple3 = new R7H_LOCKEXP;
                     ple3->SetAttr(ple1, ple2);
                     r7hlval.ple = ple3;
                     return ATTRLIT;
                 }
 [0-9]+          {
                     R7H_LOCKEXP *ple = new R7H_LOCKEXP;
                     ple->SetRef(atoi(r7htext));
                     r7hlval.ple = ple;
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
                     r7hlval.i = atoi(r7htext+1);
                     iObjectField = 1;
                     BEGIN(name);
                     return OBJECT;
                 }
  -?[0-9]+[\n]   {
                     r7hlval.i = atoi(r7htext);
                     if (0 < iObjectField)
                     {
                         iObjectField++;
                         if (6 == iObjectField)
                         {
                             iObjectField = -1;
                             BEGIN(lock);
                             return INTEGER;
                         }
                     }
                     return INTEGER;
                 }
 \>[0-9]+[\n]    {
                     r7hlval.i = atoi(r7htext+1);
                     iObjectField = -1;
                     BEGIN(value);
                     return ATTRREF;
                 }
 \<[\n]          {
                     iObjectField = -1;
                     BEGIN(afterhdr);
                     return '<';
                 }
}

[\n]             {
                     iObjectField = -1;
                 }
[\t ]+           /* ignore whitespace */ ;
.                { return EOF; }
%%
