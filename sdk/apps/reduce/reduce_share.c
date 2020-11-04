#include "util.h"
#include "fscallargs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "eapp.h"

#define PAGE_SIZE 4096*20
#define MERGE_OFFSET (5 + 5*5) 
#define REDUCE_OFFSET 5
#define NUM 4

int reduce(Keyvalue *reduce_keyvalue, Keyvalue *keyvalue)
{
    char buf[128];
    Keyvalue * begin;
    // eapp_print("here \n");
    while(1)
    {
        if (!strcmp(reduce_keyvalue->key, "-end-"))
            break;
        int j = 0;
        while(1)
        {
            if(!strcmp(keyvalue[j].key, "-end-"))
            {
                memcpy(keyvalue[j].key, reduce_keyvalue->key, 10);
                keyvalue[j].value =  reduce_keyvalue->value;
                memcpy(keyvalue[j+1].key, "-end-", sizeof("-end-"));
                break;
            }
            else
            {
                if(!strcmp(keyvalue[j].key, reduce_keyvalue->key))
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
        reduce_keyvalue = (Keyvalue *)reduce_keyvalue + 1;
    }
}

int doreduce(unsigned long * args, int keyvalue_size, int map_num)
{
    long reduce_index;
    char * shm;
    Keyvalue *merge_data;
    Keyvalue ** reduce_keyvalue;
    reduce_keyvalue = (Keyvalue **)malloc(map_num * sizeof(Keyvalue *));
    reduce_index = (long)args[12] % NUM;
    shm = (int*)args[10];
    unsigned long shm_size = args[11];
    merge_data = shm + PAGE_SIZE * (reduce_index + MERGE_OFFSET);
    for (int i=0; i < map_num; i++)
    {
        reduce_keyvalue[i] = shm + REDUCE_OFFSET * PAGE_SIZE + (i * map_num + reduce_index) * PAGE_SIZE;
        // eapp_print("reduce_keystone i %d addr %lx\n", i, reduce_keyvalue[i]);
    }
    // eapp_print("reduce_index %d shm %lx\n", reduce_index, shm);
    // Keyvalue *keyvalue = (Keyvalue*)malloc(PAGE_SIZE);
    // memset(keyvalue, 0, PAGE_SIZE);
    // memcpy(keyvalue->key, "-end-", sizeof("-end-"));
    Keyvalue *keyvalue = merge_data;
    memcpy(keyvalue->key, "-end-", sizeof("-end-"));
    for(int i = 0; i<map_num; i++)
        reduce(reduce_keyvalue[i], keyvalue);
    // while(1)
    // {
    //     char buf[10];
    //     if (!strcmp(keyvalue->key, "-end-"))
    //         break;
    //     sprintf(buf, "%s:%d\n",keyvalue->key,keyvalue->value);
    //     // eapp_print("buf %s\n",buf);
    //     memcpy(merge_data->key, keyvalue->key, 10);
    //     merge_data->value = keyvalue->value;
    //     merge_data = (Keyvalue *)merge_data + 1;
    //     keyvalue = (Keyvalue *)keyvalue + 1;
    // }
    // memcpy(merge_data->key, "-end-", sizeof("-end-"));
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
    unsigned long* args;
    EAPP_RESERVE_REG;
    doreduce(args, 500,NUM);
    return 0;
}