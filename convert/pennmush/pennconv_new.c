#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

int i_fetchcntr = 0;

#define LBUF 20000
#define FNAMESIZE 255
#define DEFINED_LOCKS 15

void
FGETS(char *x, int y, FILE *z)
{
/*
   memset(x, '\0', y);
*/
   if ( feof(z) ) {
      fprintf(stderr, "Unexpected end of file on line %d\n", i_fetchcntr);
      exit(1);
   }
   fgets(x, y, z);
   i_fetchcntr++;
}

void
LogError(int i_cnt, char *s_match, char *s_instr, char *s_firstarg, char *s_restarg)
{
   fprintf(stderr, "Error: '%s' expected but got '%s' instead on line '%d'.\n",
           s_match, (s_firstarg ? s_firstarg : "(null)"), i_cnt);
   fprintf(stderr, "Strings: %s %s %s\n", s_instr, s_firstarg, s_restarg);
   exit(1);
}


void
SetupArguments(char **s_instr, char **s_firstarg, char **s_restarg)
{
   /* Strip spaces at start */
   while ( **s_instr && isspace(**s_instr) ) (*s_instr)++;

   /* Store first argument */
   *s_firstarg = *s_instr;

   /* Go to next argument */
   while ( **s_instr && !isspace(**s_instr) ) (*s_instr)++;

   /* Store second argument */
   if ( **s_instr ) {
      *s_restarg = (*s_instr)+1;
      **s_instr = '\0';
   } else {
      *s_restarg = NULL;
   }
   return;
}
char *
stripnum(char *s_instr)
{
   if ( *s_instr == '#' )
      return(s_instr+1);
   else
      return(s_instr);
}

char *
StripQuote(char *s_instr, int key)
{
   int i_len;

   i_len = strlen(s_instr);
   if ( i_len <= 2 ) {
      if ( i_len >= 1 ) {
         if ( (*s_instr == '"') && (*(s_instr+1) == '"') ) {
            if ( key )
               *(s_instr+1) = '\r';
            else
               *(s_instr+1) = '\n';
            *(s_instr+2) = '\0';
            return(s_instr+1);
         } else if ( *s_instr == '"' ) {
            if ( key ) 
               *(s_instr) = '\r';
            else
               *(s_instr) = '\n';
            *(s_instr+1) = '\0';
            return(s_instr);
         } else {
            return(s_instr);
         }
      } else {
         return(s_instr);
      }
   }
   if ( (*(s_instr + i_len - 2) == '"') &&
        (*(s_instr + i_len - 3) != '\\') &&
        (*(s_instr + i_len - 3) != '%') ) {
      if ( key ) {
         *(s_instr + i_len - 2) = '\0';
      } else {
         *(s_instr + i_len - 2) = '\n';
         *(s_instr + i_len - 1) = '\0';
      }
   }
   if ( *s_instr == '"' ) {
      return(s_instr+1);
   } else {
      return(s_instr);
   }

}

