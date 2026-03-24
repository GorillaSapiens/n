#CA65=~/cc65x/bin/ca65
#CC65=~/cc65x/bin/cc65
#CL65=~/cc65x/bin/cl65
#LD65=~/cc65x/bin/ld65
#AR65=~/cc65x/bin/ar65
#MT=-t sim6502
#SIM65= ~/cc65x/bin/sim65

ASM=~/n/src/assembler/na
AR=~/n/src/archiver/nar

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do echo ===== $$each ; $(ASM) -i $$each --o65; done
	$(AR) -c nlib.a wrk/*.o65
#	$(CL65) $(MT) -m wrk/out.map tests.c nlib.a
#	$(SIM65) tests

clean:
	rm -f *.o *.bin tests nlib.a wrk/*
