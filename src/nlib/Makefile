CA65=~/cc65x/bin/ca65
CC65=~/cc65x/bin/cc65
CL65=~/cc65x/bin/cl65
LD65=~/cc65x/bin/ld65
AR65=~/cc65x/bin/ar65
MT=-t sim6502
SIM65= ~/cc65x/bin/sim65

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do $(CA65) $$each; done
	$(AR65) a nlib.a wrk/*.o
	$(CL65) $(MT) -m wrk/out.map tests.c nlib.a
	$(SIM65) tests

clean:
	rm -f *.o *.bin tests nlib.a wrk/*
