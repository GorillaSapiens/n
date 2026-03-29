ASM=../../assembler/n65asm
AR=../../archiver/n65ar

all:
	$(ASM) -o nrt0_int.o65 nrt0_int.s
	$(AR) rcs nint.a65 nrt0_int.o65

clean:
	rm -f nint.a65
