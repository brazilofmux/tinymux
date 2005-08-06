/* Db puller - used to pull data from a MUX flatfile and dump it into a file in @decompile format */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
int main(int argc, char **argv) 
{
   FILE *f1, *f2, *f3, *f4, *f5;
   char *pt1, *spt2, *spt3, *pt2, *pt3, s_attrval[65], s_attr[65], s_finattr[65] ;
   int i_chk = 0, i_lck = 1, i_atrcntr = 0;
   
   spt2=malloc(16535);
   spt3=malloc(16535);
   if ( argc != 3 ) {
      fprintf(stderr, "Syntax: %s mux-flatfile dbref# (without the #)\r\n", argv[0]);
      free(spt2);
      free(spt3);
      exit(1);
   }
   if ( (f1 = fopen(argv[1], "r")) == NULL ) {
      fprintf(stderr, "ERROR: Unable to open %s for reading.", argv[1]);
      exit(1);
   }
   pt1 = argv[2];
   while (*pt1) {
      if ( !isdigit(*pt1) ) {
         fprintf(stderr, "ERROR: Dbref# must be an integer (no # preceeding)\r\n");
         fclose(f1);
         free(spt2);
         free(spt3);
         exit(1);
      }
      pt1++;
   }
   if ( (f2 = fopen("mymuxfile.dat", "w")) == NULL ) {
      fprintf(stderr, "ERROR: Unable to open output file for attribute header information (mymuxfile.dat)\r\n");
      fclose(f1);
      free(spt2);
      free(spt3);
      exit(1);
   }
   memset(spt2, '\0', 16535);
   memset(s_attr, '\0', sizeof(s_attr));
   memset(s_attrval, '\0', sizeof(s_attr));
   while ( !feof(f1) ) {
      fgets(spt2, 16533, f1);
      pt2 = spt2;
      if ( i_chk ) {
         i_chk = 0;
         strtok(pt2, ":");
         sprintf(s_attr, "%s", strtok(NULL, ":"));
         s_attr[strlen(s_attr)-2]='\0';
         fprintf(f2, "%s %d \r\n", s_attr, atoi(s_attrval));
      }
      if ( (strlen(pt2) > 3) && (*pt2 == '+') && (*(pt2+1) == 'A') && isdigit(*(pt2+2)) ) {
         i_chk = 1;
         sprintf(s_attrval, "%s", pt2+2);
      }
      if ( *pt2 == '!' )
         break;
   }
   fclose(f2);
   if ( (f2 = fopen("mymuxfile.dat", "r")) == NULL ) {
      fclose(f1);
      fprintf(stderr, "ERROR: Unable to open attribute header information (mymuxfile.dat)\r\n");
      free(spt2);
      free(spt3);
      exit(1);
   }
   if ( (f3 = fopen("muxattrs.dat", "r")) == NULL ) {
      fclose(f1);
      fclose(f2);
      fprintf(stderr, "ERROR: Unable to open attribute header information (muxattrs.dat)\r\n");
      free(spt2);
      free(spt3);
      exit(1);
   }
   if ( (f4 = fopen("muxout.txt", "w")) == NULL ) {
      fclose(f1);
      fclose(f2);
      fclose(f3);
      fprintf(stderr, "ERROR: Unable to open output file (muxout.txt)\r\n");
      free(spt2);
      free(spt3);
      exit(1);
   }
   if ( (f5 = fopen("muxlocks.dat", "r")) == NULL ) {
      fclose(f1);
      fclose(f2);
      fclose(f3);
      fclose(f4);
      fprintf(stderr, "ERROR: Unable to open mux lock file (muxlocks.dat)\r\n");
      free(spt2);
      free(spt3);
      exit(1);
   }
   memset(spt2, '\0', 16535);
   i_chk = 0;
   memset(spt3, '\0', 16535);
   pt3 = spt3;
   fseek(f1, 0L, SEEK_SET);
   fprintf(stderr, "Step 1: Quering for dbref #%d\n", atoi(argv[2]));
   while ( !feof(f1) ) {
      fgets(spt2, 16533, f1);
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
         memset(spt2, '\0', 16535);
         memset(s_finattr, '\0', sizeof(s_finattr));
         fseek(f3, 0L, SEEK_SET);
         while ( !feof(f3) ) {
            fgets(spt2, 16533, f3);
            if ( strstr(spt2, s_attrval) != NULL ) {
               strcpy(s_finattr, (char *)strtok(spt2, " "));
               break;
            }
         }
         if ( strlen(s_finattr) == 0 ) {
            fseek(f2, 0L, SEEK_SET);
            while ( !feof(f2) ) {
               fgets(spt2, 16533, f2);
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
         fseek(f5, 0L, SEEK_SET);
         i_lck = 0;
         while ( !feof(f5) ) {
            fgets(spt2, 16533, f5);
            if ( strstr(spt2, s_attrval) != NULL ) {
               i_lck = 1;
               break;
            }
         }
         if ( i_lck )
            fprintf(f4, "@lock/%s #%s=", s_finattr, argv[2]);
         else
            fprintf(f4, "&%s #%s=", s_finattr, argv[2]);
      } else if ( i_chk == 2) {
         if ( *pt2 == '"' )
            pt2++;
         if ( *pt2 == '\001' ) {
            while ( *pt2 && *pt2 != ':' ) pt2++;
            pt2++;
            while ( *pt2 && *pt2 != ':' ) pt2++;
            pt2++;
         }
         memset(spt3, '\0', 16535);
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
         fprintf(f4, "%s", spt3);
      }
   }
   fprintf(stderr, "Step 2: Writing %d attributes\n", i_atrcntr);
   fclose(f5);
   fclose(f4);
   fclose(f3);
   fclose(f2);
   fclose(f1);
   free(spt2);
   free(spt3);
   fprintf(stderr, "Step 3: Completed (file is: muxout.txt).\n");
}
