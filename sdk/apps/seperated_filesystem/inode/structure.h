#ifndef __INODE_STRUCTURE__
#define __INODE_STRUCTURE__
#include "types.h"
#define NDIRECT 12
#define BSIZE 4096
// in-disk inode
struct dinode
{
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 2];
};

// in-memory inode
struct inode
{
    uint dev;
    uint inum;
    int ref;
    int flags;
    short type;
    short major;
    short minor;
    short nlink;
    uint index;
    uint size;
    uint addrs[NDIRECT + 2];
};
#define I_BUSY 0x1
#define I_VALID 0x2
#define IPB  (BSIZE / sizeof(struct dinode))
// number of all the disk inode
#define NINODES   200
// number of active in-memory inode
#define NINODE 50
struct buf
{
    int flags;
    uint dev;
    uint blockno;
    struct buf *prev, *next, *qnext; // LRU cache list
    uint index;
    uchar data[BSIZE];
};
#define BPB           (BSIZE*8)
#define MAXOPBLOCKS   10
#define FSSIZE 1000
#define LOGSIZE       (MAXOPBLOCKS * 3)
#define INODESTART    (2 + LOGSIZE)
#define BMAPSTART     (2 + LOGSIZE + NINODES/IPB + 1)
#define BBLOCK(bn)    ((bn)/BPB + BMAPSTART)
#define IBLOCK(i)     ((i)/IPB + INODESTART)
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)
struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#endif