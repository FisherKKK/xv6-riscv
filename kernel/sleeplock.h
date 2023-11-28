// Long-term locks for processes
// 相当于这个Sleep Lock就是原本Lock的一个封装
// 
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};

