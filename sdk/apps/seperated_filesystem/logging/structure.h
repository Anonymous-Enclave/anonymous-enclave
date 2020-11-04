#ifndef __LOG_STRUCTURE__
#define __LOG_STRUCTURE__
#include "types.h"
#define MAXOPBLOCKS 10
#define LOGSIZE (MAXOPBLOCKS*3)
#define BSIZE 4096
#define LOGSTART 2
struct buf
{
    int flags;
    uint dev;
    uint blockno;
    struct buf *prev, *next, *qnext; // LRU cache list
    uint index;
    uchar data[BSIZE];
};
#define B_BUSY  0x1  // buffer is locked by some process
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk
#endif