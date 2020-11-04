#include<string.h>
#include<stdio.h>
int myprintf(const char *__restrict__ __format, ...){  
    return strlen(__format);
}

int mysetvbuf(FILE *__restrict__ __stream, char *__restrict__ __buf, int __modes, size_t __n){
    return 0;
}

int mygethostname(char* name, size_t len){
    strcpy(name,"test");
    return 0;
}

char* mygetenv(const char* name){
    return NULL;
}

int mygetpid(void){
    static int pid = 4;
    return pid ++;
}

void myperror(const char* s){
    return;
}



