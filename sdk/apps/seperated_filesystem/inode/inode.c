#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "biocallrags.h"
#include "string.h"
#include "inodecallarg.h"
#include "loggingcallarg.h"
#define min(a,b) ((a)<(b)?(a):(b))
#define PREMAPPED_SIZE (BSIZE*2)
#define BREAD 0
#define BWRITE 1
#define BRELSE 2
#define CACHE_BWRITE 3
#define BIGIN_OP 0
#define END_OP 1
#define LOG_WRITE 2
static uint64 __bio_handle = 0;
static uint64 __logging_handle = 0;
static void* __pre_mapped_buf = 0;
int inited = 0;
void* get_premapped_buf(){
    if(!__pre_mapped_buf){
        __pre_mapped_buf = eapp_mmap(0,PREMAPPED_SIZE);
    }
    return __pre_mapped_buf;
}

uint64 get_bio_handle(){
    if(!__bio_handle){
        __bio_handle = acquire_enclave("block_cache");
    }
    return __bio_handle;
}
uint64 get_logging_handle(){
    if(!__logging_handle){
        __logging_handle = acquire_enclave("logging");
    }
    return __logging_handle;
}
static void xv6_bzero(int dev, int bno){
    void* buf = get_premapped_buf();
    struct bread_arg *read_arg = (struct bread_arg*)buf;
    read_arg->dev = dev;
    read_arg->blockno = bno;
    uint64 ret;
    int status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: xv6_bzero error\n");
    }
    struct buf* b = (struct buf*)read_arg;
    memset(b->data,0,BSIZE);
    status = call_enclave(get_bio_handle(),CACHE_BWRITE,b,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: xv6_bzero error\n");
    }
    status = call_enclave(get_logging_handle(),LOG_WRITE,b,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: xv6_bzero error\n");
    }
    status = call_enclave(get_bio_handle(),BRELSE,b,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: xv6_bzero error\n");
    }
}

static uint balloc(uint dev){
    int  b,bi,m;
    struct buf *bp;
    uint64 ret;
    int status;
    struct bread_arg *arg = (struct bread_arg*)get_premapped_buf();
    for(b = 0;b < FSSIZE; b += BPB){
        arg->dev = dev;
        arg->blockno = BBLOCK(b);
        status = call_enclave(get_bio_handle(),BREAD,arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        bp = (struct buf*)arg;
        for(bi = 0; bi < BPB && b+bi<FSSIZE;bi++){
            m = 1 << (bi % 8);
            if(bp->data[bi/8]&m == 0){
                bp->data[bi/8] |= m;
                status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
                if(status || ret){
                    goto bad;
                }
                status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
                if(status || ret){
                    goto bad;
                }
                status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
                if(status || ret){
                    goto bad;
                }
                xv6_bzero(dev,b+bi);
                return b+bi;
            }

        }
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
    }
bad:
    eapp_print("inode: balloc error\n");
    return -1;
}

//FIXME: put all bfree in lower layer to get better pereformance?
static void bfree(int dev,uint b){
    struct bread_arg* read_arg = (struct bread_arg*)get_premapped_buf();
    read_arg->dev = dev;
    read_arg->blockno = b;
    int status;
    uint64 ret;
    struct buf *bp;
    int bi,m;
    status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: bfree error\n");
        return;
    }
    bi = b % BPB;
    m = 1 << (bi % 8);
    bp = (struct buf*)read_arg;
    if(bp->data[bi/8] == 0){
        eapp_print("inode: bfree error\n");
        return;
    }
    bp->data[bi/8] &= ~m;
    status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: bfree error\n");
        return;
    }
    status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: bfree error\n");
        return;
    }
    status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        eapp_print("inode: bfree error\n");
        return;
    }
}

struct {
    struct inode  inode[NINODE];
}icache;

void iinit(){
    for(int i = 0; i < NINODE; i++){
        icache.inode[i].index = i;
    }
}

