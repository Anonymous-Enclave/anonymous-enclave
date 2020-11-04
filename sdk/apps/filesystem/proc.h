#ifndef __XV6PROC__
#define __XV6PROC__
struct proc {
  struct inode *cwd;
};

struct proc *curproc;
#endif