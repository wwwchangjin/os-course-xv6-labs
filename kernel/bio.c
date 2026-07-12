// Buffer cache.
//
// The buffer cache stores cached copies of disk blocks.
// Buffers are divided into hash buckets so that accesses
// to different disk blocks can proceed in parallel.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct bucket {
  struct spinlock lock;

  struct buf head;
};

struct {

  struct spinlock evict_lock;

  struct bucket bucket[NBUCKET];
  struct buf buf[NBUF];
} bcache;

static int
hash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

static void
removebuf(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
}

static void
insertbuf(int index, struct buf *b)
{
  struct buf *head = &bcache.bucket[index].head;

  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evict_lock, "bcache.evict");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");

    bcache.bucket[i].head.next = &bcache.bucket[i].head;
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
  }

  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];

    b->valid = 0;
    b->dev = 0;
    b->blockno = 0;
    b->refcnt = 0;

    initsleeplock(&b->lock, "buffer");

    int index = i % NBUCKET;
    insertbuf(index, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return a sleep-locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = hash(dev, blockno);

  acquire(&bcache.bucket[index].lock);

  for(b = bcache.bucket[index].head.next;
      b != &bcache.bucket[index].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;

      release(&bcache.bucket[index].lock);
      acquiresleep(&b->lock);

      return b;
    }
  }

  release(&bcache.bucket[index].lock);

  acquire(&bcache.evict_lock);


  acquire(&bcache.bucket[index].lock);

  for(b = bcache.bucket[index].head.next;
      b != &bcache.bucket[index].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;

      release(&bcache.bucket[index].lock);
      release(&bcache.evict_lock);

      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[index].lock);

  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.bucket[i].lock);

    for(b = bcache.bucket[i].head.next;
        b != &bcache.bucket[i].head;
        b = b->next){
      if(b->refcnt == 0){
        b->refcnt = 1;
        removebuf(b);

        release(&bcache.bucket[i].lock);
        goto found;
      }
    }

    release(&bcache.bucket[i].lock);
  }

  panic("bget: no buffers");

found:
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;

  acquire(&bcache.bucket[index].lock);
  insertbuf(index, b);
  release(&bcache.bucket[index].lock);

  release(&bcache.evict_lock);

  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);

  if(!b->valid){
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }

  return b;
}

// Write b's contents to disk. Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");

  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  int index;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  index = hash(b->dev, b->blockno);

  acquire(&bcache.bucket[index].lock);

  if(b->refcnt < 1)
    panic("brelse: refcnt");

  b->refcnt--;

  release(&bcache.bucket[index].lock);
}

// Pin a buffer in the cache.
void
bpin(struct buf *b)
{
  int index = hash(b->dev, b->blockno);

  acquire(&bcache.bucket[index].lock);
  b->refcnt++;
  release(&bcache.bucket[index].lock);
}

// Unpin a buffer from the cache.
void
bunpin(struct buf *b)
{
  int index = hash(b->dev, b->blockno);

  acquire(&bcache.bucket[index].lock);

  if(b->refcnt < 1)
    panic("bunpin: refcnt");

  b->refcnt--;

  release(&bcache.bucket[index].lock);
}