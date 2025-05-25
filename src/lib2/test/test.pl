#!/usr/bin/perl

@tests = (
   "../add.asm",
   "../bitwise.asm",
   "../cmp.asm",
   "../div.asm",
   "../incdec.asm",
   "../mul.asm",
   "../rem.asm",
   "../shift.asm",
   "../sub.asm",
);

$ca65 = "~/cc65/bin/ca65";
$ld65 = "~/cc65/bin/ld65";

foreach $file (@tests) {
	`cat test.asm $file > foo.asm`;
	`$ca65 foo.asm -o foo.o`;
	`$ld65 foo.o -C sim.cfg -o foo.bin`;
	./run6502.exp
}
