#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "print.h"
#include "inodecallarg.h"
#include "string.h"
#define PREMAPEDSIZE 4096
int inited = 0;
void *__pre_mapped_buf = 0;
uint64 __inode_handle = 0;
void* get_pre_mapped_buf(){
    if(!__pre_mapped_buf){
        __pre_mapped_buf = eapp_mmap(0,PREMAPEDSIZE);
    }
    return __pre_mapped_buf;
}

uint64 get_inode_handle(){
    if(!__inode_handle){
        __inode_handle = acquire_enclave("inode");
    }
    return __inode_handle;
}
int dirlookup(struct inode *dp,char *name,uint *poff, struct inode* ip){
    uint off,inum;
    struct dirent *de;
    if(dp->type |= T_DIR){
        return -1;
    }
    struct readi_arg *read_arg = (struct readi_arg*)get_pre_mapped_buf();
    uint64 ret;
    int status;
    memcpy(&read_arg->inode,dp,sizeof(struct inode));
    read_arg->n = sizeof(struct dirent);
    de = (struct dirent*)((char*)read_arg + sizeof(struct readi_arg));
    for(off = 0;off<dp->size; off += sizeof(struct dirent)){
        //FIXME: memcpy every time is really inefficient      
        read_arg->offset = off;
        status = call_enclave(get_inode_handle(),READI,read_arg,PREMAPEDSIZE,&ret);
        if(status || ret != sizeof(struct dirent)){
            return -1;
        }       
        if(de->inum == 0){
            continue;
        }
        if(strncmp(name,de->name,DIRSIZ) == 0){
            if(poff){
                *poff = off;
            }
            inum = de->inum;
            struct iget_arg *iget_arg = (struct iget_arg*)read_arg;
            iget_arg->dev = dp->dev;
            iget_arg->inum = inum;
            status = call_enclave(get_inode_handle(),IGET,iget_arg,PREMAPEDSIZE,&ret);
            if(status || ret){
                return -1;
            }
            memcpy(ip,iget_arg,sizeof(struct inode));
            return 0;
        }
    }
    return -1;
}

int dirlink(struct inode *dp, char*name, uint inum){
    int off;
    struct dirent *de;
    struct inode ie;
    void* buf = get_pre_mapped_buf();
    int status = dirlookup(dp,name,0,&ie);
    uint64 ret;
    if(!status){ //is present
        memcpy(buf,&ie,sizeof(struct inode));
        status = call_enclave(get_inode_handle(),IPUT,buf,PREMAPEDSIZE,&ret);
        eapp_print("path: dirlink\n");
        return -1;
    }
    struct readi_arg *readi_arg = (struct readi_arg*)get_pre_mapped_buf();
    readi_arg->n = sizeof(struct dirent);
    de = (struct dirent*)((char*)readi_arg + sizeof(struct readi_arg));
    for(off=0;off<dp->size; off+= sizeof(struct dirent)){
        readi_arg->offset = off;
        status = call_enclave(get_inode_handle(),READI,readi_arg,PREMAPEDSIZE,&ret);
        if(status || ret != sizeof(struct dirent)){
            return -1;
        }
        if(de->inum == 0){
            break;
        }
    }
    strncpy(de->name,name,DIRSIZ);
    de->inum = inum;
    //readi_arg and writei_arg have same structure
    status = call_enclave(get_inode_handle(),WRITEI,readi_arg,PREMAPEDSIZE,&ret);
    if(status || ret != sizeof(struct dirent)){
        return -1;
    }
    return 0;

}

static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

int namex(char* path, int nameiparent, char*name, struct inode *target_ip){
    struct inode *ip,next;
    void *buf = get_pre_mapped_buf();
    struct iget_arg *iget_arg = (struct iget_arg*)buf;
    ip = (struct inode*)buf;
    int status;
    uint64 ret;
    if(*path == '/'){
        iget_arg->dev = ROOTDEV;
        iget_arg->inum = ROOTINO;
        status = call_enclave(get_inode_handle(),IGET,iget_arg,PREMAPEDSIZE,&ret);
        if(status || ret){
            return -1;
        }

    }
    else{
        eapp_print("only support absolute path currently\n");
        return -1;
    }
    while((path=skipelem(path,name)) != 0){
        status = call_enclave(get_inode_handle(),ILOCK, buf,PREMAPEDSIZE,&ret);
        if(status || ret){
            return -1;
        }
        if(ip->type != T_DIR){
            status = call_enclave(get_inode_handle(),IUNLOCKPUT,buf,PREMAPEDSIZE,&ret);
            if(status || ret){
                return -1;
            }
        }
        if(nameiparent && *path == '\0'){
            status = call_enclave(get_inode_handle(),IUNLOCK,buf,PREMAPEDSIZE,&ret);
            if(status || ret){
                return -1;
            }
            memcpy(target_ip,ip,sizeof(struct inode));
            return 0;
        }
        if(dirlookup(ip,name,0,&next) != 0){
            call_enclave(get_inode_handle(),IUNLOCKPUT,buf,PREMAPEDSIZE,&ret);
            return -1;
        }
        status = call_enclave(get_inode_handle(),IUNLOCKPUT,buf,PREMAPEDSIZE,&ret);
        if(status || ret){
            return -1;
        }
        memcpy(ip,&next,sizeof(struct inode));
    }
    if(nameiparent){
        status = call_enclave(get_inode_handle(),IPUT,buf,PREMAPEDSIZE,&ret);
        if(status || ret){
            return -1;
        }
    }
    memcpy(target_ip,ip,sizeof(struct inode));
    return 0;
}

int namei(char* path,struct inode* ip){
    char name[DIRSIZ];
    return namex(path,0,name,ip);
}

int nameiparent(char* path,char* name,struct inode *ip){
    return namex(path,1,name,ip);
}

int EAPP_ENTRY main(){
    
}