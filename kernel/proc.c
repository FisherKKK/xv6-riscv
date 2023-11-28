#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

// 这里预先定义了所有的进程
struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
//? wait系统调用需要首先获取的锁
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
// 为每一个进程映射一个内核栈
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc)); // 内核栈的虚拟地址
    // 为内核页表映射KSTACK和pa
    // 这里相当于为每个进程分配了一个内核栈, 这里User无法进行访问
    // 只能内核进行处理
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
// 初始化所有的进程
void
procinit(void)
{
  struct proc *p;
  
  //? 这两个锁的机制:
  //? nextpid锁保证获取下一个pid原子性
  //? 
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc"); // 初始化进程锁
      p->state = UNUSED; // 设置状态为Unused
      p->kstack = KSTACK((int) (p - proc)); // 设置当前进程的内核栈
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
// 为了防止竞争出现, 需要关中断
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
// 获取当前的CPU, 必须要关闭中断
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
// 获取当前CPU正在运行的进程
struct proc*
myproc(void)
{
  push_off(); // 关中断获取CPU和对应的进程
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// 分配pid时需要保证原子性
int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
// 通过循环在进程表中寻找一个空闲的进程
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  // 如果找到了, 分配新的pid
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  // 分配一个陷阱页
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  // 为进程创建一个页表, 并且映射trampoline和trapframe
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // 设置进程的上下文
  memset(&p->context, 0, sizeof(p->context));
  // ra寄存器实际上是保存当前函数执行完成之后
  // ret之后会运行的地址
  p->context.ra = (uint64)forkret;
  // 设置栈顶的地址, 这里的设置是因为RISCV是grow down
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
// 为用户进程创建一个页表, 没有额外的内存空间
// 但是保留了trampoline和trapframe
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  // 映射TRAPOLINE: 
  // TRAMPOLINE是虚拟地址
  // trampoline对应的是实际物理地址, 在汇编中定义
  // 每个进程都会映射TRAMPOLINE作为最高的内存地址
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  // 映射TRAPFRAME -> pa
  // 这个本质就是映射陷阱页
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc(); // 分配一个空闲的进程
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  // 分配一个物理页, 然后将initcode中的内容保存到其中
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE; // 当前p的size就是PGSIZE

  // prepare for the very first "return" from kernel to user.
  // 设置当前的PC指针为0, 也就是initcode对应的虚拟地址
  p->trapframe->epc = 0;      // user program counter
  // 设置栈指针为PGSIZE
  p->trapframe->sp = PGSIZE;  // user stack pointer

  // 拷贝进程的名称
  safestrcpy(p->name, "initcode", sizeof(p->name));
  // 设置进程的工作目录为"/"根目录
  p->cwd = namei("/");

  // 设置进程为RUNNABLE, 等待被调度
  p->state = RUNNABLE;

  // 释放进程锁
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
// 创建一个新的进程, 父子进程之间所有的内容均相同, 但是父子进程的返回值并不相同
// 所以大体的思路就是:
// 1. 找到一个空闲的进程
// 2. 通过当前进程的pid将所有的内容进行deepcopy(因此在这里可以实现cow)
// 3. 那么如何实现父子进程的return value不同呢? 首先我们知道返回值保存在哪个寄存器, 我们可以通过设置process的context来保证这一点
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  // 首先分配一个子进程
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  // 将父进程页表中的所有内容 --> 子进程中
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  // 设置页表的大小
  np->sz = p->sz;

  // copy saved user registers.
  // 复制所有的父进程上下文
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  // 设置子进程的返回值为0, 通过设置a0寄存器
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  // 复制所有父进程中的OPEN_FILE引用(可以理解成是增加IO计数)
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  // 复制父进程的名称
  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  // 设置子进程的父进程
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  // 设置当前子进程进程的状态
  np->state = RUNNABLE;
  release(&np->lock);
  // 当前函数的返回值就是子进程的id
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
// 进程p放弃称为父亲, 那么就把它的所有的child寄养到init下
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      // TODO: Not yet analyze
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
// exit当前的进程, 永远不会返回, 一个exit的进程会进入zombie状态
// 直到它的父进程调用wait才会进行释放
void
exit(int status)
{
  struct proc *p = myproc();

  // init进程无法进行exit
  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  // 关闭所有打开的文件, 底层是关闭文件的引用计数
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  // 这里相当于减小inode引用计数
  iput(p->cwd);
  end_op();
  // 关闭p的工作目录
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  // 改变其所有子进程的parent -> init
  reparent(p);

  // Parent might be sleeping in wait().
  // TODO: 唤醒p的父进程
  wakeup(p->parent);
  
  acquire(&p->lock);

  // 设置退出状态
  p->xstate = status;
  // 设置为ZOMBIE
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  // 进入调度scheduler, 进程不会return也不会继续运行
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      // 如果存在子进程
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        // 尝试获取进程锁, 这里可以保证子进程持有锁
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          // 如果子进程已经进入了ZOMBIE状态, 可以将它的xstate -> 用户空间的对应地址
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// 每个CPU都会执行当前的调度函数
// Never Return, 在循环中它的操作如下:
// - 选择一个进程运行
// - 进行上下文切换
// - 控制权交换
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  // 重置当前CPU上正在运行的进程
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    // 开中断
    intr_on();

    // 寻找一个可以被调度的进程
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock); // 保证原子操作
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        // 进程需要自己释放刚刚被设置的锁
        // 交换完成之后自己再获得锁
        p->state = RUNNING;
        c->proc = p;
        // 进行上下文交换, 如果是新创建的进程会直接跳转到
        // 内核中的forkret
        //! 到这一步为止: 线程的context中stack指向内核栈, epc指向forkret
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
// 在这里需要保存开关中断的信息, 因此这个属性属于某个kernel thread
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
// 被创建的进程第一次会来到的位置
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  // 释放当前进程的锁
  release(&myproc()->lock);

  if (first) {
    // 如果是第一个运行的进程必须进行文件系统的初始化
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    //TODO: 初始化文件系统
    fsinit(ROOTDEV);
  }

  //TODO: 返回用户空间
  // 从内核空间返回用户空间
  usertrapret();
}

/**
 * 因此sleep和wakeup的整体逻辑是, 线程等待一个变量chan的过程中释放锁进入休眠状态
 * 接着唤醒进程采用更新chan对应的值之后, wakeup所有相关的进程, 让他们重复判断
 * 当前的状态是否满足进程的处理逻辑
*/

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// 自动释放锁, 进入休眠同时等待被唤醒
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  // 获取进程对应的锁, 从而改变进程的状态
  acquire(&p->lock);  //DOC: sleeplock1
  // 释放等待的锁
  release(lk);

  // Go to sleep.
  // 进入休眠状态
  p->chan = chan;
  p->state = SLEEPING;

  // 进行调度
  sched();

  // Tidy up.
  // 这里相当于被唤醒之后
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
// wakeup在chan上休眠的进程
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      // 相当于唤醒休眠的进程
      if(p->state == SLEEPING && p->chan == chan) {
        // 让这个线程可以重新进行调度
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
