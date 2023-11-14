        #
        # interrupts and exceptions while in supervisor
        # mode come here.
        #
        # the current stack is a kernel stack.
        # push all registers, call kerneltrap().
        # when kerneltrap() returns, restore registers, return.
        #
        # S模式下的中断和异常会到这个函数, 当前处的位置是内核栈
        # 这里本质上因为寄存器设置的地址为kernelvec, 因此会调用这个函数
.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # make room to save registers.
        # 为保留寄存器腾出空间
        addi sp, sp, -256

        # save the registers.
        # 将寄存器存入栈中
        sd ra, 0(sp)
        sd sp, 8(sp)
        sd gp, 16(sp)
        sd tp, 24(sp)
        sd t0, 32(sp)
        sd t1, 40(sp)
        sd t2, 48(sp)
        sd s0, 56(sp)
        sd s1, 64(sp)
        sd a0, 72(sp)
        sd a1, 80(sp)
        sd a2, 88(sp)
        sd a3, 96(sp)
        sd a4, 104(sp)
        sd a5, 112(sp)
        sd a6, 120(sp)
        sd a7, 128(sp)
        sd s2, 136(sp)
        sd s3, 144(sp)
        sd s4, 152(sp)
        sd s5, 160(sp)
        sd s6, 168(sp)
        sd s7, 176(sp)
        sd s8, 184(sp)
        sd s9, 192(sp)
        sd s10, 200(sp)
        sd s11, 208(sp)
        sd t3, 216(sp)
        sd t4, 224(sp)
        sd t5, 232(sp)
        sd t6, 240(sp)

        # call the C trap handler in trap.c
        # 调用c函数
        call kerneltrap

        # restore registers.
        # 从内核栈中回复寄存器的值
        ld ra, 0(sp)
        ld sp, 8(sp)
        ld gp, 16(sp)
        # not tp (contains hartid), in case we moved CPUs
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld s0, 56(sp)
        ld s1, 64(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld s2, 136(sp)
        ld s3, 144(sp)
        ld s4, 152(sp)
        ld s5, 160(sp)
        ld s6, 168(sp)
        ld s7, 176(sp)
        ld s8, 184(sp)
        ld s9, 192(sp)
        ld s10, 200(sp)
        ld s11, 208(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        # 还原栈地址
        addi sp, sp, 256

        # return to whatever we were doing in the kernel.
        sret

        #
        # machine-mode timer interrupt.
        # 机器模式下Timer中断的处理函数
        #
.globl timervec
.align 4
timervec:
        # start.c has set up the memory that mscratch points to:
        # 64个字节为单位的数组, 0-8 8-16 16-24是保留寄存器区域
        # 24-32是CMP寄存器对应的位置
        # 32-40是inter对应的位置
        # scratch[0,8,16] : register save area.
        # scratch[24] : address of CLINT's MTIMECMP register.
        # scratch[32] : desired interval between interrupts.
        
        # 交换a0和mscratch中的值
        csrrw a0, mscratch, a0
        # 如下就是保留寄存器的值:
        # a1 -> a0 + 0
        sd a1, 0(a0)
        # a2 -> a0 + 8
        sd a2, 8(a0)
        # a3 -> a0 + 16
        sd a3, 16(a0)

        # schedule the next timer interrupt
        # by adding interval to mtimecmp.
        # a0 + 24 -> a1 (a1 = CMP对应的值)
        ld a1, 24(a0) # CLINT_MTIMECMP(hart)
        # a0 + 32 -> a2 (a2 = 中断间隔)
        ld a2, 32(a0) # interval
        # a1 + 0 -> a3  (a3 = CMP)
        ld a3, 0(a1)
        # a3 + a2 -> a3 (a3 = CMP + Interval)
        add a3, a3, a2
        # a3 -> a1 + 0 (a1 = CMP + Interval = CMP)
        # 也就是说这一步更新了Timer下一次的时间
        sd a3, 0(a1)

        # arrange for a supervisor software interrupt
        # after this handler returns.
        # 将2 -> a1, 相当于返回值, 现在编程软中断
        li a1, 2
        csrw sip, a1

        # a0 + 16 -> a3
        # a0 + 8  -> a2
        # a0 + 0  -> a1
        ld a3, 16(a0)
        ld a2, 8(a0)
        ld a1, 0(a0)
        # a0 <--> mscratch
        csrrw a0, mscratch, a0
        # 从M模式返回到S模式
        mret
