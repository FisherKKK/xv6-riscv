// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

/**
 * 休眠锁的本质就是:
 *  如果当前获取的锁仍然被locked, 那么进入休眠
 *  等待被唤醒, 唤醒时重新获取锁
*/
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    // 进行休眠, 基于锁lk
    sleep(lk, &lk->lk);
  }
  // 设置锁
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}

// 唤醒休眠锁
void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}



