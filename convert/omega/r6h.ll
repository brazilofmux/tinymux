%{
#include "omega.h"
#include "r6hgame.h"
#include "r6h.tab.hpp"
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
%option prefix="r6h"

%s afterhdr object
%x lock name value
%%

                 char aQuotedString[65536];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+V[0-9]+[\n] {
                     r6hlval.i = atoi(r6htext+2);
                     flags = r6hlval.i;
                     BEGIN(afterhdr);
                     return VHDR;
                 }
}
<afterhdr>{
  ^\+S[0-9]+[\n] {
                     r6hlval.i = atoi(r6htext+2);
                     return SIZEHINT;
                 }
  ^\+N[0-9]+[\n] {
                     r6hlval.i = atoi(r6htext+2);
                     return NEXTATTR;
                 }
  ^\-R[0-9]+[\n] {
                     r6hlval.i = atoi(r6htext+2);
                     return RECORDPLAYERS;
                 }
  ^\+A[0-9]+[\n] {
                     r6hlval.i = atoi(r6htext+2);
                     return ATTRNUM;
                 }
  ^[0-9+]+:[^\n]+[\n] {
                     r6hlval.p = StringClone(r6htext);
                     return ATTRNAME;
                 }
  ![0-9]+[\n]    {
                     r6hlval.i = atoi(r6htext+1);
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
                     r6hlval.p = StringCloneLen(r6htext, strlen(r6htext)-1);
                     BEGIN(object);
                     return OBJNAME;
                 }
}

<value>{
  ^([^\n]|\r\n)+[\n]    {
                     r6hlval.p = StringCloneLen(r6htext, strlen(r6htext)-1);
                     BEGIN(object);
                     return STRING;
                 }
}

<lock>{
 [^:/&|()\n]+\/[^&|()\n]+ {
                     char *p = strchr(r6htext, '/');
                     R6H_LOCKEXP *ple1 = new R6H_LOCKEXP;
                     ple1->SetText(StringCloneLen(r6htext, p-r6htext));
                     R6H_LOCKEXP *ple2 = new R6H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     R6H_LOCKEXP *ple3 = new R6H_LOCKEXP;
                     ple3->SetEval(ple1, ple2);
                     r6hlval.ple = ple3;
                     return EVALLIT;
                 }
 [^:/&|()\n]+:[^&|()\n]+ {
                     char *p = strchr(r6htext, ':');
                     R6H_LOCKEXP *ple1 = new R6H_LOCKEXP;
                     ple1->SetText(StringCloneLen(r6htext, p-r6htext));
                     R6H_LOCKEXP *ple2 = new R6H_LOCKEXP;
                     ple2->SetText(StringClone(p+1));
                     R6H_LOCKEXP *ple3 = new R6H_LOCKEXP;
                     ple3->SetAttr(ple1, ple2);
                     r6hlval.ple = ple3;
                     return ATTRLIT;
                 }
 [0-9]+          {
                     R6H_LOCKEXP *ple = new R6H_LOCKEXP;
                     ple->SetRef(atoi(r6htext));
                     r6hlval.ple = ple;
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
                     r6hlval.i = atoi(r6htext+1);
                     iObjectField = 1;
                     BEGIN(name);
                     return OBJECT;
                 }
  -?[0-9]+[\n]   {
                     r6hlval.i = atoi(r6htext);
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
                     r6hlval.i = atoi(r6htext+1);
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
