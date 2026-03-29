ASM=../../assembler/na
AR=../../archiver/nar

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do echo ===== $$each ; $(ASM) -i $$each --o65; done
	$(AR) -c nlib.a65 wrk/*.o65

clean:
	rm -f *.o *.bin tests nlib.a65 wrk/*
