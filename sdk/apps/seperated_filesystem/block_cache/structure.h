#ifndef __BIO_STRUCTURE__
#define __BIO_STRUCTURE__
#include "types.h"
#define BSIZE 4096
#define MAXOPBLOCKS 10
//FIXME: sychronization method should be redesigned depends on the IPC scheme
struct buf
{
    int flags;
    uint dev;
    uint blockno;
    struct buf *prev, *next, *qnext; // LRU cache list
    uint index;
    uchar data[BSIZE];
};
#define NBUF (MAXOPBLOCKS*3)
#define B_BUSY  0x1  // buffer is locked by some process
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk
#endif
