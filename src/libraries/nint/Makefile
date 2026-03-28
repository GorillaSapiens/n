ASM=../../assembler/na
AR=../../archiver/nar

all:
	$(ASM) -i nrt0_int.s --o65
	$(AR) -c nint.a65 nrt0_int.o65

clean:
	rm -f nint.a65