int iget(uint dev, uint inum,void* buf, uint64 size){
    struct inode *ip, *empty;
    empty = 0;
    for(ip=&icache.inode[0]; ip<&icache.inode[NINODE];ip++){
        if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
            ip->ref ++;
            memcpy(buf,ip,sizeof(struct inode));
            return 0;
        }
        if(empty == 0 && ip->ref == 0){
            empty = ip;
        }
    }
    if(empty == 0){
        eapp_print("inode: iget error, no inodes\n");
        return -1;
    }
    ip = empty;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->flags = 0;
    memcpy(buf,ip,sizeof(struct inode));
    return 0;
}

int ialloc(uint dev, short type, void* buf, uint64 size){
    int inum;
    struct buf *bp;
    struct dinode *dip;
    uint64 ret;
    int status;
    struct bread_arg* read_arg = (struct bread_arg*)buf;
    for(inum = 1; inum < NINODES; inum++){
        read_arg->dev = dev;
        read_arg->blockno = IBLOCK(inum);
        status = call_enclave(get_bio_handle(),BREAD,buf,size,&ret);
        if(status || ret){
            goto bad;
        }
        bp = (struct buf*)buf;
        dip = (struct dinode*)bp->data + inum%IPB;
        if(dip->type == 0){
            memset(dip,0,sizeof(*dip));
            dip->type = type;
            status = call_enclave(get_bio_handle(),CACHE_BWRITE,buf,size,&ret);
            if(status || ret){
                goto bad;
            }
            status = call_enclave(get_logging_handle(),LOG_WRITE,buf,size,&ret);
            if(status || ret){
                goto bad;
            }
            status = call_enclave(get_bio_handle(),BRELSE,buf,size,&ret);
            if(status || ret){
                goto bad;
            }
            return iget(dev,inum,buf,size);
        }
    status = call_enclave(get_bio_handle(),BRELSE,buf,size,&ret);
    if(status || ret){
        goto bad;
    }
    }

bad:
    eapp_print("inode: ialloc failed\n");
    return -1;
}

int iupdate(void* buf,uint64 size){
    struct buf *bp = (struct buf*)get_premapped_buf();
    struct bread_arg* read_arg = (struct bread_arg*)bp;
    struct inode* ip = (struct inode*)buf;
    read_arg->dev = ip->dev;
    read_arg->blockno = IBLOCK(ip->inum);
    struct dinode *dip;
    uint64 ret;
    int status;
    status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
    if(status || ret){
        goto bad;
    }
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs,ip->addrs,sizeof(ip->addrs));
    status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        goto bad;
    }
    status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        goto bad;
    }
    status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
    if(status || ret){
        goto bad;
    }
    return 0;
