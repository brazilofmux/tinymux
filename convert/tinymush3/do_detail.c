#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

void v_fold_obj(FILE *, int);
void v_fold_atr(FILE *, int, int);

void main(int argc, char **argv)
{
    FILE *fp;
    int type;

    type = atoi(argv[1]);
    if ((type == 1) && argv[3] && *argv[3])
    {
        if ((fp = fopen(argv[3], "r")) == NULL)
        {
            return;
        }
    }
    else if ((type == 2) && argv[4] && *argv[4])
    {
        if ((fp = fopen(argv[4], "r")) == NULL)
        {
            return;
        }
    }
    else
    {
        return;
    }

    if (type == 1)
    {
        v_fold_obj(fp, atoi(argv[2]));
        fclose(fp);
        return;
    }

    if (type == 2)
    {
        v_fold_atr(fp, atoi(argv[2]), atoi(argv[3]));
        fclose(fp);
        return;
    }
}

void v_fold_obj(FILE *fp, int line)
{
    char inbuf[10000], obj_buff[100], atr_buff[100];
    int cur_line;

    cur_line = 0;
    memset(inbuf, '\0', sizeof(inbuf));
    memset(obj_buff, '\0', sizeof(obj_buff));
    memset(atr_buff, '\0', sizeof(atr_buff));
    fgets(inbuf, 9999, fp);
    while (!feof(fp))
    {
        cur_line++;
        if (inbuf[0] == '!')
        {
            sprintf(obj_buff, "%.99s", inbuf+1);
        }

        if (line == cur_line)
        {
            if (atr_buff[0] != '\0')
            {
                printf("%d %d\n", atoi(obj_buff), atoi(atr_buff));
            }
            else
            {
                printf("%d -1\n", atoi(obj_buff));
            }
            return;
        }

        if (inbuf[0] == '>')
        {
            sprintf(atr_buff, "%.99s", inbuf+1);
        }
        fgets(inbuf, 9999, fp);
    }
}

void v_fold_atr(FILE *fp, int obj, int atr)
{
    char inbuf[10000], *atrname_ret, atrname[100];
    int found;

    if (atr > 255)
    {
        found = 0;
        fgets(inbuf, 9999, fp);
        memset(inbuf, '\0', sizeof(inbuf));
        memset(atrname, '\0', sizeof(atrname));
        sprintf(atrname, "+A%d", atr);
        while (!feof(fp))
        {
            inbuf[strlen(inbuf)-1]='\0';
            if (found)
            {
                inbuf[strlen(inbuf)-1]='\0';
                atrname_ret = inbuf+3;
                printf("      >Object #%d Attrib: %s\n", obj, atrname_ret);
                return;
            }

            if (strcmp(atrname, inbuf) == 0)
            {
                found = 1;
            }
            fgets(inbuf, 9999, fp);
        }
    }
    printf("      >Object #%d INTERNAL Attrib: #%d (refer to attrs.h for attribute)\n", obj, atr);
}
