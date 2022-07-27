// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define BUCNUM 13
#define hash(blockno) ((blockno)%BUCNUM)

struct {
  struct spinlock lock[BUCNUM];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[BUCNUM];
} bcache;

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < BUCNUM; i++)
  {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked list of buffers
  
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock[hash(blockno)]);

  // Is the block already cached?
  for(b = bcache.head[hash(blockno)].next; b != &bcache.head[hash(blockno)]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hash(blockno)]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (int i = 1; i < BUCNUM; i++)
  {
    int no = hash(blockno + i);
    acquire(&bcache.lock[no]);
    for (b = bcache.head[no].next; b != &bcache.head[no]; b = b->next)
    {
      if (b->refcnt == 0)
      {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.lock[no]);
        //插入尾部，尽可能让refcnt=0的buffer在靠前的位置
        b->next = &bcache.head[hash(blockno)];
        b->prev = bcache.head[hash(blockno)].prev;
        bcache.head[hash(blockno)].prev->next = b;
        bcache.head[hash(blockno)].prev = b;
        release(&bcache.lock[hash(blockno)]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[no]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int no = hash(b->blockno);
  acquire(&bcache.lock[no]);
  b->refcnt--;
  if (b->refcnt == 0) {//插入头部，尽可能让LRU靠前
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[no].next;
    b->prev = &bcache.head[no];
    bcache.head[no].next->prev = b;
    bcache.head[no].next = b;
  }
  
  release(&bcache.lock[no]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock[hash(b->blockno)]);
  b->refcnt++;
  release(&bcache.lock[hash(b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[hash(b->blockno)]);
  b->refcnt--;
  release(&bcache.lock[hash(b->blockno)]);
}
