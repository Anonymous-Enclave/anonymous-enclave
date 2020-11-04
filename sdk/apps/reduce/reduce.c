#include "util.h"
#include "fscallargs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "eapp.h"

void split(char *src,const char *separator,char **dest,int *num) 
{
     char *pNext;
     int count = 0;
     if (src == NULL || strlen(src) == 0)
        return;
     if (separator == NULL || strlen(separator) == 0)
        return;    
     pNext = strtok(src,separator);
     while(pNext != NULL) {
          *dest++ = pNext;
          ++count;
         pNext = strtok(NULL,separator);  
    }  
    *num = count;
}

int reduce(FILE* f, Keyvalue *keyvalue)
{
    char buf[128];
    Keyvalue * begin;
    // eapp_print("here \n");
    while(fgets(buf, sizeof(buf), f))
    {
        char *revbuf[2] = {0};
        //分割后子字符串的个数
        int num = 0;
        split(buf,":",revbuf,&num);
        int j = 0;
        while(1)
        {
            if(!strcmp(keyvalue[j].key, "-end-"))
            {
                memcpy(keyvalue[j].key, revbuf[0], strlen(revbuf[0]));
                keyvalue[j].value =  atoi(revbuf[1]);
                memcpy(keyvalue[j+1].key, "-end-", sizeof("-end-"));
                break;
            }
            else
            {
                if(!strcmp(keyvalue[j].key, revbuf[0]))
                {
                    keyvalue[j].value += 1;
                    break;
                }
                else
                {
                    j = j+1;
                }
                
            }            
        }

    }
}

int doreduce(unsigned long * args, int keyvalue_size, int map_num)
{
    long reduce_index, total_size;
    total_size = 0;
    reduce_index = (long)args[12];
    FILE** f_list = (FILE** )malloc(map_num * sizeof(FILE*));
    for(int i=0; i < map_num; i++)
    {
        char rdc_file[15];
        sprintf(rdc_file, "/sub/rdc-%d-%d.txt",i , reduce_index);
        f_list[i] = fopen(rdc_file,"r");
        if(!(f_list[i]))
            eapp_print("error2\n");
        fseek(f_list[i],0L,SEEK_END);
        int size=ftell(f_list[i]);
        fseek(f_list[i],0L,SEEK_SET);
        total_size = total_size + size;
    }
    char merge_file[15];
    sprintf(merge_file, "/sub/merge-%d.txt", reduce_index);
    FILE* f2 = fopen(merge_file,"w");
    if(!(f2))
        eapp_print("error3\n");

    Keyvalue *keyvalue = (Keyvalue*)malloc(total_size * 3 + 15);
    memset(keyvalue, 0, total_size * 3 + 15);
    memcpy(keyvalue->key, "-end-", sizeof("-end-"));
    for(int i = 0; i<map_num; i++)
        reduce(f_list[i], keyvalue);
    while(1)
    {
        char buf[10];
        if (!strcmp(keyvalue->key, "-end-"))
            break;
        sprintf(buf, "%s:%d\n",keyvalue->key,keyvalue->value);
        // eapp_print("buf %s\n",buf);
        fputs(buf, f2);
        keyvalue = keyvalue + 1;
    }
    for(int i = 0; i<map_num; i++)
        fclose(f_list[i]);
    fclose(f2);
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
    unsigned long* args;
    EAPP_RESERVE_REG;
    doreduce(args, 500,5);
    return 0;
}