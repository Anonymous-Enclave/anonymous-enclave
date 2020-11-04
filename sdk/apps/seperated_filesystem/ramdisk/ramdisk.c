#include "types.h"
#include "structure.h"
#include "eapp.h"
#include "print.h"
//FIXME: the filesystem size need to de re-considered
static char ramdisk[FSSIZE*BSIZE];
static char* test_content = "Hello World!";
static struct superblock init_sb;
static uint freeblock;
static int inited;
// TODO: add the communication code and message-pass (or shared memory) interface

void wsect(uint sec,void* buf){
  memmove(ramdisk + sec*BSIZE, buf, BSIZE);
}

void rsect(uint sec, void* buf){
  memmove(buf, ramdisk + sec*BSIZE, BSIZE);
}

void rinode(uint inum, struct dinode* ip){
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;
  bn = IBLOCK(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void winode(uint inum, struct dinode* ip){
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;
  bn = IBLOCK(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

uint ialloc(ushort type){
  static uint freeinode = 1;
  uint inum = freeinode ++;
  struct dinode di;
  memset(&di, 0, sizeof(di));
  di.type = type;
  di.size = 0;
  di.nlink = 1;
  winode(inum,&di);
  return inum;
}

void balloc(int used){
  uchar buf[BSIZE] = {0};
  int i;

  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  wsect(BMAPSTART, buf);
}

void iappend(uint inum, void* xp, int n){
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT] = {0}; // FIXME: doses it need to be set 0 ?
  uint x;
  rinode(inum,&din);
  off = din.size;
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(din.addrs[fbn] == 0){
        din.addrs[fbn] = freeblock ++;
      }
      x = din.addrs[fbn];
    }
    else{
      if(din.addrs[NDIRECT] == 0){
        din.addrs[NDIRECT] = freeblock ++;
        wsect(din.addrs[NDIRECT], (char*)indirect);
      }
      x = indirect[fbn-NDIRECT];
    }
    n1 =  min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    memcpy(buf + off - (fbn * BSIZE), p,n1);
    wsect(x,buf);
    n -= n1;
    p += n1;
    off += n1;
  }
  din.size = off;
  winode(inum, &din);
}

//TODO: get information from message and do the read-write for client eapp
void ramdiskinit(void){
  // FIXME: at this moment, should do the work like mkfs do.
  char buf[BSIZE] = {0};
  struct superblock init_sb;
  struct dirent de;
  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);
  // make superblock
  init_sb.magic = FSMAGIC;
  init_sb.nlog = LOGSIZE;
  init_sb.ninodes = NINODES;
  init_sb.logstart = 2;
  init_sb.inodestart = 2 + LOGSIZE;
  init_sb.bmapstart = BMAPSTART;
  init_sb.size = FSSIZE;
  int nbitmap = FSSIZE/(BSIZE*8) + 1;
  int nmeta = 2 + LOGSIZE + NINODES/IPB + 1 + nbitmap;
  init_sb.nblocks = FSSIZE - nmeta;
  memset(buf, 0, BSIZE);
  memmove(buf,&init_sb, sizeof(struct superblock));
  wsect(1,buf);
  freeblock = nmeta;
  uint rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  memset(&de, 0, sizeof(de));
  de.inum = rootino;
  strcpy(de.name,".");
  iappend(rootino,&de,sizeof(de));

  memset(&de, 0, sizeof(de));
  de.inum = rootino;
  strcpy(de.name,"..");
  iappend(rootino, &de, sizeof(de));

  uint inum;
  inum = ialloc(T_DIR);
  memset(&de,0,sizeof(de));
  de.inum = inum;
  strcpy(de.name,"sub");
  iappend(rootino, &de, sizeof(de));
  struct dirent tmp_de;
  memset(&tmp_de, 0, sizeof(tmp_de));
  tmp_de.inum = ialloc(T_FILE);
  strcpy(tmp_de.name,"test.txt");
  iappend(de.inum,&tmp_de,sizeof(tmp_de));
  iappend(tmp_de.inum,test_content,strlen(test_content) + 1);  
  memset(&de, 0 ,sizeof(de));
  de.inum = ialloc(T_FILE);
  strcpy(de.name,"test.txt");
  iappend(rootino,&de,sizeof(de));
  iappend(de.inum,test_content, strlen(test_content) + 1);
  balloc(freeblock);
}


// TODO: modify read-write interface
int ramdiskrw(struct buf* b){
//   if(!holdingsleep(&b->lock))
//     panic("ramdiskrw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    return -1;

  if(b->blockno >= FSSIZE)
    return -1;

  uint64 diskaddr = b->blockno * BSIZE;
  char *addr = (char *)ramdisk + diskaddr;

  if(b->flags & B_DIRTY){
    // write
    memmove(addr, b->data, BSIZE);
    b->flags &= ~B_DIRTY;
  } else {
    // read
    memmove(b->data, addr, BSIZE);
    b->flags |= B_VALID;
  }
  return 0;
}

int EAPP_ENTRY main(int argc,char* argv[]){
    unsigned long* args;
    EAPP_RESERVE_REG;
    eapp_print("ramdisk server begin to run\n");
    if(!inited){
      ramdiskinit();
      inited = 1;
    }
    struct buf *buf = (struct buf*)args[11];
    int ret = ramdiskrw(buf);

    SERVER_RETURN(ret,args[11],args[12]);
}