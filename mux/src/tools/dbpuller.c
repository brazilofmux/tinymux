/**************************************************************************************************
 * Db puller - used to pull data from a MUX flatfile and dump it into a file in @decompile format *
 *                                                                                                *
 * By: Ashen-Shugar (08/16/2005)                                                                  *
 **************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Standard MUX 2.x definitions */
/* This should be over twice the size of the LBUF.  If it's not, it'll misbehave */
#define MALSIZE 16535
/* This should be SBUF_SIZE + 1. If it's not, it'll coredump*/
#define SBUFSIZE 65

int main(int argc, char **argv) 
{
   FILE *f_muxflat, *f_mymuxfile, *f_muxattrs, *f_muxout, *f_muxlock;
   char *pt1, *spt2, *spt3, *pt2, *pt3, 
        s_attrval[SBUFSIZE], s_attr[SBUFSIZE], s_finattr[SBUFSIZE];
   int i_chk = 0, i_lck = 1, i_atrcntr = 0;
   
   if ( argc != 3 ) {
      fprintf(stderr, "Syntax: %s mux-flatfile dbref# (no preceeding # character)\r\n", argv[0]);
      exit(1);
   }
   if ( (f_muxflat = fopen(argv[1], "r")) == NULL ) {
      fprintf(stderr, "ERROR: Unable to open %s for reading.", argv[1]);
      exit(1);
   }
   pt1 = argv[2];
   while (*pt1) {
      if ( !isdigit(*pt1) ) {
         fprintf(stderr, "ERROR: Dbref# must be an integer (no # preceeding)\r\n");
         fclose(f_muxflat);
         exit(1);
      }
      pt1++;
   }
   if ( (f_mymuxfile = fopen("mymuxfile.dat", "w")) == NULL ) {
      fprintf(stderr, "ERROR: Unable to open output file for attribute header information (mymuxfile.dat)\r\n");
      fclose(f_muxflat);
      exit(1);
   }
   spt2=malloc(MALSIZE);
   memset(spt2, '\0', MALSIZE);
   memset(s_attr, '\0', sizeof(s_attr));
   memset(s_attrval, '\0', sizeof(s_attr));
   while ( !feof(f_muxflat) ) {
      fgets(spt2, (MALSIZE-2), f_muxflat);
      pt2 = spt2;
      if ( i_chk ) {
         i_chk = 0;
         strtok(pt2, ":");
         sprintf(s_attr, "%s", strtok(NULL, ":"));
         s_attr[strlen(s_attr)-2]='\0';
         fprintf(f_mymuxfile, "%s %d \r\n", s_attr, atoi(s_attrval));
      }
      if ( (strlen(pt2) > 3) && (*pt2 == '+') && (*(pt2+1) == 'A') && isdigit(*(pt2+2)) ) {
         i_chk = 1;
         sprintf(s_attrval, "%s", pt2+2);
      }
      if ( *pt2 == '!' )
         break;
   }
   fclose(f_mymuxfile);
   if ( (f_mymuxfile = fopen("mymuxfile.dat", "r")) == NULL ) {
      fclose(f_muxflat);
      fprintf(stderr, "ERROR: Unable to open attribute header information (mymuxfile.dat)\r\n");
      free(spt2);
      exit(1);
   }
   if ( (f_muxattrs = fopen("muxattrs.dat", "r")) == NULL ) {
      fclose(f_muxflat);
      fclose(f_mymuxfile);
      fprintf(stderr, "ERROR: Unable to open attribute header information (muxattrs.dat)\r\n");
      free(spt2);
      exit(1);
   }
   if ( (f_muxout = fopen("muxout.txt", "w")) == NULL ) {
      fclose(f_muxflat);
      fclose(f_mymuxfile);
      fclose(f_muxattrs);
      fprintf(stderr, "ERROR: Unable to open output file (muxout.txt)\r\n");
      free(spt2);
      exit(1);
   }
   if ( (f_muxlock = fopen("muxlocks.dat", "r")) == NULL ) {
      fclose(f_muxflat);
      fclose(f_mymuxfile);
      fclose(f_muxattrs);
      fclose(f_muxout);
      fprintf(stderr, "ERROR: Unable to open mux lock file (muxlocks.dat)\r\n");
      free(spt2);
      exit(1);
   }
   memset(spt2, '\0', MALSIZE);
   spt3=malloc(MALSIZE);
   memset(spt3, '\0', MALSIZE);
   pt3 = spt3;
   fseek(f_muxflat, 0L, SEEK_SET);
   fprintf(stderr, "Step 1: Quering for dbref #%d\n", atoi(argv[2]));
   i_chk = 0;
   while ( !feof(f_muxflat) ) {
      fgets(spt2, (MALSIZE-2), f_muxflat);
      pt2 = spt2;
      if ( (*pt2 == '<') && i_chk ) {
         break;
      }
      if ( *pt2 == '!' && (atoi(pt2+1) == atoi(argv[2])) ) {
         i_chk = 1;
         continue;
      }
      if ( i_chk && *pt2 == '>' && isdigit(*(pt2+1)) ) {
         i_chk = 2;
         i_atrcntr++;
         sprintf(s_attrval, " %d ", atoi(pt2+1));
         memset(spt2, '\0', MALSIZE);
         memset(s_finattr, '\0', sizeof(s_finattr));
         fseek(f_muxattrs, 0L, SEEK_SET);
         while ( !feof(f_muxattrs) ) {
            fgets(spt2, (MALSIZE-2), f_muxattrs);
            if ( strstr(spt2, s_attrval) != NULL ) {
               strcpy(s_finattr, (char *)strtok(spt2, " "));
               break;
            }
         }
         if ( strlen(s_finattr) == 0 ) {
            fseek(f_mymuxfile, 0L, SEEK_SET);
            while ( !feof(f_mymuxfile) ) {
               fgets(spt2, (MALSIZE-2), f_mymuxfile);
               if ( strstr(spt2, s_attrval) != NULL ) {
                  strcpy(s_finattr, (char *)strtok(spt2, " "));
                  break;
               }
            }
         }
         if ( strlen(s_finattr) == 0 ) {
            fprintf(stderr, "ERROR: Unknown error in attribute handler.");
            exit(1);
         }
         fseek(f_muxlock, 0L, SEEK_SET);
         i_lck = 0;
         while ( !feof(f_muxlock) ) {
            fgets(spt2, (MALSIZE-2), f_muxlock);
            if ( strstr(spt2, s_attrval) != NULL ) {
               i_lck = 1;
               break;
            }
         }
         if ( i_lck )
            fprintf(f_muxout, "@lock/%s #%s=", s_finattr, argv[2]);
         else
            fprintf(f_muxout, "&%s #%s=", s_finattr, argv[2]);
      } else if ( i_chk == 2) {
         if ( *pt2 == '"' )
            pt2++;
         if ( *pt2 == '\001' ) {
            while ( *pt2 && *pt2 != ':' ) pt2++;
            pt2++;
            while ( *pt2 && *pt2 != ':' ) pt2++;
            pt2++;
         }
         memset(spt3, '\0', MALSIZE);
         pt3 = spt3;
         while ( *pt2 ) {
            if ( *pt2 == '\\' ) {
               pt2++;
            }
            if ( *pt2 == '\t' ) {
               pt2++;
               *pt3 = '%';
               pt3++;
               *pt3 = 't';
               pt3++;
            }
            *pt3 = *pt2; 
            pt2++;
            pt3++;
         }
         *pt3 = '\0';
         if ( strlen(spt3) > 2) {
            if ( *(pt3-2) == '"' ) {
               *(pt3-2) = '\n';
               *(pt3-1) = '\0';
            } else if ( *(pt3-2) == '\r' ) {
               *(pt3-2) = '%';
               *(pt3-1) = 'r';
               *pt3 = '\0';
            }
         }
         if ( *spt3 == '\r' && (strlen(spt3) <= 2) ) {
             strcpy(spt3, "%r");
         }
         fprintf(f_muxout, "%s", spt3);
      }
   }
   fprintf(stderr, "Step 2: Writing %d attributes\n", i_atrcntr);
   fclose(f_muxlock);
   fclose(f_muxout);
   fclose(f_muxattrs);
   fclose(f_mymuxfile);
   fclose(f_muxflat);
   free(spt2);
   free(spt3);
   fprintf(stderr, "Step 3: Completed (file is: muxout.txt).\n");
   return 0;
}
