/* Coded by Ashen-Shugar (c) 1999, all rights reserved
 * This program strips quotes that TINY uses for attribute definition,
 * combines attributes that TINY tends to break up, and strips ^M's
 * that TINY tends to add as well.  How lame.
 */
#include <stdio.h>
#include <stdlib.h>

main(int argc, char **argv)
{
   FILE *fp_in  = NULL,
        *fp_out = NULL;
   char ch_in   = '\0';
   int hard_return = 0,
       is_chklock = 0,
       is_endline = 0,
       is_begobj = 0;

   if ( argc < 3 ) {
      printf("syntax: %s <filein> <fileout>\n", argv[0]);
      exit(1);
   }
   if ( (fp_in = fopen(argv[1], "r")) == NULL ) {
      printf("Error opening file %s for reading.\n", argv[1]);
      exit(1);
   }
   if ( (fp_out = fopen(argv[2], "w")) == NULL ) {
      printf("Error opening file %s for writing.\n", argv[2]);
      fclose(fp_in);
      exit(1);
   }
   is_endline=0;
   is_chklock=0;
   while ( !feof(fp_in) ) {
      ch_in = getc(fp_in);
      if ( ch_in == '\n' ) {
         is_endline=1;
      }
      if ( ch_in == '!' && is_endline ) {
         is_begobj=1;
      }
      if ( is_endline && (ch_in == '>' || ch_in == '<') && is_begobj ) {
         is_begobj=0;
         is_chklock=0;
      }
      if ( ch_in != '\n' )
         is_endline=0;
      if ( ch_in == '(' && is_begobj )
         is_chklock++;
      if ( ch_in == ')' && is_begobj )
         is_chklock--;
      if ( ch_in == '\013' ) {
	 hard_return = 1;
         putc(ch_in, fp_out);
         continue;
      }
      if ( (ch_in == '\r' || ch_in == '\n') && (hard_return || (is_chklock > 0)) ) {
         hard_return = 0;
         continue;
      }
      else if ( ch_in == '\0' )
         hard_return = 0;
      if ( !feof(fp_in) )
         putc(ch_in, fp_out);
   }
   if ( fp_in )
      fclose(fp_in);
   if ( fp_out )
      fclose(fp_out);
   return(0);
}
