/* Original Code provided by Zenty (c) 1999, All rights reserved
* Modified by Ashen-Shugar for attribute allignment and functionary locks
* (c) 1999, All rights reserved
*/
#include <stdio.h>
#include <string.h>
#define MUX_MASK1	0xFFFFFFFB
#define MUX_MASK2	0xF707DFFF
#define MUX_MASK3	0x00000000

int main(void) {
    int val, flag1, flag2, flag3, nflag1, nflag2, nflag3, obj, ismux2;
    int mage, royalty, staff, ansi, immortal, offsetchk, badobj, cntr;
    char f[16384], *q, *f1, *f2, attrname[65];
    memset(f,'\0', sizeof(f));
    q = f;
    f1 = f+1;
    f2 = f+2;
    offsetchk = 0;
    badobj = 0;
    ismux2 = 0;

    gets(q);
    cntr = 1;
    obj = -1;
    while(q != NULL && !feof(stdin) ) {
        if(f[0] == '!') {
            obj=atoi(f1);
            /* object conversion */
            printf("%s\n",q);
            gets(q); printf("%s\n",q); /* name */
            gets(q); printf("%s\n",q); /* location */
            gets(q); printf("%s\n",q); /* zone */
            gets(q); printf("%s\n",q); /* Contents */
            gets(q); printf("%s\n",q); /* Exits */
            gets(q); printf("%s\n",q); /* Link */
            gets(q); printf("%s\n",q); /* Next */
            gets(q);
            if ( strchr(f, '/') == 0 && strchr(f, ':') == 0 ) {
                printf("%s\n", q); /* Bool */
            } else {
                printf("%s\n", q); /* Functionary Lock */
                gets(q);printf("%s\n",q);     /* Bool */
                offsetchk = 1;
            }
            /* If previous was null, last was owner, not bool */
            if ( (!( q && *q) && offsetchk) || !offsetchk ) {
                gets(q); printf("%s\n",q); /* Owner */
            }
            offsetchk = 0;
            gets(q); printf("%s\n",q); /* Parent */
            gets(q); printf("%s\n",q); /* Pennies */
            /* flag conv */
            gets(q); flag1 = atoi(q);
            if ( flag1 & 0x00000004 ) {
                fprintf(stderr, "WARNING: Unrecognized Object Type for #%d\r\n", obj);
            }
            gets(q); flag2 = atoi(q);
            gets(q); flag3 = atoi(q);
            nflag1 = (flag1 & MUX_MASK1);
            nflag2 = (flag2 & MUX_MASK2);
            nflag3 = (flag3 == 0 ? 0 : (flag3 & MUX_MASK3));
            printf("%i\n",nflag1); /* Flags (First word) */
            printf("%i\n",nflag2); /* Flags (Second word) */
            printf("%i\n",nflag3);
            gets(q); printf("%s\n",q); /* Powers (First word) */
            gets(q); printf("%s\n",q); /* Powers (Second Word) */
            fflush(stdout);
            fflush(stdout);
            gets(q); /* Swallow the modified stamp  - if they exist */
            if ( !((q[0] == '<') || (q[0] == '>')) ) {
                gets(q); /* Swallow the create stamp */
                gets(q); /* Get next line */
            }
        } else
            if(f[0] == '+') {
                if((f[1] == 'A') || (f[1] == 'N')) {
                    val = atoi(f2);
                    if ( (cntr == 3) )
                        printf("-R1\n");
                    printf("+%c%d\n",f[1],val);
                    cntr++;
                    fflush(stdout);
                    gets(q);
                } else if(f[1] == 'X') {
                    printf("%s\n", q);
                    cntr++;
                    fflush(stdout);
                    gets(q);
                } else if(f[1] == 'T') {
                    printf("+X992001\n");
                    cntr++;
                    fflush(stdout);
                    gets(q);
                } else {
                    printf("%s\n",q);
                    fflush(stdout);
                    gets(q);
                }
            } else
                if(f[0] == '>') {
                    val = atoi(f1);
                    /* attr conversion */
                    if((val == 99) || ((val > 212) && (val < 218))) {
                        switch(val) {
                                       case 99: strcpy(attrname, "ControlLock");
                                           break;
                                       case 213:strcpy(attrname, "NewObjs");
                                           badobj++;
                                           break;
                                       case 214: /* Conformat */
                                           break;
                                       case 215: /* Exit Format */
                                           break;
                                       case 216:strcpy(attrname, "ExitTo");
                                           break;
                                       case 217:strcpy(attrname, "ChownLock");
                                           break;
                        }
                        if ( val == 214 || val == 215 ) {
                            if ( val == 214 ) {
                                printf(">242\n", val);
                            } else {
                                printf(">241\n", val);
                            }
                            fflush(stdout);
                            gets(q);
                            printf("%s\n",q);
                            gets(q);
                        } else {
                            gets(q);
                            if ( val != 213 ) {
                                fprintf(stderr, "Can not convert attribute '%s' (attrnum %d) for object #%d.\r\nAttribute will be skipped.\r\n", attrname, val, obj);
                                fprintf(stderr, "--> Content of attribute: %s\r\n", q);
                            }
                            gets(q);
                        }
                    } else  {
                        printf(">%d\n",val);
                        fflush(stdout);
                        gets(q);
                        printf("%s\n",q);
                        gets(q);
                    }
                } else
                    if(f[0] == '-') {
                        printf("%s\n",q);
                        cntr++;
                        gets(q);
                    } else {
                        printf("%s\n",q);
                        fflush(stdout);
                        gets(q);
                    }

                    if(strstr(f, "***END OF DUMP***") != NULL )
                    {
                        printf("***END OF DUMP***\n");
                        fflush(stdout);
                        q = NULL;
                        if ( badobj > 0 ) {
                            fprintf(stderr, "WARNING: Can not convert INTERNAL attribute 'NewObjs'.\r\n");
                            fprintf(stderr, "--> %d total instances ignored.  Not required for MUX2.\r\n", badobj);
                        }
                        return(0);
                    }
    } /* while */
    return(0);	
}
