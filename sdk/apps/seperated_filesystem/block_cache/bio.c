#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "print.h"
#include "biocallrags.h"

#define PREMAP_SIZE (BSIZE*2)
// FIXME: remove all the lock temporarily
// FIXME: can define some status code to ther calling client
static int inited;
static unsigned long __ramdisk_handle = 0;
static void *__premapped_buf = 0;
struct
{
    struct buf buf[NBUF];
    struct buf head;
}bcache;

unsigned long get_ramdisk_handle(){
    if(!__ramdisk_handle){
        __ramdisk_handle = acquire_enclave("ramdisk");
    }
    return __ramdisk_handle;
}

void* get_premapped_buf(){
    if(!__premapped_buf){
        __premapped_buf = eapp_mmap(0,PREMAP_SIZE);
    }
    return __premapped_buf;
}

void binit(void){
    struct buf *b;
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        b->dev = -1;
        b->index = b-bcache.buf;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

static struct buf* bget(uint dev, uint blockno){
    struct buf *b;
    // acquire bcache lock 
    for(b = bcache.head.next; b != &bcache.head; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            if(!(b->flags & B_BUSY)){ // the cache not busy
                b->flags |= B_BUSY;
                // release bcache lock
                return b;
            }
            // jump to schedule?
            // sleep
            return 0;
        }
    }

    b = bcache.head.prev;
    while(b != (&bcache.head)){
      if((b->flags & B_BUSY) == 0 && (b->flags & B_DIRTY) == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->flags = B_BUSY;
        return b;
      }
      b = b->prev;
  }

  return b;
}

int bread(uint dev, uint blockno,void* message_buf,uint64 size){
    struct buf *b;
    b = bget(dev,blockno);
    if(!b){
        return -1;
    }
    if(!(b->flags & B_VALID)){
        memcpy(message_buf,b,sizeof(struct buf));
        uint64 ret;
        call_enclave(get_ramdisk_handle(),0,message_buf,size,&ret);
        if(ret != 0){
            return -1;
        }
        memcpy(b,message_buf,sizeof(struct buf));
        return 0;
    }
    memcpy(message_buf,b,sizeof(struct buf));
    return 0;
}

int bwrite(struct buf *b, uint64 size){
    if((b->flags & B_BUSY) == 0){
        return -1;
    }
    b->flags |= B_DIRTY;
    uint64 ret;
    call_enclave(get_ramdisk_handle(),0,b,size,&ret);
    return ret;
}

int bcache_write(struct buf* b){
    if(b->index >= NBUF){
        eapp_print("block_cache: bcache_write failed\n");
        return -1;
    }
    memcpy(bcache.buf + b->index, b, sizeof(struct buf));
    return 0;
}

int brelse(struct buf* buf){
    struct buf *b = bcache.buf + buf->index;
    if((b->flags & B_BUSY) == 0){
        return -1;
    }
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;

    b->flags &= ~B_BUSY;
    return 0;
}

int install_trans_block(struct install_trans_arg * arg, uint size){
    struct buf *src_b = bget(arg->src_dev,arg->src_block_no);
    struct buf *dst_b = bget(arg->dst_dev,arg->dst_block_no);
    if(!src_b || !dst_b){
        return -1;
    }
    if(!(src_b->flags)&B_VALID){
        memcpy(arg,src_b,sizeof(struct buf));
        uint64 ret;
        int status = call_enclave(get_ramdisk_handle(),0,arg,size,&ret);
        if(ret == -1 || !status){
            return -1;
        }
        memcpy(src_b,arg,sizeof(struct buf));
    }
    memcpy(dst_b->data,src_b->data,BSIZE);
    struct buf *tmp_buf = (struct buf*)arg;
    tmp_buf->blockno = dst_b->blockno;
    tmp_buf->flags |= B_DIRTY;
    tmp_buf->dev = dst_b->dev;
    uint64 ret;
    int status = call_enclave(get_ramdisk_handle(),0,arg,size,&ret);
    if(ret == -1 || !status){
        return -1;
    }
    dst_b->flags &= ~B_DIRTY;
    brelse(src_b-bcache.buf);
    brelse(src_b-bcache.buf);
    return 0;
}
int EAPP_ENTRY main(){
    uint64 *args;
    EAPP_RESERVE_REG;
    eapp_print("block cache server run\n");
    if(!inited){
        binit();
        inited = 1;
    }
    uint64 service_type = args[10];
    switch (service_type)
    {
    case BREAD:
        struct bread_arg *arg = (struct bread_arg*)args[11];
        uint dev = arg->dev;
        uint blockno = arg->blockno;
        int ret = bread(dev,blockno,args[11],args[12]);
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case BWRITE:
        int ret = bwrite((struct buf*)args[11],args[12]);
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case BRELSE:
        int ret = brelse(((struct buf*)args[11]));
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case INSTALL_TRANS:
        int ret = install_trans_block((struct install_trans_arg*)args[11],args[12]);
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case BCACHE_WRITE:
        int ret = bcache_write((struct buf*)args[11]);
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    default:
        break;
    }
}