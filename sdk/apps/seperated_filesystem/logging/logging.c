#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "print.h"
#include "biocallrags.h"
#include "loggingcallarg.h"
#define PREMAP_SIZE  (BSIZE*2)

struct logheader{
    int n;
    int block[LOGSIZE];
};

struct log{
    int start;
    int size;
    int outstanding;
    int committing;
    int dev;
    struct logheader lh;
};

struct log log;

static void recover_fromo_log();

static void commit();

static uint64 get_bio_handle(){
    static uint64 __bio_handle = 0;
    if(!__bio_handle){
        __bio_handle = acquire_enclave("block_cache");
    }
    return __bio_handle;
}

static void* get_pre_mapped_buf(){
    static void* __pre_mapped_buf = 0;
    if(!__pre_mapped_buf){
        __pre_mapped_buf = eapp_mmap(0,PREMAP_SIZE);
    }
    return __pre_mapped_buf;
}

void initlog(int dev){
    if(sizeof(struct logheader) >= BSIZE){
        eapp_print("init logging failed\n");
    }
    log.start = LOGSTART;
    log.size = LOGSIZE;
    log.dev = dev;
    recover_fromo_log();
}

// install transactions
static void install_trans(void){
    int tail;
    void *pre_mapped_buf = get_pre_mapped_buf();
    struct install_trans_arg* arg = (struct install_trans_arg*)pre_mapped_buf;
    uint64 ret;
    for(tail = 0; tail < log.lh.n; tail++){
        int status = call_enclave(get_bio_handle(),INSTALL_TRANS,arg,PREMAP_SIZE,&ret);
        if(status || ret){
            eapp_print("logging:instal_trans failed\n");
        }
    }
}

static void read_head(){
    struct bread_arg *arg = (struct bread_arg*)get_pre_mapped_buf();
    arg->dev = log.dev;
    arg->blockno = log.start;
    uint64 ret;
    int status = call_enclave(get_bio_handle(),BREAD,arg,PREMAP_SIZE,&ret);
    if(status || (int)ret == -1){
        eapp_print("logging: read_head failed\n");
        return;
    }
    struct logheader *lh = (struct logheader*)((struct buf*)arg)->data;
    int i;
    log.lh.n = lh->n;
    for(i = 0; i < log.lh.n;i++){
        log.lh.block[i] = lh->block[i];
    }
    uint64 rel_ret;
    status = call_enclave(get_bio_handle(),BRELSE,arg,PREMAP_SIZE,&rel_ret);
    if(status || rel_ret){
        eapp_print("logging: read_head failed\n");
        return;
    }
}

static void write_head(){
    //FIXME: flush first, add later
    struct bread_arg* read_arg = (struct bread_arg*)get_pre_mapped_buf();
    read_arg->dev = log.dev;
    read_arg->blockno = log.start;
    uint64 ret;
    int status = call_enclave(get_bio_handle(),BREAD,read_arg,PREMAP_SIZE,&ret);
    if(status || (int)ret == -1){
        eapp_print("logging: write_head failed\n");
        return;
    }
    struct logheader* lh = (struct logheader*)((struct buf*)read_arg)->data;
    lh->n = log.lh.n;
    int i;
    for(i = 0;i<log.lh.n;i++){
        lh->block[i] = log.lh.block[i];
    }
    uint64 flag_ret;
    status = call_enclave(get_bio_handle(),BWRITE,lh,PREMAP_SIZE,&flag_ret);
    if(status || flag_ret){
        eapp_print("logging: write_head failed\n");
        return;
    }
    status = call_enclave(get_bio_handle(),BRELSE,read_arg,PREMAP_SIZE,&flag_ret);
    if(status || flag_ret){
        eapp_print("logging: write_head failed\n");
        return;
    }

}

static void recover_from_log(){
    read_head();
    install_trans();
    log.lh.n = 0;
    write_head();
}

static void write_log(){
    int tail;
    void *buf = get_pre_mapped_buf();
    struct install_trans_arg *arg = (struct install_trans_arg*)buf;
    uint64 ret;
    int status;
    for(tail = 0;tail < log.lh.n; tail++){
        arg->src_dev = log.dev;
        arg->src_block_no = log.lh.block[tail];
        arg->dst_dev = log.dev;
        arg->dst_block_no = log.start + tail + 1;
        status = call_enclave(get_bio_handle(),INSTALL_TRANS,arg,PREMAP_SIZE,&ret);
        if(status || ret){
            eapp_print("logging: write_log failed");
        }
    }
}

static void commit(){
    if(log.lh.n > 0){
        write_log();
        write_head();
        install_trans();
        log.lh.n = 0;
        write_head();
    }
}

int begin_op(){
    if(log.committing){
        return -1;
    }
    if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
        return -1;
    }
    log.outstanding += 1;
    return 0;
}

int  end_op(){
    int do_commit = 0;
    if(log.committing){
        eapp_print("logging: end_op error\n");
        return -1;
    }
    log.outstanding -= 1;
    if(log.outstanding == 0){
        do_commit = 1;
        log.committing = 1;
    }
    if(do_commit){
        commit();
        log.committing = 0;
    }
    return 0;
}

int log_write(void* buf, uint64 size){
    int i;
    struct buf *b = (struct buf*)buf;
    if(log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1){
        eapp_print("logging: log_write failed\n");
        return -1;
    }
    if(log.outstanding < 1){
        eapp_print("logging: log_write failed\n");
        return -1;
    }
    for(i = 0; i < log.lh.n;i++){
        if(log.lh.block[i] == b->blockno){
            break;
        }
    }
    log.lh.block[i] = b->blockno;
    if(log.lh.n == i){
        log.lh.n++;
        b->flags |= B_DIRTY;
    }
}

int EAPP_ENTRY main(){
    uint64* args;
    EAPP_RESERVE_REG;
    uint64 service_type = args[10];
    eapp_print("logging server begin to run\n");
    switch (service_type)
    {
    case BIGIN_OP:
        int ret = begin_op();
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case END_OP:
        int ret = end_op();
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    case LOG_WRITE:
        int ret = log_write(args[11],args[12]);
        SERVER_RETURN(ret,args[11],args[12]);
        break;
    default:
        eapp_print("Bat Request to Logging Server\n");
        break;
    }
}