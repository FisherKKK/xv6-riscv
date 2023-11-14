#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // 进行PLIC的初始化
  // set desired IRQ priorities non-zero (otherwise disabled).
  // 设置值中断的优先级
  // IRQ类似于中断号, 标识对应的中断
  // 因此可以将PLIC理解成一个中断向量表
  // 4的原因是这是一个32位的数组
  // 这里本质上就是开启UART和VIRTIO中断
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}


// 初始化每一个hart用来处理PLIC的方式
void
plicinithart(void)
{
  int hart = cpuid();
  
  // set enable bits for this hart's S-mode
  // for the uart and virtio disk.
  // 设置当前CPU的S mode下的接收UART和VIRTIO中断
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  // 设置当前CPU S模式下的优先级阈值为0
  // 也就是说只要有中断出现, 立即执行, 不考虑优先级
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
