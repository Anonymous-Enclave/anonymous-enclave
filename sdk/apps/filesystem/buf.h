#ifndef __XV6BUF__
#define __XV6BUF__
struct buf {
  int flags;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  // struct sleeplock lock;
  // pthread_mutex_t lock;  using B_BUSY flag to maintain exclusion
  // uint refcnt;   without lock, a buf can only be referenced by one thread
  struct buf *prev; // LRU cache list
  struct buf *next;
  struct buf *qnext; // disk queue
  uchar data[BSIZE];
};
#define B_BUSY  0x1  // buffer is locked by some process
#define B_VALID 0x2  // buffer has been read from disk
#define B_DIRTY 0x4  // buffer needs to be written to disk
#endif