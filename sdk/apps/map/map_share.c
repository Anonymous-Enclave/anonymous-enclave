#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "fscallargs.h"
#include "eapp.h"
// ELF Hash Function
#define PAGE_SIZE 4096*20
#define REDUCE_OFFSET 5
#define NUM 4
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

int map(char *f, Keyvalue* keyvalue)
{
    // eapp_print("fgets\n");
    char *ptr_begin;
    char *word_begin;
    word_begin = NULL;
    for (ptr_begin = f; *ptr_begin != 0; ++ptr_begin) {
        // eapp_print("char %c", *ptr_begin);
        if ((word_begin ==NULL) && ( isspace(*ptr_begin) || (*ptr_begin == '\n')))
            continue;
        if (word_begin == NULL)
            word_begin = ptr_begin;
        if ( isspace(*ptr_begin) || (*ptr_begin == '\n'))
        {
            memcpy(keyvalue->key, word_begin, ptr_begin - word_begin);
            keyvalue->value = 1;
            word_begin = NULL;
            keyvalue = keyvalue + 1;
        }
    }
    
    memcpy(keyvalue->key, "-end-", sizeof("-end-"));
}

int domap(unsigned long *args, int reduce_num)
{
    long map_index;
    char * shm, *map_data;
    Keyvalue ** reduce_keyvalue;
    reduce_keyvalue = (Keyvalue **)malloc(reduce_num * sizeof(Keyvalue *));
    map_index = (long)args[12] % NUM;
    eapp_print("map index %d\n", map_index);
    shm = (int*)args[10];
    unsigned long shm_size = args[11];
    map_data = shm + PAGE_SIZE * map_index;
    for (int i=0; i < reduce_num; i++)
    {
        reduce_keyvalue[i] = shm + REDUCE_OFFSET * PAGE_SIZE + (map_index * reduce_num + i) * PAGE_SIZE;
        // eapp_print("reduce_keystone i %d addr %lx\n", i, reduce_keyvalue[i]);
    }
    Keyvalue *keyvalue = (Keyvalue*)malloc(PAGE_SIZE);
    memset(keyvalue, 0, PAGE_SIZE);
    map(map_data, keyvalue);
    while(1)
    {
        char buf[10];
        if (!strcmp(keyvalue->key, "-end-"))
            break;
        unsigned int hash = ELFHash(keyvalue->key);
        int n = hash % reduce_num;
        sprintf(buf, "%s:%d\n",keyvalue->key,keyvalue->value);
        // eapp_print("file %d buf %s\n", n, buf);
        memcpy(reduce_keyvalue[n]->key, keyvalue->key, 10);
        reduce_keyvalue[n]->value = keyvalue->value;
        reduce_keyvalue[n] = (Keyvalue *)reduce_keyvalue[n] + 1;
        keyvalue = (Keyvalue *)keyvalue + 1;
    }
    for(int i =0; i<reduce_num; i++)
    {
        memcpy(reduce_keyvalue[i]->key, "-end-", sizeof("-end-"));
    }
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
    unsigned long* args;
    EAPP_RESERVE_REG;
    domap(args, NUM);
    return 0;
}

