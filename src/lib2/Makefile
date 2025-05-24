CA65=~/cc65/bin/ca65
LD65=~/cc65/bin/ld65

all:
	for each in `ls *.asm`; do ~/cc65/bin/ca65 $$each; done

debug:
	$(CA65) mul.asm -o mul.o
	$(LD65) mul.o -C sim.cfg -o mul.bin

