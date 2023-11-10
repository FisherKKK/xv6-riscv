        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
        # 这是因为linker的脚本设置了入口为_entry
        # .global定义了全局可见的entry
        # .text表示可执行代码段
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096), 也就是说每个CPU对应的内存中的栈
        # la指令设置sp寄存器的值为stack0
        la sp, stack0
        # li设置a0寄存器的值为4096
        li a0, 1024*4
        # csrr用于从寄存器中读值并且赋值, 即将mhartid -> a1
        csrr a1, mhartid
        # a1 + 1 -> a1, 这样设置的原因是RISC-V中栈grow down
        addi a1, a1, 1
        # a0 * a1 -> a0
        mul a0, a0, a1
        # sp + a0 -> sp
        add sp, sp, a0
        # jump to start() in start.c
        call start
spin:
        j spin
