#!/usr/bin/perl

@tests = (
   "add.asm",
   "bitwise.asm",
   "cmp.asm",
   "div.asm",
   "incdec.asm",
   "mul.asm",
   "rem.asm",
   "shift.asm",
   "sub.asm",
);

$ca65 = "~/cc65/bin/ca65";
$ld65 = "~/cc65/bin/ld65";

$sed = "grep -v endproc | sed \"s/^[[:space:]]*\\.proc[[:space:]]\\+\\([a-zA-Z0-9_]\\+\\)/\\1:/\"";

foreach $file (@tests) {

   @proc = ();

   open FILE, "$file";
   foreach $line (<FILE>) {
      if ($line =~ /\.proc/) {
         $tmp = $line;
         $tmp =~ s/[\x0a\x0d]//g;
         $tmp =~ s/^[\s]*\.proc[\s]+//g;
         $tmp =~ s/[\s]*//g;
         push @proc, $tmp;
      }
   }
   close FILE;

   foreach $proc (@proc) {
      if ($proc eq "shiftN") {
         next;
      }

      print "=== $file $proc\n";

#      print "cat test/test.asm | sed \"s/TARGET/$proc/g\" > foo.asm\n";
      print `cat test/test.asm  | sed "s/TARGET/$proc/g" > foo.asm`;

      print "$ca65 foo.asm -o foo.o\n";
      print `$ca65 foo.asm -o foo.o`;

      print "$ld65 foo.o nlib.a -C test/sim.cfg -o foo.bin\n";
      print `$ld65 foo.o nlib.a -C test/sim.cfg -o foo.bin`;

      open FILE, ">script.txt";
      print FILE "width 72\n";
      print FILE "load foo.bin 0x8000\n";
      print FILE "registers pc=0x8000\n";
      print FILE "disassemble 8000:8040\n";
      print FILE "add_breakpoint 8036\n";
      print FILE "mem 8039:804a\n";
      print FILE "goto 8000\n";
      print FILE "mem 8039:804a\n";
      close FILE;

      print `py65mon --mpu 6502 < script.txt | grep 8039`;

#      if ($file eq "cmp.asm") {
#         exit 0;
#      }
#      exit 0;
   }
}
