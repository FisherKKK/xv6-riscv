#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

// 管道的数据结构
struct pipe {
  struct spinlock lock; // 读写对应的锁
  char data[PIPESIZE]; // 管道中的数据
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};


// 分配一个读写管道
int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  // 首先为读写分配一个文件, 因为基本all都被抽象成了file
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  
  // 为pipe单独分配一个页
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;
  // 设置这个pipe是否可读写
  pi->readopen = 1;
  pi->writeopen = 1;
  // pipe当前的读写偏置(数目)
  pi->nwrite = 0;
  pi->nread = 0;
  initlock(&pi->lock, "pipe");
  // 设置管道读文件
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  // 设置管道写文件
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

// 如果出现问题就清理现场
 bad:
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){
    pi->writeopen = 0;
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite);
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock);
    kfree((char*)pi);
  } else
    release(&pi->lock);
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(i < n){
    if(pi->readopen == 0 || killed(pr)){
      release(&pi->lock);
      return -1;
    }
    if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
      wakeup(&pi->nread);
      sleep(&pi->nwrite, &pi->lock);
    } else {
      char ch;
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
        break;
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);

  return i;
}


/**
 * 如果read的文件底层是一个pipe, 具体的逻辑如下:
 *  1. 获取pipe的读写锁
 *  2. 根据读写的偏置进行读取数据
 *  3. 将读取的数据返回到用户空间
 */
int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  // 首先获取管道对应的锁
  acquire(&pi->lock);

  // 这个相当于阻塞同步, 也就是没有数据可读的时候进入休眠并且释放锁
  // 采用while循环的原因是进程被唤醒后应该重新竞争锁
  while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
    // 如果当前进行已经被kill, 释放锁
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    // 进入休眠, 等待写数据
    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
  }

  // 当管道中存在可读数据的时候, 进行数据的读取
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    // 这一步相当于如果没有可读数据时跳出
    if(pi->nread == pi->nwrite)
      break;
    // 获取当前读偏置对应的数据
    ch = pi->data[pi->nread++ % PIPESIZE];
    // 将数据copy到用户空间
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1)
      break;
  }
  // 唤醒写等待写的进程
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  // 释放管道锁
  release(&pi->lock);
  return i;
}
