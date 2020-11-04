#ifndef __XV6DEFS__
#define __XV6DEFS__
#include "types.h"
struct pipe;
struct spinlock;
struct file;
struct superblock;
struct inode;
struct stat;
// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*,uint64, uint, uint);
void            itrunc(struct inode *);

// ramdisk.c
// void            ramdiskinit(void);
// void            ramdiskintr(void);
// void            ramdiskrw(struct buf*);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op();
void            end_op();

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr();
//ramdisk.c 
void            panic(char*);
void            ramdiskinit(void);
void            ramdiskrw(struct buf*);
void            ramdiskflush();

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// spinlock.c
void initlock(struct spinlock *, char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
void wakeup(void* );
void sleep(void* , struct spinlock*);

// sys_file.c

int sys_open(char*, int);
int sys_close(int);
int sys_read(int, void*, int);
int sys_read(int, void*,int);
int sys_ftruncate(int);
int sys_unlink(const char*);
#endif