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

struct run {
  struct run *next;
};

struct {
  int ref_cnt[PHYSTOP / PGSIZE];
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kincref(void *pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP || kmem.ref_cnt[(uint64)pa / PGSIZE] <= 0)
    panic("increref");
  
  //ref count increment  
  acquire(&kmem.lock);
  kmem.ref_cnt[(uint64)pa / PGSIZE]++;
  release(&kmem.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // printf("here1\n");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  // printf("here2\n");
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    kmem.ref_cnt[(uint64)p / PGSIZE] = 1;//防止释放失败 
    kfree(p);
    // if((uint64)p>(uint64)pa_end/256*255)
    //   printf("%d: %d,%d\n", (uint64)p, (uint64)kmem.freelist, (uint64)kmem.freelist->next);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP||kmem.ref_cnt[(uint64)pa / PGSIZE]<=0)
    panic("kfree");

  acquire(&kmem.lock);
  kmem.ref_cnt[(uint64)pa / PGSIZE]--;
  
  if (kmem.ref_cnt[(uint64)pa / PGSIZE] == 0)
  {
    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
    // printf("%d\n", (uint64)kmem.freelist->next);
    release(&kmem.lock);
    // Fill with junk to catch dangling refs.开锁再清空，节省占用锁的时间
    memset(pa+sizeof(struct run*), 1, PGSIZE-sizeof(struct run*));
  }
  else
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  // printf("here%x\n",16843009);
  if(r)
  {
    // printf("here%d\n",(uint64)r);
    if(kmem.ref_cnt[(uint64)r / PGSIZE]){
      release(&kmem.lock);
      panic("kalloc");
    }
    kmem.ref_cnt[(uint64)r / PGSIZE]++;
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
