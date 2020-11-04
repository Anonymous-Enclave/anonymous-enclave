#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "fscallargs.h"
#include "eapp.h"

int dosplit(unsigned long *args, int keyvalue_size, int map_num)
{
    FILE* f = fopen("/sub/origin.txt","r");
    FILE** f_list = (FILE** )malloc(map_num * sizeof(FILE*));
    for(int i=0; i < map_num; i++)
    {
        char map_file[15];
        sprintf(map_file, "/sub/map-%d.txt", i);
        f_list[i] = fopen(map_file,"w");
    }
    char buf[512];
    int cnt = 0;
    while(fgets(buf, sizeof(buf), f))
    {
        fputs(buf, f_list[cnt % map_num]);
        cnt = cnt + 1;
    }
    for(int i=0; i<map_num; i++)
    {
        fclose(f_list[i]);
    }
    EAPP_RETURN(0);
}

int EAPP_ENTRY main(){
    unsigned long* args;
    EAPP_RESERVE_REG;
    dosplit(args, 500,5);
    return 0;
}

