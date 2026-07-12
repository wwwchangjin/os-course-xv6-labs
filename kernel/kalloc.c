// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// 空闲物理页链表中的节点。
// 每一个空闲物理页开头都被当作一个 struct run 使用。
struct run {
  struct run *next;
};

// 为每个 CPU 分别设置一把锁和一个空闲页链表。
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

// 从其他 CPU 的空闲链表中偷取一部分页面。
// 调用该函数时，中断应当已经关闭。
static struct run *
steal(int id)
{
  // 从当前 CPU 的下一个 CPU 开始依次查找。
  for(int offset = 1; offset < NCPU; offset++){
    int victim = (id + offset) % NCPU;

    acquire(&kmem[victim].lock);

    // 统计目标 CPU 当前拥有多少个空闲页。
    int count = 0;
    struct run *p;
    for(p = kmem[victim].freelist; p != 0; p = p->next)
      count++;

    if(count > 0){
      // 偷取约一半页面。
      // 只有一个页面时，也至少偷取一个。
      int take = (count + 1) / 2;

      struct run *first = kmem[victim].freelist;
      struct run *last = first;

      for(int i = 1; i < take; i++)
        last = last->next;

      // 从目标 CPU 的链表中摘下这段页面。
      kmem[victim].freelist = last->next;
      last->next = 0;

      release(&kmem[victim].lock);

      // 将偷来的第一个页面直接返回给本次 kalloc。
      struct run *r = first;
      struct run *rest = r->next;
      r->next = 0;

      // 剩余偷来的页面放入当前 CPU 的空闲链表。
      if(rest != 0){
        struct run *rest_last = rest;

        while(rest_last->next != 0)
          rest_last = rest_last->next;

        acquire(&kmem[id].lock);
        rest_last->next = kmem[id].freelist;
        kmem[id].freelist = rest;
        release(&kmem[id].lock);
      }

      return r;
    }

    release(&kmem[victim].lock);
  }

  // 所有 CPU 都没有空闲页。
  return 0;
}

void
kinit()
{
  // 初始化每个 CPU 对应的锁和空闲链表。
  for(int i = 0; i < NCPU; i++){
    // 官方测试要求这些锁的名称以 kmem 开头。
    initlock(&kmem[i].lock, "kmem");
    kmem[i].freelist = 0;
  }

  // freerange 调用 kfree，把所有初始空闲页交给
  // 当前执行 kinit 的 CPU。
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);

  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  The exception is initialization
// through freerange().
void
kfree(void *pa)
{
  struct run *r;
  int id;

  if(((uint64)pa % PGSIZE) != 0 ||
     (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 填充垃圾数据，用于尽早发现悬空引用。
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // cpuid() 只有在中断关闭时才能安全使用。
  // 否则获取 CPU 编号后，进程可能被调度到其他 CPU。
  push_off();
  id = cpuid();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id;

  // 在获取并使用当前 CPU 编号期间关闭中断。
  push_off();
  id = cpuid();

  // 首先从当前 CPU 自己的空闲链表分配。
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;

  if(r != 0)
    kmem[id].freelist = r->next;

  release(&kmem[id].lock);

  // 当前 CPU 没有空闲页时，从其他 CPU 偷取。
  if(r == 0)
    r = steal(id);

  pop_off();

  if(r != 0)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}