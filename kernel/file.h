struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type; // 文件的类型
  int ref; // reference count, 文件的引用数目
  char readable; // 描述文件的读写性质
  char writable;
  struct pipe *pipe; // FD_PIPE, 根据文件的类型设置其对应的底层数据结构
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// map major device number to device functions.
// 每个设备对应的RW函数, 如果定义一个数组, 通过设备号索引
// 那么就能得到每个设备的RW函数
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
