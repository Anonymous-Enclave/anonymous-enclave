#ifndef __IOZONE_INTERPOSITION__
#define __IOZONE_INTERPOSITION__
#include<stdio.h>
#define printf(...) myprintf(__VA_ARGS__)
#define setvbuf(a,b,c,d) mysetvbuf(a,b,c,d)
#define gethostname(a,b) mygethostname(a,b)
#define getenv(a) mygetenv(a)
#define getpid(void)  mygetpid(void)
#define perror(a) myperror(a)
int myprintf(const char *__restrict__ __format, ...);
int mysetvbuf(FILE *__restrict__ __stream, char *__restrict__ __buf, int __modes, size_t __n);
int mygethostname(char* name, size_t len);
char* mygetenv(const char* name);
int mygetpid(void);
void myperror(const char* s);
#endif