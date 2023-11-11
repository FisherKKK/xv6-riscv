/* 这里作为linker的脚本文件, 指定了整个程序的入口为_entry */
OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   * .表示程序被加载的位置, 这里固定在0x80000
   */
  . = 0x80000000;

  /*
   * text段用于存储可执行指令和数据
   * .text .text.*表示通配所有的这样段
   * .表示0x1000地址4096对齐
   * trampoline等于当前对齐的地址
   * trampsec表示将这个section放在其中
   */
  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  /*
   * rodata段用于存储只读数据
   */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  /*
   * data段用于存储可读写且需要初始化的数据
   */
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  /*
   * bss段用于存储可读写的初始化数据(清零)
   */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .); /* 在这个地方定义了end的地址, 也就是内核加载完成之后的地址 */
}
