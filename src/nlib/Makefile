CA65=~/cc65/bin/ca65
CC65=~/cc65/bin/cc65
CL65=~/cc65/bin/cl65
LD65=~/cc65/bin/ld65
AR65=~/cc65/bin/ar65
MT=-t sim6502
#SIM65= ~/cc65/bin/sim65 -x 2000000000
SIM65= ~/cc65/bin/sim65


all:
	rm -rf split nlib.a
	mkdir split
	./unbundle.pl
	cp *.s split
	for each in `ls split/*.s`; do ~/cc65/bin/ca65 $$each; done
	$(AR65) a nlib.a split/*.o
	$(CL65) $(MT) -m out.map tests.c nlib.a
	$(SIM65) tests

clean:
	rm -f *.o *.bin tests split/*