bad:
    eapp_print("inode: iupdate error\n");
    return -1;
}
//FIXME: merge iget and lock together?
int ilock(void* buf, uint64 size){
    struct inode* ip = (struct inode*)buf;
    struct dinode *dip;
    struct buf *bp;
    uint64 ret;
    int status;
    if(ip == 0 || ip->ref < 1){
        eapp_print("inode: ilock error\n");
        return -1;
    }
    if(ip->flags & I_BUSY){
        return -1;
    }
    ip->flags |= I_BUSY;
    if(!(ip->flags&I_VALID)){
        bp = (struct buf*)get_premapped_buf();
        struct bread_arg* read_arg = (struct bread_arg*)bp;
        read_arg->dev = ip->dev;
        read_arg->blockno = IBLOCK(ip->inum);
        status = call_enclave(get_bio_handle(),BREAD,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        dip = (struct dinode*)bp->data + ip->inum%IPB;
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs,dip->addrs,sizeof(ip->addrs));
        ip->flags |= I_VALID;
        memcpy(&icache.inode[ip->index],ip,sizeof(struct inode));
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        if(ip->type == 0){
            eapp_print("inode: ilock error\n");
            return -1;
        }
        return 0;
    }
bad:
    eapp_print("inode: lock error\n");
    return -1;
}
int iunlock(void* buf, uint64 size){
    struct inode *ip = (struct inode*)buf;
    if(ip == 0|| !(ip->flags&I_BUSY) || ip->ref < 1 || ip->index >= NINODE){
        eapp_print("inode: iunlock error\n");
        return -1;
    }
    ip->flags &= ~I_BUSY;
    icache.inode[ip->index].flags &= ~I_BUSY;
    return 0;
}
//FIXME: it can be really slow if the file is big
int itrunc(void *buf,uint64 size){
    struct inode *ip = (struct inode*)buf;
    uint index = ip->index;
    int i,j,k;
    struct buf *bp;
    struct buf *tmp_bp = (struct buf*)malloc(sizeof(struct buf));
    struct buf *direct_tmp_bp = (struct buf*)malloc(sizeof(struct buf));
    uint *a, *b;;
    uint64 ret;
    int status;
    for(i=0;i<NDIRECT;i++){
        if(ip->addrs[i]){
            bfree(ip->dev,ip->addrs[i]);
            ip->addrs[i] = 0;
            icache.inode[index].addrs[i] = 0;
        }
    }
    if(ip->addrs[NDIRECT]){
        bp = (struct buf*)get_premapped_buf();
        struct bread_arg *read_arg = (struct bread_arg*)bp;
        read_arg->dev = ip->dev;
        read_arg->blockno = ip->addrs[NDIRECT];
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        memcpy(tmp_bp,bp,sizeof(struct buf));
        a = (uint*)tmp_bp->data;
        for(j=0;j<NINDIRECT;j++){
            if(a[j]){ // FIXME: if a[j] == 0 can directly break cause the file content don't have hole?
                bfree(ip->dev,a[j]);
            }
        }
        memcpy(bp,tmp_bp,sizeof(struct buf));
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        bfree(ip->dev,ip->addrs[NDIRECT]);
        ip->addrs[NINDIRECT] = 0;
        icache.inode[index].addrs[NINDIRECT] = 0;
    }
    if(ip->addrs[NDIRECT+1]){ // double indirect block data
        struct bread_arg *read_arg = (struct bread_arg*)get_premapped_buf();
        read_arg->dev = ip->dev;
        read_arg->blockno = ip->addrs[NDIRECT+1];
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        memcpy(tmp_bp,read_arg,sizeof(struct buf));
        a = (uint*)tmp_bp->data;
        for(j = 0; j<NINDIRECT; i++){
            if(a[j]){
                read_arg->dev = ip->dev;
                read_arg->blockno = a[j];
                status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
                if(status || ret){
                    goto bad;
                }
                memcpy(direct_tmp_bp,read_arg,sizeof(struct buf));
                b = (uint*)direct_tmp_bp->data;
                for(k = 0;k < NINDIRECT; k++){
                    if(b[k]){
                        bfree(ip->dev,b[k]);
                    }
                }
                memcpy(read_arg,direct_tmp_bp,sizeof(struct buf));
                status = call_enclave(get_bio_handle(),BRELSE,read_arg,PREMAPPED_SIZE,&ret);
                if(status || ret){
                    goto bad;
                }
            }
        }
        memcpy(read_arg,tmp_bp,sizeof(struct buf));
        status = call_enclave(get_bio_handle(),BRELSE,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            goto bad;
        }
        ip->addrs[NDIRECT+1] = 0;
        icache.inode[index].addrs[NDIRECT+1] = 0;
    }
    goto good;
bad:
    if(tmp_bp){
       free(tmp_bp);
    };
    if(direct_tmp_bp){
        free(direct_tmp_bp);
    }
    eapp_print("inode: itrunc failed\n");
    return -1;
good:
    ip->size = 0;
    memcpy(&icache.inode[ip->index],ip,sizeof(struct inode));
    return iupdate(buf,size);
}

int iput(void* buf, uint64 size){
    struct inode *ip = (struct inode*)buf;
    if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
        if(ip->flags & I_BUSY){
            eapp_print("inode: iput error\n");
            return -1; 
        }
        ip->flags |= I_BUSY;    
        icache.inode[ip->index].flags |= I_BUSY;
        itrunc(buf,size);   
        ip->type = 0;
        icache.inode[ip->index].type = 0;
        ip->flags = 0;
        icache.inode[ip->index].flags = 0;
        iupdate(buf,size);
    }
    ip->ref --;
}

