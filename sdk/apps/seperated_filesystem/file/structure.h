#ifndef __FILE_STRUCTURE__
#define __FILE_STRUCTURE__
#include "types.h"
#define NDIRECT 12
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
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  //FIXME: how to support pipe? or don't support
 // struct pipe *pipe; // FD_PIPE
  struct inode ie;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
  uint index;
};

//FIXME: what these do?
#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

#define I_BUSY 0x1
#define I_VALID 0x2
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];
#define NDEV 10
#define NFILE 100
struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};
#define MAXOPBLOCKS 10
#define BSIZE 4096
#endif