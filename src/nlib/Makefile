CA65=~/cc65/bin/ca65
LD65=~/cc65/bin/ld65

all:
	for each in `ls *.asm`; do ~/cc65/bin/ca65 $$each; done

clean:
	rm -f *.o *.bin foo.*

debug:
	cat test.asm div.asm > foo.asm
	$(CA65) foo.asm -o foo.o
	$(LD65) foo.o -C sim.cfg -o foo.bin
	./run6502.exp
