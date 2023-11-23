# 文件所在的为止, 分为内核文件和用户文件
K=kernel
U=user

# 所有需要参与编译的Object文件
# $相当于取值操作, 这里是所有的内核目标文件
OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
# 在这一步相当于找对RISV64对应的编译工具
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# 设置对应的模拟器为QEMU
QEMU = qemu-system-riscv64

# 设置编译器, 汇编器, 链接器
CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# 设置编译器对应的Flag
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# 链接器对应的Flag
LDFLAGS = -z max-page-size=4096

# 所有的内核文件, 编译形成内核
# LD相当于连接成对应的内核文件kernel
# objdump -S相当于生成内核的汇编代码
# objdump -S相当于生成符号表信息
$K/kernel: $(OBJS) $K/kernel.ld $U/initcode
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS)
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

# 编译生成用户空间的代码, initcode从汇编代码生成
# -I表示查找头文件的位置, 也就是说initcode中存在部分头文件来自内核
# 同样生成对应的汇编文件
$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm


tags: $(OBJS) _init
	etags *.S *.c

# 构建的User Lib
ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

# %表示通配符, 将ULIB和所有的.o文件构建成_开头的可执行文件
# @表示文件名, ^表示所有依赖项
# 所以这段代码的本质含义是生成RISCV下的可执行文件
# gcc编译生成对应的.o文件
# 真正linker进行链接的时候还需要借助user.ld下的linker script才能真正构成可执行文件
_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

# 生成对应的usys.S汇编文件
$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

# 从汇编生成对应的.o文件
$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

# 生成用户空间下的forktest
$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

# 生成fs
mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
# 保存生成的.o文件
.PRECIOUS: %.o

# 用户空间下的程序
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\

# 文件系统的镜像
#? 这里可能是将文件拷贝进文件系统
fs.img: mkfs/mkfs README $(UPROGS)
	mkfs/mkfs fs.img README $(UPROGS)

# 表示需要自动识别相关的依赖关系
-include kernel/*.d user/*.d

# clean命令对应的操作
clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$U/initcode $U/initcode.out $K/kernel fs.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

# try to generate a unique GDB port
# GDB的端口
GDBPORT = $(shell expr `id -u` % 5000 + 25000)

# QEMU's gdb stub command line changed in 0.11
# qemu的gdb命令
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)

# 定义默认CPU的数目
ifndef CPUS
CPUS := 3
endif

# QEMU对应的FLAGS
# 将kernel/kernel作为内核文件
# 挂载fs.img作为文件驱动
QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# qemu对应的命令
qemu: $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

# 生成对应的GDB调试命令
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# qemu的调试命令
qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

