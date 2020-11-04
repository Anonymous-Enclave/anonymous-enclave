#include "types.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "buf.h"
#include "param.h"
#include "stdio.h"
struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void binit(void){
  struct buf *b;
  initlock(&bcache.lock, "bcache");
  // pthread_mutex_init(&bcache.lock,NULL);
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    // initsleeplock(&b->lock, "buffer");
    b->dev = -1; // does not belong to any device
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.

static struct buf* bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);
  // pthread_mutex_lock(&bcache.lock);
  // Is the block already cached?
loop:
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      if(!(b->flags & B_BUSY)){
        b->flags |= B_BUSY;
        release(&bcache.lock);
        return b;
      }
      // b->refcnt++;
      // pthread_mutex_unlock(&bcache.lock);
    //   acquiresleep(&b->lock);
      sleep(b,&bcache.lock);
      goto loop;
    }
  }
  // Not cached; recycle an unused buffer.
  b = bcache.head.prev;
  // for(; b != (&bcache.head); b = b->prev){
  while(b != (&bcache.head)){
    if((b->flags & B_BUSY) == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      // b->valid = 0;
      // b->refcnt = 1;
      b->flags = B_BUSY;
      release(&bcache.lock);
      return b;
    }
    b = b->prev;
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.

struct buf* bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!(b->flags & B_VALID)) {
    // virtio_disk_rw(b, 0);
    ramdiskrw(b);
    b->flags |= B_VALID;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// FIXME: how to check if the current thread holding a lock
// DONE: pthread_mutex_t cannot acomplish this

void bwrite(struct buf *b)
{
    // FIXME: just remove the check temporarily
//   if(!holdingsleep(&b->lock))
//     panic("bwrite");
  if((b->flags & B_BUSY) == 0){
    panic("bwrite");
  }
  b->flags |= B_DIRTY;
  // virtio_disk_rw(b, 1);
  ramdiskrw(b);
}

// Release a locked buffer.
// Move to the head of the MRU list.

void brelse(struct buf *b)
{
    //FIXME: 
//   if(!holdingsleep(&b->lock))
//     panic("brelse");
     if((b->flags & B_BUSY) == 0){
       panic("brelse");
     }
//   releasesleep(&b->lock);
  // pthread_mutex_unlock(&b->lock);
  acquire(&bcache.lock);
  // pthread_mutex_lock(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  // }
  
//   release(&bcache.lock);
  b->flags &= ~B_BUSY;
  release(&bcache.lock);
  // pthread_mutex_unlock(&bcache.lock);
}


// void bpin(struct buf *b) {
// //   acquire(&bcache.lock);
//   pthread_mutex_lock(&bcache.lock);
//   b->refcnt++;
// //   release(&bcache.lock);
//   pthread_mutex_unlock(&bcache.lock);
// }


// void bunpin(struct buf *b) {
// //   acquire(&bcache.lock);
//   pthread_mutex_lock(&bcache.lock);
//   b->refcnt--;
// //   release(&bcache.lock);
//   pthread_mutex_unlock(&bcache.lock);
// }