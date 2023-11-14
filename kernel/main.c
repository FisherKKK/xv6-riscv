#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
// start函数会跳转到这个位置, 同时从M Mode -> S mode
// 开始执行时, 虚拟内存等操作都还没有开始进行
// 中断也不会被处理, 也就是说现在万物都还没有开始
void
main()
{
  if(cpuid() == 0){
    // 如果这段代码只能在CPU0上进行执行
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator, 初始化物理页表分配器
    kvminit();       // create kernel page table, 创建内核页表
    kvminithart();   // turn on paging, 开启分页功能
    procinit();      // process table, 初始化进程表
    trapinit();      // trap vectors,  陷阱向量初始化
    trapinithart();  // install kernel trap vector, 为内核安装trap vector
    plicinit();      // set up interrupt controller, 设置平台中断控制器
    plicinithart();  // ask PLIC for device interrupts, 为每个CPU初始化plic
    binit();         // buffer cache, 初始化buf
    iinit();         // inode table, 初始化inode table
    fileinit();      // file table, 初始化文件表
    virtio_disk_init(); // emulated hard disk, 模拟硬盘
    userinit();      // first user process, 初始化第一个用户进程
    __sync_synchronize();
    started = 1;
  } else {
    // 其余CPU进行空转
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
