#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "print.h"
#include "string.h"
#include "loggingcallarg.h"
#include "inodecallarg.h"
#include "filecallarg.h"
#define PREMAPSIZE 4096
struct devsw devsw[NDEV];
struct {
//   struct spinlock lock;
  struct file file[NFILE];
} ftable;

int inited = 0;
void *__premapped_buf = 0;
uint64 __logging_handle;
uint64 __inode_handle;
void * get_premapped_buf(){
    if(!__premapped_buf){
        __premapped_buf = eapp_mmap(0,PREMAPSIZE);
    }
    return __premapped_buf;
}

uint64 get_logging_handle(){
    if(!__logging_handle){
        __logging_handle = acquire_enclave("logging");
    }
    return __logging_handle;
}

uint64 get_inode_handle(){
    if(!__inode_handle){
        __inode_handle = acquire_enclave("inode");
    }
    return __inode_handle;
}
void fileinit(){
    for(int i = 0;i<NFILE;i++){
        ftable.file[i].index = i;
    }
}

int filealloc(){
    struct file *f;
    int i = 0;
    for(f = ftable.file; f<ftable.file+NFILE;f++,i++){
        if(f->ref == 0){
            f->ref == 1;
            return i;
        }
    }
    return -1;
}

int fileclose(void *buf){
    int i = ((struct fileclose_arg*)buf)->index;
    struct file *f = &ftable.file[i];
    if(f->ref < 1){
        return -1;
    }
    if(--f->ref >0){
        return 0;
    }
    uint64 ret;
    int status;
    f->ref = 0;
    f->type = FD_NONE;
    if(f->type == FD_INODE){
        status = call_enclave(get_logging_handle(),BEGIN_OP,NULL,0,&ret);
        if(status || ret){
            return -1;
        }
        struct inode* ip = (struct inode*)get_premapped_buf();
        memcpy(ip,&f->ie,sizeof(struct inode));
        status = call_enclave(get_inode_handle(),IPUT,ip,PREMAPSIZE,&ret);
        if(status || ret){
            return -1;
        }
        status = call_enclave(get_logging_handle(),END_OP,NULL,0,&ret);
        if(status || ret){
            return -1;
        }
    }
    else{
        return -1;
    }
    return -1;
}

int filestat(struct file* f,struct stat* st){
    int status;
    uint64 ret;
    if(f->type == FD_INODE){
        struct inode* ip  = (struct inode*)get_premapped_buf();
        memcpy(ip,&f->ie,sizeof(struct inode));
        status = call_enclave(get_inode_handle(),ILOCK,ip,PREMAPSIZE,&ret);
        if(status || ret){
            return -1;
        }
        status = call_enclave(get_inode_handle(),STATI,ip,PREMAPSIZE,&ret);
        if(status || ret){
            call_enclave(get_inode_handle(),IUNLOCK,ip,PREMAPSIZE,&ret);
            return -1;
        }
        memcpy(st,(char*)ip+sizeof(struct inode),sizeof(struct inode));
        status = call_enclave(get_inode_handle(),IUNLOCK,ip,PREMAPSIZE,&ret);
        if(status || ret){
            return -1;
        }
    }
    return 0;
}

int fileread(void* buf,uint64 size){
    struct fileread_arg *fileread_arg = (struct fileread_arg*)buf;
    struct file* f = &ftable.file[fileread_arg->index];
    uint n = fileread_arg->sz;
    int r;
    if(f->ref == 0){
        return -1;
    }
    uint64 ret;
    int status;
    struct inode *ip = (struct inode*)get_premapped_buf();
    if(f->type == FD_INODE){
        memcpy(ip,&f->ie,sizeof(struct inode));
        status = call_enclave(get_inode_handle(),ILOCK,ip,PREMAPSIZE,&ret);
        if(status || ret){
            return -1;
        }
        struct readi_arg *readi_arg = (struct readi_arg*)buf;
        readi_arg->inode = *ip;
        readi_arg->offset = f->off;
        readi_arg->n = n;
        readi_arg->start_offset = RWSTARTOFFSET;
        status = call_enclave(get_inode_handle(),READI,readi_arg,size,&ret);
        if(status || (int)ret < 0){
            call_enclave(get_inode_handle(),IUNLOCK,ip,PREMAPSIZE,&ret);
            return -1;
        }
        f->off += (int)ret;
        status = call_enclave(get_inode_handle(),IUNLOCK,ip,PREMAPSIZE,&ret);
        if(status || ret){
            return -1;
        }
        return (int)ret;
    }
    else{
        return -1;
    }
}

int filewrite(void* buf,uint64 size){
    struct filewrite_arg *filewrite_arg = (struct filewrite_arg*)buf;
    struct file *f = &ftable.file[filewrite_arg->index];
    uint n = filewrite_arg->sz;
    if(f->writable == 0){
        return -1;
    }
    if(f->type == FD_INODE){
        uint max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
        uint i = 0;
        int status;
        uint64 ret;
        struct inode * ip = (struct inode*)get_premapped_buf();
        memcpy(ip,&f->ie,sizeof(struct inode));
        struct writei_arg *writei_arg = (struct writei_arg*)buf;
        writei_arg->inode = *ip;
        writei_arg->offset = f->off;       
        writei_arg->start_offset = RWSTARTOFFSET;
        while(i<n){
            uint n1 = n - i;
            if(n1>max){
                n1 = max;
            }
            status = call_enclave(get_logging_handle(),BEGIN_OP,NULL,0,&ret);
            if(status || ret){
                return i == 0?-1:i;
            }         
            status = call_enclave(get_inode_handle(),ILOCK,ip,PREMAPSIZE,&ret);
            if(status || ret){
                call_enclave(get_logging_handle(),END_OP,NULL,0,&ret);
                return i == 0?-1:i;
            }
            writei_arg->n = n;
            status = call_enclave(get_inode_handle(),WRITEI,writei_arg,size,&ret);
            if(status || (int)ret == -1){
                call_enclave(get_inode_handle(),ILOCK,ip,PREMAPSIZE,&ret);
                call_enclave(get_logging_handle(),END_OP,NULL,0,&ret);
                return i==0?-1:i;
            }
            writei_arg->offset += (int)ret;
            
        }
    }
}


