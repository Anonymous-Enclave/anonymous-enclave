#ifndef __PATH_STRUCTURE__
#define __PATH_STRUCTURE__
#include "types.h"
#define NDIRECT 12
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

#define DIRSIZ 14
#define T_DIR   1
#define ROOTDEV  1
#define ROOTINO  1
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
#endif