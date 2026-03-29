ASM=../../assembler/n65asm
AR=../../archiver/n65ar

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do stem=`basename $$each .s`; echo ===== $$each ; $(ASM) -o wrk/$$stem.o65 $$each; done
	$(AR) rcs nlib.a65 wrk/*.o65

clean:
	rm -f *.o *.bin tests nlib.a65 wrk/*
