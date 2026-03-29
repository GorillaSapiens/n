ASM=../../assembler/n65asm
AR=../../archiver/n65ar

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do echo ===== $$each ; $(ASM) -i $$each --o65; done
	$(AR) rcs nlib.a65 wrk/*.o65

clean:
	rm -f *.o *.bin tests nlib.a65 wrk/*
