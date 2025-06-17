CA65=~/cc65/bin/ca65
CC65=~/cc65/bin/cc65
CL65=~/cc65/bin/cl65
LD65=~/cc65/bin/ld65
AR65=~/cc65/bin/ar65
MT=-t sim6502
SIM65= ~/cc65/bin/sim65

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do ~/cc65/bin/ca65 $$each; done
	$(AR65) a nlib.a wrk/*.o
	$(CL65) $(MT) -m wrk/out.map tests.c nlib.a
	$(SIM65) tests

clean:
	rm -f *.o *.bin tests nlib.a wrk/*

# ~/cc65/bin/cl65 -C n.cfg -m map.txt -u _nrt0_reset --target none -o nrt0.bin nrt0.s nlib_zeropage.s
