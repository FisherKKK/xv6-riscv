//
// low-level driver routines for 16550a UART.
// UART的全称是Universal Asynchronous Receiver/Transmitter通用异步收发器
// 是一种硬件通信协议, 用于设备间数据传输
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
// UART的寄存器是被映射到内存地址, 从UART0开始
// 这个宏命令就是返回对应的寄存器
#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes), 输入寄存器
#define THR 0                 // transmit holding register (for output bytes), 输出寄存器
#define IER 1                 // interrupt enable register, 中断开启寄存器
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register, FIFO队列寄存器, 因为数据会被首先缓存在FIFO
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs, 每次发送数据之前需要清除FIFO的内容
#define ISR 2                 // interrupt status register, 中断状态寄存器
#define LCR 3                 // line control register, line控制寄存器
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate, 设置数据的传输速率
#define LSR 5                 // line status register, 线状态
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR, 准备读操作, 也就是可以开始读了
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send, 已经可以发送字符了

#define ReadReg(reg) (*(Reg(reg))) // 读寄存器对应的值
#define WriteReg(reg, v) (*(Reg(reg)) = (v)) // 向寄存器写入值

// the transmit output buffer.
struct spinlock uart_tx_lock; // 硬件设备锁, 保证传输持有锁, 防止数据混乱
#define UART_TX_BUF_SIZE 32 // 传输的缓冲区大小
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE], 写指针
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE], 读指针

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  // 关中断
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  // 通过线控寄存器告诉设备要开始设置数据传输速率
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 设置数据的传输速率为38.4, 根据除法计算而来
  // LSB for baud rate of 38.4K.
  // 最低有效位
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  // 最高有效位
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  // 设置数据位模式为8位, 无奇偶校验
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  // 重置并开启FIFO队列
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  // 开启读写中断
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  // 初始化uart设备锁
  initlock(&uart_tx_lock, "uart");
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void
uartintr(void)
{
  // read and process incoming characters.
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
