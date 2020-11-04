#ifndef __PATH_CALLARG__
#define __PATH_CALLARG__
#define MAXPATH 128
#define DIRSIZ 14
struct dirlookup_arg
{
    char path[MAXPATH];
    char name[DIRSIZ];
    unsigned int poff;
    struct inode inode;
};

struct dirlink_arg
{
    struct inode de;
    char name[DIRSIZ];
    unsigned int inum;
};

struct namei_arg
{
    char path[MAXPATH];
};

struct nameiparent_arg
{
    char path[MAXPATH];
    char name[DIRSIZ];
};




#endif