#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "fscallargs.h"
#include "eapp.h"
// ELF Hash Function
unsigned int ELFHash(char *str)
{
    unsigned int hash = 0;
    unsigned int x = 0;

    while (*str)
    {
        hash = (hash << 4) + (*str++);
        if ((x = hash & 0xF0000000L) != 0)
        {

            hash ^= (x >> 24);
            hash &= ~x;
        }
    }
    return (hash & 0x7FFFFFFF);
}

int map(FILE *f, Keyvalue* keyvalue)
{
    char buf[512];
    while(fgets(buf, sizeof(buf), f))
    {
        // eapp_print("fgets\n");
        char *ptr_begin;
        char *word_begin;
        word_begin = NULL;
        for (ptr_begin = buf; *ptr_begin != 0; ++ptr_begin) {
            // eapp_print("char %c", *ptr_begin);
            if ((word_begin ==NULL) && ( isspace(*ptr_begin) ))
                continue;
            if (word_begin == NULL)
                word_begin = ptr_begin;
            if ( isspace(*ptr_begin) )
            {
                memcpy(keyvalue->key, word_begin, ptr_begin - word_begin);
                keyvalue->value = 1;
                word_begin = NULL;
                keyvalue = keyvalue + 1;
            }
        }
    }
    memcpy(keyvalue->key, "-end-", sizeof("-end-"));
}

int domap(unsigned long *args, int keyvalue_size, int reduce_num)
{
    long map_index;
    map_index = (long)args[12];
    char map_file[15];
    sprintf(map_file, "/sub/map-%d.txt", map_index);
    FILE* f = fopen(map_file,"r");
    if(!(f))
        eapp_print("error0\n");
    fseek(f,0L,SEEK_END);
    int size=ftell(f);
    fseek(f,0L,SEEK_SET);
    // eapp_print("file size is %d\n", size);
    FILE** f_list = (FILE** )malloc(reduce_num * sizeof(FILE*));
    for(int i=0; i < reduce_num; i++)
    {
        char rdc_file[15];
        sprintf(rdc_file, "/sub/rdc-%d-%d.txt",map_index, i);
        f_list[i] = fopen(rdc_file,"w");
        if(!(f_list[i]))
            eapp_print("error1\n");
    }
    Keyvalue *keyvalue = (Keyvalue*)malloc(size * 3 + 15);
    memset(keyvalue, 0, size * 3 + 15);
    map(f, keyvalue);
    while(1)
    {
        char buf[10];
        if (!strcmp(keyvalue->key, "-end-"))
            break;
        unsigned int hash = ELFHash(keyvalue->key);
        int n = hash % reduce_num;
        sprintf(buf, "%s:%d\n",keyvalue->key,keyvalue->value);
        // eapp_print("file %d buf %s", n, buf);
        fputs(buf, f_list[n]);
        keyvalue = keyvalue + 1;
    }
    fclose(f);
    for(int i =0; i<reduce_num; i++)
    {
        fclose(f_list[i]);
    }
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
    unsigned long* args;
    EAPP_RESERVE_REG;
    domap(args, 500,5);
    return 0;
}

