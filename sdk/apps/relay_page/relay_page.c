#include "eapp.h"
#include "print.h"
#include <stdlib.h>
#include "util.h"

//page size in the RISCV
#define PAGE_SIZE 4096
//begin of the intermediate relay page for reduce enclave
#define REDUCE_OFFSET 1
#define MAP_NUM 2
#define REDUCE_NUM 2

unsigned int ELFHash(char *str, int num)
{
    unsigned int hash = 0;
    unsigned int x = 0;

    for (int i = 0; i < num; i++)
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

int map(char *f, Keyvalue** keyvalue, int reduce_num)
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
            unsigned int hash = ELFHash(word_begin, ptr_begin - word_begin);
            int n = hash % reduce_num;
            memcpy(keyvalue[n]->key, word_begin, ptr_begin - word_begin);
            // eapp_print("intermediate entry %d key is %s\n", n, keyvalue[n]->key);
            keyvalue[n]->value = 1;
            word_begin = NULL;
            keyvalue[n] = keyvalue[n] + 1;
        }
    }
    for(int i = 0; i < reduce_num; i++)
    {
        memcpy(keyvalue[i]->key, "-end-", sizeof("-end-"));
    }
}

/*
 * reduce_num @ the number of reduce enclave
 * relay_page memory layout:
 * | origin data |map 1-1|map 1-2|map 2-1|map 2-2|
 */
int domap(unsigned long *args, int reduce_num)
{
    eapp_print("begin domap \n");
    char * shm, *map_data;
    Keyvalue ** reduce_keyvalue;
    //divide relay page into reduce_num region, and each region contain several keyvalue pairs 
    reduce_keyvalue = (Keyvalue **)malloc(reduce_num * sizeof(Keyvalue *));
    //get the relay page address
    shm = (int*)args[13];
    unsigned long shm_size = args[14];
    int map_index = args[12] % MAP_NUM;
    map_data = shm ;
    // eapp_print("split addr is %lx split size is %lx split point is %lx\n", shm, shm_size, shm + REDUCE_OFFSET * PAGE_SIZE);
    split_mem_region(shm, shm_size, shm + REDUCE_OFFSET * PAGE_SIZE);
    for (int i=0; i < reduce_num; i++)
    {
        //REDUCE_OFFSET * PAGE_SZE is stored the original data, and map_index * reduce_num is the transfer relay page to the reducer enclave
        reduce_keyvalue[i] = shm + REDUCE_OFFSET * PAGE_SIZE + i * PAGE_SIZE;
        //split the relay page into sub relat page and treansfer them to different reduce enclaves
        // eapp_print("split addr %d is %lx split size is %lx split point is %lx\n", i, shm + REDUCE_OFFSET * PAGE_SIZE, shm_size - REDUCE_OFFSET * PAGE_SIZE - i * PAGE_SIZE, shm + (REDUCE_OFFSET + i + 1 ) * PAGE_SIZE);
        split_mem_region(shm + (REDUCE_OFFSET + i) * PAGE_SIZE, shm_size - REDUCE_OFFSET * PAGE_SIZE - i * PAGE_SIZE, 
                        shm + (REDUCE_OFFSET + i + 1 ) * PAGE_SIZE);
        // eapp_print("reduce_keystone i %d addr %lx\n", i, reduce_keyvalue[i]);
    }
    // Keyvalue *keyvalue = (Keyvalue*)malloc(PAGE_SIZE);
    // memset(keyvalue, 0, PAGE_SIZE);
    // eapp_print("map data: %s \n", map_data);
    map(map_data, reduce_keyvalue, reduce_num);
    char enclave_name[16];
    sprintf(enclave_name, "test%d", map_index);
    struct call_enclave_arg_t call_arg;
    for (int i = 0; i < reduce_num; i++)
    {
        call_arg.req_vaddr = shm + (REDUCE_OFFSET + i) * PAGE_SIZE;
        call_arg.req_size = PAGE_SIZE;
        asyn_enclave_call(enclave_name,  &call_arg);
    }
    // for (int i=0; i < reduce_num; i++)
    // {
    //     //REDUCE_OFFSET * PAGE_SZE is stored the original data, and map_index * reduce_num is the transfer relay page to the reducer enclave
    //     reduce_keyvalue[i] = shm + REDUCE_OFFSET * PAGE_SIZE + i * PAGE_SIZE;
    // }
    // for (int i =0; i < reduce_num; i++)
    // {
    //     eapp_print("print %d\n", i);
    //     while(1)
    //     {
    //         char buf[10];
    //         if (!strcmp(reduce_keyvalue[i]->key, "-end-"))
    //             break;
    //         sprintf(buf, "%s:%d\n",reduce_keyvalue[i]->key,reduce_keyvalue[i]->value);
    //         eapp_print(buf);
    //         reduce_keyvalue[i] = reduce_keyvalue[i] + 1;
    //     }
    // }
    eapp_print("end domap\n");
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
  unsigned long * args;
  EAPP_RESERVE_REG;
  domap(args, REDUCE_NUM);
}