#include "eapp.h"
#include "print.h"
#include <stdlib.h>
#include "util.h"

#define PAGE_SIZE 4096
#define MAP_NUM 2

int reduce(Keyvalue *reduce_keyvalue, Keyvalue *keyvalue)
{
    char buf[128];
    Keyvalue * begin;
    // eapp_print("here \n");
    while(1)
    {
        //end of the relay page entry
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

int doreduce(unsigned long * args, int map_num)
{
    long reduce_index;
    char * shm;
    Keyvalue *merge_data;
    Keyvalue ** reduce_keyvalue;
    reduce_keyvalue = (Keyvalue **)malloc(map_num * sizeof(Keyvalue *));
    shm = (int*)args[13];
    unsigned long shm_size = args[14];
    merge_data = (Keyvalue *)args[10];
    for (int i=0; i < map_num; i++)
    {
        reduce_keyvalue[i] = shm + i * PAGE_SIZE;
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
    while(1)
    {
        char buf[10];
        if (!strcmp(merge_data->key, "-end-"))
            break;
        sprintf(buf, "%s:%d\n",merge_data->key,merge_data->value);
        eapp_print(buf);
        merge_data = merge_data + 1;
    }
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
  unsigned long * args;
  EAPP_RESERVE_REG;
  doreduce(args, MAP_NUM);
}