int iunlockput(void* buf,uint64 size){
    int a = iunlock(buf,size);
    int b = iput(buf,size);
    return a||b?-1:0;
}

static uint bmap(struct inode *ip, uint bn){
    uint addr, *a;
    struct buf * bp;
    struct bread_arg* read_arg;
    if(bn < NDIRECT){
        if((addr = ip->addrs[bn]) == 0){
            ip->addrs[bn] = addr = balloc(ip->dev);
        }
        icache.inode[ip->index].addrs[bn] = addr;
        return addr;
    }
    bn -= NDIRECT;
    if(bn < NINDIRECT){
        if((addr = ip->addrs[NDIRECT]) == 0){
            ip->addrs[NDIRECT] = addr = balloc(ip->dev);
            icache.inode[ip->index].addrs[NDIRECT] = addr;
        }
       read_arg = (struct bread_arg*)get_premapped_buf();
       read_arg->dev = ip->dev;
       read_arg->blockno = addr;
       uint64 ret;
       int status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
       if(status || ret){
           eapp_print("inode: bmap error\n");
           return -1;
       }
       bp = (struct buf*)read_arg;
       a = (uint*)bp->data;
       if((addr = a[bn]) == 0){
           a[bn] = addr = balloc(ip->dev);
           status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
           if(status || ret){
               eapp_print("inode: bmap error\n");
               return -1;
           }
           status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
           if(status || ret){
               eapp_print("inode: bmap error\n");
               return -1;
           }   
       } 
       status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
       if(status || ret){
           eapp_print("inode: bmap error\n");
           return -1;
       }
       return addr;
    }
    if(bn < MAXFILE - NDIRECT){
        int bni;
        uint64 ret;
        int status;
        bn -= NINDIRECT;
        if((addr = ip->addrs[NDIRECT+1]) == 0){
            ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
            icache.inode[ip->index].addrs[NDIRECT+1] = addr;
        }
        read_arg = (struct bread_arg*)get_premapped_buf();
        read_arg->dev = ip->dev;
        read_arg->blockno = addr;
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: bmap error\n");
            return -1;
        }
        bp = (struct buf*)read_arg;
        a = (uint*)bp->data;
        bni = bn/NINDIRECT;
        if((addr = a[bni]) == 0){
            a[bni] = addr = balloc(ip->dev);
            bp->data[bni] = addr;
            status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
            if(status || ret){
                eapp_print("inode: bmap error\n");
                return -1;
            }
            status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
            if(status || ret){
                eapp_print("inode: bmap error\n");
                return -1;
            }
        }
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: bmap error\n");
            return -1;
        }
        read_arg->dev = ip->dev;
        read_arg->blockno = addr;
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: bmap error\n");
            return -1;
        }
        a = (uint*)bp->data;
        bni = bn % NINDIRECT;
        if((addr = a[bni]) == 0){
            a[bni] = addr = balloc(ip->dev);
            status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
            if(status || ret){
                eapp_print("inode: bmap error\n");
                return -1;
            }
            status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
            if(status || ret){
                eapp_print("inode: bmap error\n");
                return -1;
            }
        }
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: bmap error\n");
            return -1;
        }
        //FIXME: fscq call bread and brelse here and I removed
        return addr;
    }
    eapp_print("inode: bmap error\n");
    return -1;
}

int stati(void* buf,uint64 size){
    struct inode *ip = (struct inode*)buf;
    ip = &icache.inode[ip->index];
    struct stat *st = (struct stat*)((char*)buf+sizeof(struct inode));
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->size = ip->size;
    st->nlink = ip->nlink;
    return 0;
}