char *
convert_flagstomux(FILE *fd_filein, FILE *fd_fileout, FILE *fd_err, char *s_instr, int i_type, int i_id)
{
   char *s_strtok, *s_strtokr, **s_listtmp, empty_flags[LBUF+1];
   char *s_flags1_list[] = {"WIZARD", "VISUAL", "VERBOSE", "TRANSPARENT", "TERSE",
                            "STICKY", "SAFE", "QUIET", "PUPPET", "OPAQUE", "NOSPOOF",
                            "MYOPIC", "LINK_OK", "JUMP_OK", "HAVEN", "HALT", "GOING",
                            "ENTER_OK", "DESTROY_OK", "DEBUG", "DARK", "CHOWN_OK", 
                            "AUDIBLE", NULL};
   char *s_flags2_list[] = {"UNFINDABLE", "SUSPECT", "ROYALTY", "NO_TEL", "LIGHT",
                            "GAGGED", "FLOATING", "FIXED", "CONNECTED", "COLOR", 
                            "ANSI", "ABODE", NULL};
   char *s_flags3_list[] = {"NO_COMMAND", NULL};
   char *s_flags4_list[] = {NULL, NULL};
   int i_flags1_list[] = {16, 1048576, 16777216, 8, 2147483648U, 
                          256, 268435456, 2048, 131072, 8388608, 67108864,
                          65536, 32, 128, 1024, 4096, 16384,
                          524288, 512, 8192, 64, 262144,
                          1073741824, 0};
   int i_flags2_list[] = {8, 268435456, 1024, 524288, 32,
                          65536, 4, 524288, 1073741824, 67108864,
                          67108864, 2, 0};
   int i_flags3_list[] = {32768, 0};
   int i_flags4_list[] = {0, 0};
   int i_penntype[] = {1, 2, 4, 8, 16, 32, 65535, 0};
   int i_muxtype[] = {0, 1, 2, 3, 5, 0, 7, 0};
   int i_found;
   int *i_listtmp, *i_listtmp2, i_flags1, i_flags2, i_flags3, i_flags4;

   i_flags1 = i_flags2 = i_flags3 = i_flags4 = i_found = 0;

   memset(empty_flags, '\0', LBUF+1);
   for ( i_listtmp = i_penntype, i_listtmp2 = i_muxtype; i_listtmp && *i_listtmp; i_listtmp++, i_listtmp2++ ) {
      if ( *i_listtmp == i_type ) {
         i_flags1 = *i_listtmp2;
         break;
      }
   }
   s_strtok = strtok_r(s_instr, " \t\r\n", &s_strtokr);
   while ( s_strtok && *s_strtok ) {
      i_found = 0;
      for ( s_listtmp = s_flags1_list, i_listtmp = i_flags1_list; 
            s_listtmp && *s_listtmp; s_listtmp++, i_listtmp++ ) {
         if ( strcmp(*s_listtmp, s_strtok) == 0 ) {
            i_flags1 = i_flags1 | *i_listtmp;
            i_found = 1;
            break;
         }
      }
      if ( !i_found ) {
         for ( s_listtmp = s_flags2_list, i_listtmp = i_flags2_list; 
               s_listtmp && *s_listtmp; s_listtmp++, i_listtmp++ ) {
            if ( strcmp(*s_listtmp, s_strtok) == 0 ) {
               i_flags2 = i_flags2 | *i_listtmp;
               i_found = 1;
               break;
            }
         }
      }
      if ( !i_found ) {
         for ( s_listtmp = s_flags3_list, i_listtmp = i_flags3_list; 
               s_listtmp && *s_listtmp; s_listtmp++, i_listtmp++ ) {
            if ( strcmp(*s_listtmp, s_strtok) == 0 ) {
               i_flags3 = i_flags3 | *i_listtmp;
               i_found = 1;
               break;
            }
         }
      }
      if ( !i_found ) {
         for ( s_listtmp = s_flags4_list, i_listtmp = i_flags4_list; 
               s_listtmp && *s_listtmp; s_listtmp++, i_listtmp++ ) {
            if ( strcmp(*s_listtmp, s_strtok) == 0 ) {
               i_flags4 = i_flags4 | *i_listtmp;
               i_found = 1;
               break;
            }
         }
      }
      if ( !i_found ) {
         strcat(empty_flags, s_strtok);
         strcat(empty_flags, " ");
      }
      s_strtok = strtok_r(NULL, " \t\r\n", &s_strtokr);
   }
   if ( strlen(empty_flags) > 0 ) {
      fprintf(fd_err, "Flags on object #%d non-convertable: %s\n", i_id, empty_flags);
   }
   /* For now, just dump empty flags */
   fprintf(fd_fileout, "%d\n", i_flags1); /* flags 1 */
   fprintf(fd_fileout, "%d\n", i_flags2); /* flags 2 */
   fprintf(fd_fileout, "%d\n", i_flags3); /* Flags 3 */
   return((char *)NULL);
}

void
convert_powerstomux(FILE *fd_filein, FILE *fd_fileout, char *s_instr)
{
   /* For now, just dump empty powers */
   fputs("0\n", fd_fileout); /* Powers 1 */
   fputs("0\n", fd_fileout); /* Powers 2 */
   return;
}

