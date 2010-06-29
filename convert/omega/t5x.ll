%{
#include "omega.h"
#include "t5xgame.h"
#include "t5x.tab.hpp"
%}

%option 8bit 
%option yylineno
%option noyywrap
%option prefix="t5x"

%s afterhdr object
%x str
%%

                 char aQuotedString[10000];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+X[0-9]+     {
                     t5xlval.i = atoi(t5xtext+2);
                     BEGIN(afterhdr);
                     return XHDR;
                 }
}
<afterhdr>{
  ^\+S[0-9]+     {
                     t5xlval.i = atoi(t5xtext+2);
                     return OBJECTCOUNT;
                 }
  ^\+N[0-9]+     {
                     t5xlval.i = atoi(t5xtext+2);
                     return NEXTATTR;
                 }
  ^\-R[0-9]+     {
                     t5xlval.i = atoi(t5xtext+2);
                     return RECORDPLAYERS;
                 }
  ^\+A[0-9]+     {
                     t5xlval.i = atoi(t5xtext+2);
                     return ATTRNAME;
                 }
  -?[0-9]+       {
                     t5xlval.i = atoi(yytext);
                     return INTEGER;
                 }
  ![0-9]+        {
                     t5xlval.i = atoi(t5xtext+1);
                     BEGIN(object);
                     return OBJECT;
                 }
}

<object>{
  ![0-9]+        {
                     t5xlval.i = atoi(t5xtext+1);
                     return OBJECT;
                 }
  \#-?[0-9]+     {
                     t5xlval.i = atoi(yytext+1);
                     return DBREF;
                 }
  -?[0-9]+       {
                     t5xlval.i = atoi(yytext);
                     return INTEGER;
                 }
  "***END OF DUMP***" {
                     return EOD;
                 }
 \>              {
                     return '>';
                 }
 \<              {
                     return '<';
                 }
 \(              {
                     return '(';
                 }
 \)              {
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
 \:              {
                     return ':';
                 }
 \/              {
                     return '/';
                 }
}

\"               {
                     pQuotedString = aQuotedString;
                     iPreStrContext = YY_START;
                     BEGIN(str);
                 }
<str>{
  \"             {
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

[\n\t ]+         /* ignore whitespace */ ;
.                { return EOF; }
%%
