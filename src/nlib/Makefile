CA65=~/cc65/bin/ca65
CC65=~/cc65/bin/cc65
CL65=~/cc65/bin/cl65
LD65=~/cc65/bin/ld65
AR65=~/cc65/bin/ar65
MT=-t sim6502
SIM65= ~/cc65/bin/sim65 -x 2000000


all:
	./unbundle.pl
	cp *.s split
	for each in `ls split/*.s`; do ~/cc65/bin/ca65 $$each; done
	$(AR65) a nlib.a split/*.o
	$(CL65) $(MT) foo.c split/*.o
	$(SIM65) foo

clean:
	rm -f *.o *.bin foo.* split/*

debug:
	cat test.asm div.asm > foo.asm
	$(CA65) foo.asm -o foo.o
	$(LD65) foo.o -C sim.cfg -o foo.bin
	./run6502.exp
