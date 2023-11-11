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

// end是内核加载完成之后的首个地址
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 初始化内存
void
kinit()
{
  // 初始化memory lock
  initlock(&kmem.lock, "kmem"); 
  // 初始化所有从end -> PHYSTOP的页
  // 这些都是实际的物理内存
  freerange(end, (void*)PHYSTOP);
}

// 释放过程很简单:
// 将所有的页看作是run结构体
// 进行链表式连接即可
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 因为一个页的大小是4096, 这里首先向上对齐
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 释放内存页, 本质上就是将这个页放在
// 链表中
void
kfree(void *pa)
{
  struct run *r;
  // 首先检查是否对齐和地址范围
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 初始化是为了快速捕捉dangling 引用问题
  memset(pa, 1, PGSIZE);

  // 转换为run结构体致函
  r = (struct run*)pa;

  acquire(&kmem.lock);
  // 链表进行连接, 这里首先需要获得锁, 因为
  // 可能多个CPU同时free
  r->next = kmem.freelist;
  kmem.freelist = r;
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