void
process_locks(FILE *fd_filein, FILE *fd_fileout, FILE *fd_err, int i_iterations, char *s_lock_list[DEFINED_LOCKS], 
              char *s_lock_array[DEFINED_LOCKS], int i_id)
{
   char *s_instr, *s_firstarg, *s_restarg, s_tmpbuff[LBUF+1], *s_tmpbufptr, *s_resttmp;
   int i_loop, i, i_foundlock, i_basic, i_slash, i_joiner, i_abortlock, i_not, i_basictype, i_joinercount;

   s_instr = malloc(LBUF+1);
   i_loop = i_foundlock = i_basic = 0;
   memset(s_tmpbuff, '\0', LBUF+1);
   FGETS(s_instr, LBUF, fd_filein);
   while ( i_loop < i_iterations ) {
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      if ( s_firstarg && strcmp(s_firstarg, "type") == 0 ) {
         i_loop++;
         for ( i = 0; i < DEFINED_LOCKS; i++ ) {
            if ( s_lock_array[i] && strcmp((char *)StripQuote(s_restarg,1), (char *)s_lock_array[i]) == 0 ) {
               i_foundlock = 1;
               /* get creator */
               FGETS(s_instr, LBUF, fd_filein);
               /* get flags */
               FGETS(s_instr, LBUF, fd_filein);
               /* get derefs */
               FGETS(s_instr, LBUF, fd_filein);
               /* get key */
               FGETS(s_instr, LBUF, fd_filein);
               SetupArguments(&s_instr, &s_firstarg, &s_restarg);
               /* We need to strip the # from number based locks */
               if ( s_lock_array[i] && strcmp(s_lock_array[i], "Basic" ) == 0 ) {
                  i_joiner = i_slash = i_abortlock = i_not = i_basictype = 0;
                  s_resttmp = s_restarg;
                  s_tmpbufptr = s_tmpbuff;
                  i_joinercount = 0;
                  /* Count joiners */
                  while ( *s_resttmp && s_resttmp) {
                     /* Right now, we can't handle any alpha characters.  Will work later */
                     if ( *s_resttmp == '"' ) { 
                        s_resttmp++; 
                        continue;
                     }
                     if ( *s_resttmp == '#') { 
                        s_resttmp++; 
                        continue; 
                     }
                     if ( (*(s_resttmp) == '=') || (*(s_resttmp) == '@') ||
                          (*(s_resttmp) == '+') || (*(s_resttmp) == '$')) {
                        i_basictype = 1;
                        *s_tmpbufptr = '(';
                        s_tmpbufptr++;
                     }
                     if ( *s_resttmp == '\n' ) {
                        if ( i_not ) {
                           *s_tmpbufptr = ')';
                           s_tmpbufptr++;
                           i_not = 0;
                        }
                        if ( i_basictype ) {
                           *s_tmpbufptr = ')';
                           s_tmpbufptr++;
                        }
                        s_resttmp++;
                        continue;
                     }
                     if ( isalpha(*s_resttmp) ) {
                        fprintf(fd_err, "Unable to convert complex lock on object #%d: %s", i_id, StripQuote(s_restarg,0));
                        i_abortlock = 1;
                        break;
                     }
                     if ( (*s_resttmp == '!') ) {
                        i_not = 1;
                        *s_tmpbufptr = '(';
                        s_tmpbufptr++;
                     }
                     if ( (*s_resttmp == '&') || (*s_resttmp == '|') ) {
                        /* We can't handle complex locks yet */
                        fprintf(fd_err, "Unable to convert complex lock on object #%d: %s", i_id, StripQuote(s_restarg,0));
                        i_abortlock = 1;
                        break;
                        if ( i_not ) {
                           *s_tmpbufptr = ')';
                           s_tmpbufptr++;
                           i_not = 0;
                        }
                        if ( i_basictype ) {
                           *s_tmpbufptr = ')';
                           s_tmpbufptr++;
                           i_joiner++;
                        } 
                        i_basictype = 0;
                        if ( isalpha(*(s_resttmp+1)) ) {
                           /* MUX can't handle these complex locks - Clear it */
                           fprintf(fd_err, "Unable to convert complex lock on object #%d: %s", i_id, StripQuote(s_restarg,0));
                           i_abortlock = 1;
                           break;
                        }
                     }
                     if ( *s_resttmp == '/' ) {
                        if ( i_slash ) {
                           /* Woopse!  Can't handle multi-attribute locks.  Clear it */
                           fprintf(fd_err, "Unable to convert complex lock on object #%d: %s", i_id, StripQuote(s_restarg,0));
                           i_abortlock = 1;
                           break;
                        }
                        i_slash++;
                     }
                     *s_tmpbufptr = *s_resttmp;
                     if ( (*s_resttmp == '&') || (*s_resttmp == '|') ) {
                        if ( i_basictype ) {
                           s_tmpbufptr++;
                           *s_tmpbufptr=')';
                        }
                        i_basictype = 0;
                     }
                     s_resttmp++;
                     s_tmpbufptr++;
                  }
                  *s_tmpbufptr = '\0';
                  if ( !i_abortlock ) {
                     if ( !i_joiner ) {
                        fprintf(fd_fileout, "%s\n", StripQuote(s_tmpbuff,1));
                     } else {
                        fprintf(fd_fileout, "(%s", StripQuote(s_tmpbuff,1));
                        fprintf(fd_fileout, ")\n");
                     }
                     i_basic = 1;
                  }
               } else {
                  s_lock_list[i] = malloc(LBUF);
                  sprintf(s_lock_list[i], "%s", StripQuote(s_restarg,1));
               }
               break;
            }
         }
         if ( !i_foundlock ) {
            /* get creator */
            FGETS(s_instr, LBUF, fd_filein);
            /* get flags */
            FGETS(s_instr, LBUF, fd_filein);
            /* get derefs */
            FGETS(s_instr, LBUF, fd_filein);
            /* get key */
            FGETS(s_instr, LBUF, fd_filein);
         }
         i_foundlock = 0;
      } else {
         LogError(i_fetchcntr, "type (lock)", s_instr, s_firstarg, s_restarg);
      }
      if ( i_loop >= i_iterations )
         break;
      FGETS(s_instr, LBUF, fd_filein);
   }
   if ( !i_basic ) {
      fprintf(fd_fileout, "\n");
   }
}

