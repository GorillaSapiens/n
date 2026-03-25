ASM=~/n/src/assembler/na
AR=~/n/src/archiver/nar

all:
	$(ASM) -i nrt0_int.s --o65
	$(AR) -c nint.a nrt0_int.o65

clean:
	rm -f nint.a
