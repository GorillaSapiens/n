CA65=~/cc65/bin/ca65
LD65=~/cc65/bin/ld65
AR65=~/cc65/bin/ar65

all:
	for each in `ls split/*.s`; do ~/cc65/bin/ca65 $$each; done
	$(AR65) a nlib.a split/*.o

clean:
	rm -f *.o *.bin foo.*

debug:
	cat test.asm div.asm > foo.asm
	$(CA65) foo.asm -o foo.o
	$(LD65) foo.o -C sim.cfg -o foo.bin
	./run6502.exp