int readi(void *buf, uint64 size){
    struct readi_arg *arg = ((struct readi_arg*)buf)->inode.index;
    struct inode *ip = &icache.inode[arg->inode.index];
    uint off = arg->offset;
    uint n = arg->n;
    if(off > ip->size || off + n < off){
        return -1;
    }
    if(off + n > ip->size){
        n = ip->size - off;
    }
    n = min(n,size - arg->start_offset);
    char* dst = (char*)buf + arg->start_offset;
    uint tot,m;
    struct buf * bp = get_premapped_buf();
    struct bread_arg *read_arg = (struct bread_arg*)bp;
    uint64 ret;
    int status;
    for(tot=0;tot<n; tot +=m ,off+=m,dst +=m){
        read_arg->dev = ip->dev;
        read_arg->blockno = bmap(ip,off/BSIZE);
        if(read_arg->blockno == -1){
            eapp_print("inode: readi\n");
            return tot;
        }
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: readi\n");
            return tot;
        }
        m = min(n-tot,BSIZE-off%BSIZE);
        memmove(dst,bp->data+off%BSIZE,m);
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: readi");
            return tot;
        }
    }
    return n;
}

int writei(void* buf,uint64 size){
    struct writei_arg *arg = ((struct writei_arg*)buf)->inode.index;
    struct inode *ip = &icache.inode[arg->inode.index];
    uint off = arg->offset;
    uint n = arg->n;
    char* src = (char*)buf + arg->start_offset;
    uint tot,m;
    struct buf *bp = get_premapped_buf();
    struct bread_arg *read_arg = (struct bread_arg*)bp;
    if(off > ip->size || off + n < off){
        return -1;
    }
    if(off + n > MAXFILE*BSIZE){
        return -1;
    }
    n = min(n,size - arg->start_offset);
    int status;
    uint64 ret;
    for(tot = 0;tot < n;tot+=m,off+=m,src+=m){
        status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAPPED_SIZE,&ret);
        if(status | ret){
            eapp_print("inode: writei\n");
            return tot;
        }
        m = min(n - tot, BSIZE - off%BSIZE);
        memmove(bp->data + (off % BSIZE), (char*)src, m);
        status = call_enclave(get_bio_handle(),CACHE_BWRITE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: writei\n");
            call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
            return tot;
        }
        status = call_enclave(get_logging_handle(),LOG_WRITE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: writei");
            call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
            return tot;
        }
        status = call_enclave(get_bio_handle(),BRELSE,bp,PREMAPPED_SIZE,&ret);
        if(status || ret){
            eapp_print("inode: writei");
            return tot;
        }
    }
    if(n>0){
        ip->size = off;
        memcpy(&arg->inode,ip,sizeof(struct inode));
        memcpy(bp,ip,sizeof(struct inode));
        iupdate(bp,PREMAPPED_SIZE);
    }
    return n;
}

int EAPP_ENTRY main(){
    uint64 *args;
    EAPP_RESERVE_REG;
    eapp_print("inode server begin to run\n");
    if(!inited){
        iinit();
        inited = 1;
    }
    uint64 service_type = args[10];
    switch (service_type)
    {
    case IGET:{
        {
         struct iget_arg *arg = (struct iget_arg*)args[11];
         int ret = iget(arg->dev,arg->inum,args[11],args[12]);
         SERVER_RETURN(ret,args[11],args[12]);
         break;
        }
    case READI:
        {
         int ret = readi(args[11],args[12]);
         SERVER_RETURN(ret,args[11],args[12]);
         break;
        }
    case WRITEI:
        {
         int ret = writei(args[11],args[12]);
         SERVER_RETURN(ret,args[11],args[12]);
         break;
        }
    case STATI:
        {
         int ret = stati(args[11],args[12]);
         SERVER_RETURN(ret,args[11],args[12]);
         break;
        }
    case IPUT:
        {
            int ret = iput(args[11],args[12]);
            SERVER_RETURN(ret,args[11],args[12]);
            break;
        }
    case ILOCK:
        {
            int ret = ilock(args[11],args[12]);
            SERVER_RETURN(ret,args[11],args[12]);
            break;
        }
    case IUNLOCK:
        {
            int ret = iunlock(args[11],args[12]);
            SERVER_RETURN(ret,args[11],args[12]);
            break;
        }
    case IUNLOCKPUT:
        {
            int ret = iunlockput(args[11],args[12]);
            SERVER_RETURN(ret,args[11],args[12]);
            break;
        }
    default:
        eapp_print("inode call service type error\n");
        SERVER_RETURN(-1,args[11],args[12]);
        break;
    }
}