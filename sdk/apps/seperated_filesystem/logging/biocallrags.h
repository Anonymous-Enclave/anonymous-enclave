#ifndef __BIO_CALLARGS__
#define __BIP_CALLARGS__
struct bread_arg
{
    uint dev;
    uint blockno;
};


struct install_trans_arg{
    uint src_dev;
    uint src_block_no;
    uint dst_dev;
    uint dst_block_no;
};
#define BREAD 0
#define BWRITE 1
#define BRELSE 2
#define INSTALL_TRANS 3
#define BCACHE_WRITE 4
#endif