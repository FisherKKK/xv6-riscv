// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size, 文件系统中块的大小

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
// superblock描述了整个磁盘的布局
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks), 文件系统镜像的大小
  uint nblocks;      // Number of data blocks, 数据块的数目
  uint ninodes;      // Number of inodes, inode的数目
  uint nlog;         // Number of log blocks, log block的数目
  uint logstart;     // Block number of first log block, 第一个log block的块号
  uint inodestart;   // Block number of first inode block, 第一个inode block的块号
  uint bmapstart;    // Block number of first free map block, free map block的块号
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

// 目录是一个文件, 包含了一系列的dirent结构
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

