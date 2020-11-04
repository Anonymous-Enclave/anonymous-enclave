#ifndef __INODE_CALLARG__
#define __INODE_CALLARG__
struct readi_arg{
    struct inode inode;
    uint offset;
    uint n;
    uint start_offset;
};

struct writei_arg{
    struct inode inode;
    uint offset;
    uint n;
    uint start_offset;
    //dst is just after the writei_arg structure
};

struct iget_arg{
    uint dev;
    uint inum;
};

#define IGET 0
#define READI 1
#define WRITEI 2
#define STATI 3
#define IPUT 4
#define ILOCK 5
#define IUNLOCK 6
#define IUNLOCKPUT 7
#endif