void
process_attribs(FILE *fd_filein, FILE *fd_fileout, FILE *fd_attrtonum, FILE *fd_attrexception, int i_iterations, int i_id)
{
   static char s_i[LBUF+1];
   static char s_t[LBUF+1];
   static char s_t2[LBUF+1];
   char *s_instr, *s_firstarg, *s_restarg;
   char *s_tmpstr, *s_tmpptr, *s_tmpptr2, *s_tmpptrtok;
   char *s_tmp2str, *s_tmp2ptr, *s_tmp2ptr2, *s_tmp2ptrtok;
   char *s_flagptr, *s_flagptrtok, **s_flagcur;
   char *s_flag_list[]={"no_command", "no_inherit", "no_clone", "wizard", "mortal_dark", 
                        "hidden", "regexp", "locked", "safe", (char)NULL};
   int i_flag_list[]={32, 32768, 2097152, 4, 8, 2, 536870912, 64, 8388608, 0};
   int i_loop, i_owner, i_flags, *i_flagcur, i_contvalue, i_exception, i_attrib;

   memset(s_i, '\0', LBUF+1);
   memset(s_t, '\0', LBUF+1);
   memset(s_t2, '\0', LBUF+1);
   s_instr = s_i;
   s_tmpstr = s_t;
   s_tmp2str = s_t2;

   /* get name */
   FGETS(s_instr, LBUF, fd_filein);
   i_loop = i_contvalue = 0;
   while ( i_loop < i_iterations ) {
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* We're not reading the flatfile right.  Abort!  Abort! */
      if ( s_firstarg && strcmp(s_firstarg, "name") != 0 ) {
         LogError(i_fetchcntr, "name (attribute)", s_instr, s_firstarg, s_restarg);
      }
      /* Name match here */
      rewind(fd_attrexception);
      while ( !feof(fd_attrexception) ) {
         fgets(s_tmpstr, LBUF, fd_attrexception);
         s_tmpptr = strtok_r(s_tmpstr, " \t\n", &s_tmpptrtok);
         if ( s_tmpptr )
            s_tmpptr2 = strtok_r(NULL, " \t\n", &s_tmpptrtok);        
         else
            s_tmpptr2 = NULL;
         if ( s_restarg && s_tmpptr && strcmp(StripQuote(s_restarg,1), s_tmpptr) != 0 )
            continue;
         break;
      }
      if ( feof(fd_attrexception) ) {
         s_tmpptr2 = StripQuote(s_restarg,1);
      }
      rewind(fd_attrtonum);
      while ( !feof(fd_attrtonum) ) {
         fgets(s_tmp2str, LBUF, fd_attrtonum);
         s_tmp2ptr = strtok_r(s_tmp2str, " \t\n", &s_tmp2ptrtok);
         if ( s_tmp2ptr )
            s_tmp2ptr2 = strtok_r(NULL, " \t\n", &s_tmp2ptrtok);
         else
            s_tmp2ptr = NULL;
         if ( s_tmp2ptr && s_tmpptr2 && strcmp(s_tmpptr2, s_tmp2ptr) != 0 )
            continue;
         break;
      }
      i_attrib = 0;
      if ( feof(fd_attrtonum) || !s_tmp2ptr2) {
         fputs(">511\n", fd_fileout);
         i_attrib = 511;
      } else {
         fprintf(fd_fileout, ">%s\n", s_tmp2ptr2);
         i_attrib = atoi(s_tmp2ptr2);
      }
      i_loop++;

      /* get owner */
      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      if ( s_firstarg && strcmp(s_firstarg, "owner") == 0 ) {
         i_owner = atoi(stripnum(s_restarg));
      } else {
         LogError(i_fetchcntr, "owner", s_instr, s_firstarg, s_restarg);
      }

      /* get flags */
      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);

      i_flags = 0;
      if ( s_firstarg && strcmp(s_firstarg, "flags") == 0 ) {
         if ( *(s_restarg+1) != '"' ) {
            s_flagptr = strtok_r(StripQuote(s_restarg,1), " \t\r\n", &s_flagptrtok);
            while ( s_flagptr ) {
               for (s_flagcur = s_flag_list, i_flagcur = i_flag_list; 
                    s_flagcur && *s_flagcur; s_flagcur++, i_flagcur++) {
                  if (strcmp(s_flagptr, *s_flagcur) == 0 ) {
                     i_flags += *i_flagcur;
                     break;
                  }
               }
               s_flagptr = strtok_r(NULL, " \t\r\n", &s_flagptrtok);
            }
         }
      } else {
         LogError(i_fetchcntr, "flags", s_instr, s_firstarg, s_restarg);
      }

      /* get and toss away derefs */
      FGETS(s_instr, LBUF, fd_filein);

      /* get value(s) */
      FGETS(s_instr, LBUF, fd_filein);
      if ( (strlen(s_instr) > 2) && (*(s_instr + strlen(s_instr) -2) != '"') ) {
         i_contvalue = 1;
      }

      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      if ( s_firstarg && strcmp(s_firstarg, "value") != 0 ) {
         LogError(i_fetchcntr, "value", s_instr, s_firstarg, s_restarg);
      }

      if ( !s_restarg || !*s_restarg ) {
         i_exception = 1;
         i_contvalue = 1;
      } else if ( *(s_restarg) == '\n' ) {
         *(s_restarg) = '\0';
         i_exception = 1;
         i_contvalue = 1;
      } else if ( *(s_restarg+1) == '\r' ) {
         i_contvalue = 1;
      } else if ( *(s_restarg+1) == '\n' ) {
         i_contvalue = 1;
         if ( *s_restarg != '\r' )
            i_exception = 1;
         *(s_restarg+1) = '\0';
      } else if ( *(s_restarg+1) && *(s_restarg + strlen(s_restarg) - 2) == '\r' ) {
         i_contvalue = 1;
      } else if ( *(s_restarg+1) && *(s_restarg + strlen(s_restarg) - 2) != '"' ) {
         i_contvalue = 1;
         i_exception = 1;
         *(s_restarg + strlen(s_restarg) - 1) = '\0';
      } else if ( *(s_restarg+1) && (*(s_restarg + strlen(s_restarg) - 2) == '"') ) {
         if ( *(s_restarg+2) && (*(s_restarg + strlen(s_restarg) -3) == '\\') ) {
            i_contvalue = 1;
            i_exception = 1;
            *(s_restarg + strlen(s_restarg) - 1) = '\0';
         } else {
            i_contvalue = 0;
            i_exception = 0;
         }
      } 
      if ( ((i_owner != i_id) || (i_flags > 0)) && (i_attrib > 256) ) {
         if ( *(StripQuote(s_restarg,i_exception)) == '\n')
            fprintf(fd_fileout, "%c%d:%d:", (char)1, i_owner, i_flags, StripQuote(s_restarg,i_exception));
         else
            fprintf(fd_fileout, "%c%d:%d:%s", (char)1, i_owner, i_flags, StripQuote(s_restarg,i_exception));
      } else {
         if ( *(StripQuote(s_restarg,i_exception)) != '\n')
         {
            /* Mark PennMUSH password for later conversion */
            if (5 == i_attrib)
            {
               fprintf(fd_fileout, "$P6H$$%s", StripQuote(s_restarg,i_exception));
            }
            else
            {
               fputs(StripQuote(s_restarg,i_exception), fd_fileout);
            }
         }
         else
            ;/* fprintf(fd_fileout, "\r\n"); */
      }
      if ( i_exception ) {
         fprintf(fd_fileout, "\r\n");
      }
      i_exception = 0;
      while ( i_contvalue && !feof(fd_filein) ) {
         FGETS(s_instr, LBUF, fd_filein);
         if ( !s_instr || !*s_instr ) {
            i_exception = 1;
            i_contvalue = 1;
         } else if ( *s_instr == '\r' ) {
            i_contvalue = 1;
         } else if ( *s_instr == '\n' ) {
            i_contvalue = 1;
            i_exception = 1;
            *s_instr = '\0';
         } else if ( *(s_instr+1) && *(s_instr + strlen(s_instr) - 2) == '\r' ) {
            i_contvalue = 1;
         } else if ( *(s_instr+1) && *(s_instr + strlen(s_instr) - 2) != '"' ) {
            i_contvalue = 1;
            i_exception = 1;
            *(s_instr + strlen(s_instr) - 1) = '\0';
         } else if ( *(s_instr+1) && (*(s_instr + strlen(s_instr) - 2) == '"') ) {
            if ( (*(s_instr + strlen(s_instr) -3) == '\\') ) {
               *(s_instr + strlen(s_instr) - 1) = '\0';
               i_exception = 1;
               i_contvalue = 1;
            } else {
               i_contvalue = 0;
               i_exception = 0;
            }
         } 
         if ( *(s_instr) )
            fputs(StripQuote(s_instr,i_contvalue), fd_fileout);
         if ( i_exception )
            fprintf(fd_fileout, "\r\n");
         i_exception = 0;
      }
      fflush(fd_fileout);
      if ( i_loop < i_iterations ) {
         /* Get name again */
         FGETS(s_instr, LBUF, fd_filein);
      }
   }
}

