
all: dumpbootbin_run xbootgen_run

dumpbootbin_run:
	gcc -g -o dumpbootbin dumpbootbin.c

xbootgen_run:
	arm-none-linux-gnueabi-gcc -c reserved_for_interrupts.S
	arm-none-linux-gnueabi-ld -Ttext-segment 0 -e 0 -o c.tmp reserved_for_interrupts.o
	objcopy -O binary -I elf32-little c.tmp d.tmp
	gcc -g -o xbootgen xbootgen.c
