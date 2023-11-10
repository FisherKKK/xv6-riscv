#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

// 汇编代码将会调用这个start函数
void main();
void timerinit();

// entry.S needs one stack per CPU.
// 每个CPU对应一个stack, 每个CPU占4096个字节, 16字节对齐
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// a scratch area per CPU for machine-mode timer interrupts.
// 每一个CPU中Timer中断对应的区域
// scratch[0..2] : space for timervec to save registers.
// scratch[3] : address of CLINT MTIMECMP register.
// scratch[4] : desired interval (in cycles) between timer interrupts.
uint64 timer_scratch[NCPU][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
// 也就是说这段代码在stack0上执行, 采用的是机器模式
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  // 这时候机器还处理Machine Mode, mret将会返回到Supervisor Mode
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK; // 清除寄存器之前的Mode
  x |= MSTATUS_MPP_S; // 将S Mode写入寄存器
  w_mstatus(x); // 修改对应的寄存器状态

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  // 设置M的PC指针为PC, 也就是说mret之后就会执行main
  w_mepc((uint64)main);

  // disable paging for now.
  // 通过设置0, 禁用了分页功能
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  // 将M模式下所有的中断和异常都委托给S模式
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  // 配置物理内存的保护机制, 从而使得Smode可以访问所有的物理内存
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  // 保存每个CPU的hartid到其tp寄存器
  int id = r_mhartid();
  w_tp(id);

  // 通过mret切换到S mode并跳转到main函数
  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// arrange to receive timer interrupts.
// they will arrive in machine mode at
// at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
// 接受计时器中断, 将M模式下到达的计时器中断由timervec转换为软中断, 然后被usertrap处理
void
timerinit()
{
  // each CPU has a separate source of timer interrupts.
  // 获取CPU的id
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu. 定时器的中断周期
  // 也就是说timer中断的触发机制采用的时比较值, 当前值 + Interval
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  // 设置机器模式下的trap处理为timervec
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  // 开启M mode下的中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  // 开始计时器中断
  w_mie(r_mie() | MIE_MTIE);
}
