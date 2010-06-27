%{
#include "omega.h"
#include "p6hgame.h"
#include "p6h.tab.hpp"
%}

%option 8bit 
%option yylineno
%option noyywrap
%option prefix="p6h"

%s afterhdr object
%x str
%%

                 char aQuotedString[10000];
                 char *pQuotedString;
                 int  iPreStrContext;

<INITIAL>{
  ^\+V[0-9]+     {
                     int flags = ((atoi(p6htext+2) - 2) / 256) - 5;
                     g_p6hgame.SetFlags(flags);
                     BEGIN(afterhdr);
                     return VHDR;
                 }
}
<afterhdr>{
  savedtime      {
                     return SAVEDTIME;
                 }
  ^\+FLAGS[\t ]+LIST  {
                     return FLAGSLIST;
                 }
  ^\+POWER[\t ]+LIST  {
                     return POWERLIST;
                 }
  flagcount      {
                     return FLAGCOUNT;
                 }
  flagaliascount {
                     return FLAGALIASCOUNT;
                 }
  name           {
                     return NAME;
                 }
  letter         {
                     return LETTER;
                 }
  type           {
                     return TYPE;
                 }
  perms          {
                     return PERMS;
                 }
  negate_perms   {
                     return NEGATE_PERMS;
                 }
  alias          {
                     return ALIAS;
                 }
  -?[0-9]+       {
                     p6hlval.i = atoi(yytext);
                     return INTEGER;
                 }
  ~[0-9]+        {
                     g_p6hgame.SetObjectCount(atoi(p6htext+1));
                     return OBJECTCOUNT;
                 }
  ![0-9]+        {
                     p6hlval.i = atoi(p6htext+1);
                     BEGIN(object);
                     return OBJECT;
                 }
}

<object>{
  ![0-9]+        {
                     p6hlval.i = atoi(p6htext+1);
                     return OBJECT;
                 }
  name           {
                     return NAME;
                 }
  \#-?[0-9]+     {
                     p6hlval.i = atoi(yytext+1);
                     return DBREF;
                 }
  -?[0-9]+       {
                     p6hlval.i = atoi(yytext);
                     return INTEGER;
                 }
  location       {
                     return LOCATION;
                 }
  contents       {
                     return CONTENTS;
                 }
  exits          {
                     return EXITS;
                 }
  next           {
                     return NEXT;
                 }
  parent         {
                     return PARENT;
                 }
  lockcount      {
                     return LOCKCOUNT;
                 }
  owner          {
                     return OWNER;
                 }
  zone           {
                     return ZONE;
                 }
  pennies        {
                     return PENNIES;
                 }
  type           {
                     return TYPE;
                 }
  flags          {
                     return FLAGS;
                 }
  powers         {
                     return POWERS;
                 }
  warnings       {
                     return WARNINGS;
                 }
  created        {
                     return CREATED;
                 }
  modified       {
                     return MODIFIED;
                 }
  attrcount      {
                     return ATTRCOUNT;
                 }
  derefs         {
                     return DEREFS;
                 }
  value          {
                     return VALUE;
                 }
  creator        {
                     return CREATOR;
                 }
  key            {
                     return KEY;
                 }
  "***END OF DUMP***" {
                     return EOD;
                 }
 \][A-Z0-9!"#$&'*+,-./;<=>?@_~`]+   {
                     p6hlval.p = StringClone(p6htext+1);
                     return ATTRNAME;
                 }
 \^              {
                     return '^';
                 }
 \<              {
                     return '<';
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
                     p6hlval.p = StringClone(aQuotedString);
                     BEGIN(iPreStrContext);
                     return STRING;
                 }
  \\[\\\"]       {
                     if (pQuotedString < aQuotedString + sizeof(aQuotedString) - 1)
                     {
                         *pQuotedString++ = p6htext[1];
                     }
                 }
  [^\\\"]+       {
                     char *p = p6htext;
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