void
ConvertPenntoMUX(FILE *fd_filein, FILE *fd_fileout, FILE *fd_attrtonum, FILE *fd_attrexception, FILE *fd_err, int i_numobjs)
{
   char *s_i2;
   char *s_instr, *s_firstarg, *s_restarg, *s_toggles, s_created[LBUF+1], s_modified[LBUF+1];
   int i_instructure, i_parent, i_iterations, i_id, i, i_type, i_numobjcntr;
   char *s_lock_list[DEFINED_LOCKS],
        *s_lock_array[DEFINED_LOCKS]={"Basic", "Enter", "Use", "Page", "Teleport", "Speech", "Parent", "Link", "Leave",
                                      "Drop", "Give", "Mail", "Control", "DropTo", NULL};
   int i_lock_array[DEFINED_LOCKS]={511, 59, 62, 61, 85, 199, 98, 93, 60, 86, 63, 157, 96, 216, 0};
   int i_frell[]={1, i_numobjs * 10 / 100, i_numobjs * 20 / 100, i_numobjs * 30 / 100, i_numobjs * 40 / 100,
                  i_numobjs * 50 / 100, i_numobjs * 60 / 100, i_numobjs * 70 / 100, i_numobjs * 80 / 100,
                  i_numobjs * 90 / 100, i_numobjs, -1};
   char *s_frellret[] = {" [ 0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100% ] ", 0};
   char *s_exits, *s_next, *s_lock, *s_owner;
   int i_frellptr, i_frellquarter;



   s_i2 = malloc(LBUF+1);
   s_instr = s_i2;
   for (i = 0; i < DEFINED_LOCKS; i++) {
      s_lock_list[i] = NULL;
   }

   i_instructure = i_parent = i_iterations = i_frellptr = 0;
   i_frellquarter = i_numobjs / 30;
   i_numobjcntr = 1;
   if ( i_frellquarter == 0 )
      i_frellquarter = 1;
   while ( !feof(fd_filein) ) {
      FGETS(s_instr, LBUF, fd_filein);
      if ( !i_instructure && *s_instr != '~' )
         continue;
      if ( !i_instructure && (*s_instr == '~') ) {
         i_instructure=1;
         continue;
      }

      /* Check for end of file */
      if ( strcmp(s_instr, "***END OF DUMP***\n") == 0 ) {
         fprintf(fd_fileout, "<\n%s", s_instr);
         break;
      }
      /* Populate dbref */
      if ( *s_instr == '!' ) {
         i_id = atoi((s_instr+1));
         if ( i_id != 0 ) {
            fputs("<\n", fd_fileout);
         }
         fputs(s_instr, fd_fileout);
         if ( i_numobjs != 0 ) {
            for ( i_frellptr = 0; i_frellptr <= 10; i_frellptr++ ) {
               if ( i_frell[i_frellptr] == i_numobjcntr ) {
                  fprintf(stderr, "%s", s_frellret[i_frellptr]);
               }
            }
            i_numobjcntr++;
            if ( (i_numobjcntr % i_frellquarter) == 0 ) {
               fprintf(stderr, ".", s_frellret[i_frellptr]);
            }
         }
      }
      
      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store name */
      if ( s_firstarg && strcmp("name", s_firstarg) == 0 ) {
         fputs(StripQuote(s_restarg,0), fd_fileout);
      } else {
         LogError(i_fetchcntr, "name (object)", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store location */
      if ( s_firstarg && strcmp("location", s_firstarg) == 0) {
         fputs(stripnum(s_restarg), fd_fileout);
         /* Store empty zone value for MUX */
         fputs("-1\n", fd_fileout);
      } else {
         LogError(i_fetchcntr, "location", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store Contents */
      if ( s_firstarg && strcmp("contents", s_firstarg) == 0) {
         fputs(stripnum(s_restarg), fd_fileout);
      } else {
         LogError(i_fetchcntr, "contents", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store Exits */
      if ( s_firstarg && strcmp("exits", s_firstarg) == 0) {
         fputs(stripnum(s_restarg), fd_fileout);
         /* Populate Link location */
         fputs(stripnum(s_restarg), fd_fileout);
      } else {
         LogError(i_fetchcntr, "exits", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store next */
      if ( s_firstarg && strcmp("next", s_firstarg) == 0) {
         fputs(stripnum(s_restarg), fd_fileout);
      } else {
         LogError(i_fetchcntr, "next", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Record parent value for later */
      if ( s_firstarg && strcmp("parent", s_firstarg) == 0) {
         i_parent = atoi(stripnum(s_restarg));
      } else {
         LogError(i_fetchcntr, "parent", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* If lock do lock parsing */
      if ( s_firstarg && strcmp("lockcount", s_firstarg) == 0) {
         i_iterations = atoi(s_restarg);
         if ( atoi(s_restarg) > 0 )
            process_locks(fd_filein, fd_fileout, fd_err, i_iterations, s_lock_list, s_lock_array, i_id);
         else
            fputs("\n", fd_fileout);
      } else {
         LogError(i_fetchcntr, "lockcount", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store owner */
      if ( s_firstarg && strcmp("owner", s_firstarg) == 0) {
         fputs(stripnum(s_restarg), fd_fileout);
         /* Populate parent here */
         fprintf(fd_fileout, "%d\n", i_parent);
      } else {
         LogError(i_fetchcntr, "owner", s_instr, s_firstarg, s_restarg);
      }
      
      /* Grab zone and toss out */
      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      if ( s_firstarg && strcmp("zone", s_firstarg) == 0) {
         ; /* do nothing */
      } else {
         LogError(i_fetchcntr, "zone", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* grab pennies */
      if ( s_firstarg && strcmp("pennies", s_firstarg) == 0) {
         fputs(s_restarg, fd_fileout);
      } else {
         LogError(i_fetchcntr, "pennies", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Store type for later */
      if ( s_firstarg && strcmp("type", s_firstarg) == 0) {
         i_type = atoi(s_restarg);
      } else {
         LogError(i_fetchcntr, "type (object)", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Convert flags to MUX */
      if ( s_firstarg && strcmp("flags", s_firstarg) == 0) {
         s_toggles = convert_flagstomux(fd_filein, fd_fileout, fd_err, StripQuote(s_restarg,1), i_type, i_id);
      } else {
         LogError(i_fetchcntr, "flags", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Convert powers to MUX */
      if ( s_firstarg && strcmp("powers", s_firstarg) == 0) {
         convert_powerstomux(fd_filein, fd_fileout, StripQuote(s_restarg,1));
      } else {
         LogError(i_fetchcntr, "powers", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      /* Eat warnings */
      if ( s_firstarg && strcmp("warnings", s_firstarg) == 0) {
         ; /* do nothing */
      } else {
         LogError(i_fetchcntr, "warnings", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      memset(s_created, '\0', LBUF);
      if ( s_firstarg && strcmp("created", s_firstarg) == 0) {
         strcpy(s_created, StripQuote(s_restarg,1));
      } else {
         LogError(i_fetchcntr, "created", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      memset(s_modified, '\0', LBUF);
      if ( s_firstarg && strcmp("modified", s_firstarg) == 0) {
         strcpy(s_modified, StripQuote(s_restarg,1));
      } else {
         LogError(i_fetchcntr, "modified", s_instr, s_firstarg, s_restarg);
      }

      FGETS(s_instr, LBUF, fd_filein);
      SetupArguments(&s_instr, &s_firstarg, &s_restarg);
      if ( s_firstarg && strcmp("attrcount", s_firstarg) == 0) {
         for (i = 0; i < DEFINED_LOCKS; i++) {
            if ( s_lock_list[i] && *s_lock_list[i] ) {
               fprintf(fd_fileout, ">%d\n%s\n", i_lock_array[i], s_lock_list[i]);
            }
         }
         if ( *s_created ) {
            fprintf(fd_fileout, ">228\n%s", s_created);
         }
         if ( *s_modified ) {
            fprintf(fd_fileout, ">227\n%s", s_modified);
         }
         i_iterations = atoi(s_restarg);
         if ( i_iterations > 0 ) {
            process_attribs(fd_filein, fd_fileout, fd_attrtonum, fd_attrexception, i_iterations, i_id);
         }
      } else {
         LogError(i_fetchcntr, "attrcount", s_instr, s_firstarg, s_restarg);
      }
   }
}

int
main(int argc, char *argv[])
{
   FILE *fd_attrexception=NULL, 
        *fd_attrtonum=NULL, 
        *fd_filein=NULL, 
        *fd_fileout=NULL,
        *fd_err=NULL;

   char s_filename[FNAMESIZE];
   int i_numobjs = 0;

   memset(s_filename, '\0', FNAMESIZE);

   if ( (argc < 2) || !*argv[1] ) {
      fprintf(stderr, "Syntax: %s <penn-database-flatfile> [<number of objects>]\n", argv[0]);
      exit(1);
   }

   if ( (fd_filein = fopen(argv[1], "r")) == NULL ) {
      fprintf(stderr, "Error opening input file '%s' for reading.\n", argv[1]);
      exit(1);
   }
   sprintf(s_filename, "%s.out", argv[1]);
   if ( (fd_fileout = fopen(s_filename, "w")) == NULL ) {
      fprintf(stderr, "Error opening output file '%s' for writing.\n", s_filename);
      fclose(fd_filein);
      exit(1);
   }
   sprintf(s_filename, "%s.id", argv[1]);
   if ( (fd_attrtonum = fopen(s_filename, "r")) == NULL ) {
      fprintf(stderr, "Error opening attribute to number id table '%s' for reading.\n", s_filename);
      fclose(fd_filein);
      fclose(fd_fileout);
      exit(1);
   }
   sprintf(s_filename, "%s.exception", argv[1]);
   if ( (fd_attrexception = fopen(s_filename, "r")) == NULL ) {
      fprintf(stderr, "Error opening attribute exception table '%s' for reading.\n", s_filename);
      fclose(fd_filein);
      fclose(fd_fileout);
      fclose(fd_attrtonum);
      exit(1);
   }
   sprintf(s_filename, "%s.err", argv[1]);
   if ( (fd_err = fopen(s_filename, "w")) == NULL ) {
      fprintf(stderr, "Error opening error log '%s' for writing.\n", s_filename);
      fclose(fd_filein);
      fclose(fd_fileout);
      fclose(fd_attrtonum);
      fclose(fd_attrexception);
      exit(1);
   }
   if ( (argc > 2) && argv[2] && *argv[2] ) {
      i_numobjs = atoi(argv[2]);
   }
   ConvertPenntoMUX(fd_filein, fd_fileout, fd_attrtonum, fd_attrexception, fd_err, i_numobjs);
/*
   if ( fd_filein != NULL)
      fclose(fd_filein);
   if ( fd_fileout != NULL)
      fclose(fd_fileout);
   if ( fd_attrtonum != NULL)
      fclose(fd_attrtonum);
   if ( fd_attrexception != NULL)
      fclose(fd_attrexception); 
*/
   fcloseall();
   return(0);
}
