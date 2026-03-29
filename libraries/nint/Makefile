ASM=../../assembler/n65asm
AR=../../archiver/n65ar

all:
	$(ASM) -i nrt0_int.s --o65
	$(AR) rcs nint.a65 nrt0_int.o65

clean:
	rm -f nint.a65
