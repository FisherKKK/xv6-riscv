#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

// 定义inode节点数目为200
#define NINODES 200

// Disk layout:
// 整个磁盘的布局如下:
// [磁盘启动块 | file元信息 | 日志 | inode | free block | real data block]
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

// 这里的计算方式就是data block / (一块的位数), 也就是计算nbitmap需要多少块
int nbitmap = FSSIZE/(BSIZE*8) + 1;
// 这里计算inode的块数: 总共需要inode个数 / 每个块中可以容纳inode的数目
int ninodeblocks = NINODES / IPB + 1;
// 日志块的数目
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap), 总共元数据的数目
int nblocks;  // Number of data blocks, 数据块的数目

int fsfd;
struct superblock sb; // super block描述整个文件系统的
char zeroes[BSIZE]; // 全0
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);

// convert to riscv byte order
// 转换为riscv的字节顺序
//? 这里本质就是把x转换为小端法表示
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  // 文件系统的fd:
  // O_RDWR以读写方式打开
  // O_CREATE 不存在就创建
  // O_TRUNC 存在就截断
  // 0666表示所有用户均有权限读写
  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  // 1 fs block = 1 disk sector
  // 元数据的块数 = 1boot + 1super + nlog + ninode + nbitmap
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  // 数据块的数目 = 文件系统总块数 - 元数据块数
  nblocks = FSSIZE - nmeta;

  // 文件系统的magic数字
  sb.magic = FSMAGIC;
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2); // 处于第二块
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  // 写sector, 对所有的数据块都写0
  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  // 写buf为0
  memset(buf, 0, sizeof(buf));
  // 将sb的内容移入到buf中, memmove的优点在于防止两块内存重叠
  memmove(buf, &sb, sizeof(sb));
  // 将buf内容写入sect 1
  wsect(1, buf);

  // 根inode, 它是一个目录
  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  // 清空目录
  bzero(&de, sizeof(de));
  // 设置de的inum
  de.inum = xshort(rootino);
  // 设置de的name
  strcpy(de.name, ".");
  //TODO: Not yet learn
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
    // get rid of "user/"
    char *shortname;
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    assert(index(shortname, '/') == 0);

    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(shortname[0] == '_')
      shortname += 1;

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}


// 
void
wsect(uint sec, void *buf)
{
  // 改变文件描述符的偏置, 从0 -> sec * BSIZE
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  // 写0数组到对应的位置
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}


// inum是inode对应的号码, ip是dinode对应的地址
void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  // inum对应的inode所在的磁盘块
  bn = IBLOCK(inum, sb);
  // 读取对应的块
  rsect(bn, buf);
  // 找到ip在块中的偏置
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip; // 放到buf中
  wsect(bn, buf); // 写入磁盘块
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}


uint
ialloc(ushort type)
{
  // 记录当前分配inode的数目, 这里可以理解成当前inode的id
  uint inum = freeinode++;
  struct dinode din;
  // 将din的内容清0
  bzero(&din, sizeof(din));
  // 设置inode类型
  din.type = xshort(type);
  // 设置inode的link数目
  din.nlink = xshort(1);
  // inode的大小
  din.size = xint(0);
  // 将内存中的din写入磁盘
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}

// die相当于这里的panic
void
die(const char *s)
{
  perror(s);
  exit(1);
}
