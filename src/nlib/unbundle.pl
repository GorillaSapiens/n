#!/usr/bin/perl

`mkdir -p split`;
`cp nlib.inc split`;
foreach $file (`ls *.asm`) {
   $file =~ s/[\x0a\x0d]//g;

   if ($file eq "test.asm" || $file eq "foo.asm") {
      continue;
   }

   print "== $file\n";
   open FILE, $file;
   @file = <FILE>;
   close FILE;

   @head = ();
   @constants = ();
   $mode = 0;
   foreach $line (@file) {

      if ($line =~ /^[^;]*=/) {
         push @constants, $line;
      }

      if ($line =~ /^[a-zA-Z0-9_]+:/) {
         print "WARN: $line";
      }

      if ($line =~ /^\.include/) {
         $mode = 1;
      }
      elsif ($mode == 0) {
         push @head, $line;
      }
      elsif ($line =~ /^\.proc/) {
         @import = ();
         @func = ();
         push @func, $line;
      }
      elsif ($line =~ /^\.endproc/) {
         push @func, $line;

         $tmp = $func[0];
         $tmp =~ s/^\.proc//g;
         $tmp =~ s/[\s]//g;

         open FILE, ">split/$tmp.s";
         print FILE ";;; $tmp split from $file\n";
         print FILE @head;
         print FILE ".export $tmp\n";
         print FILE "\n";
         if ($#import >= 0) {
            print FILE ".import " . join(", ", @import) . "\n";
            print FILE "\n";
         }
         print FILE ".include \"nlib.inc\"\n";
         foreach $constant (@constants) {
            print FILE $constant;
         }
         print FILE "\n";
         print FILE @func;
         close FILE;
      }
      else {
         if ($line =~ /jsr/ || $line =~ /jmp/ || $line =~ /lda[\s]+#[<>]/ ) {
            $tmp = $line;
            $tmp =~ s/[\x0a\x0d]//g;
            $tmp =~ s/;.*//g;
            $tmp =~ s/jsr//g;
            $tmp =~ s/jmp//g;
            $tmp =~ s/lda[\s]+#[<>]//g;
            $tmp =~ s/[\s]//g;

            if (!($tmp =~ /[\(\@]/) && $tmp =~ /[a-zA-Z]/) {
               push @import, $tmp;
            }
         }
         push @func, $line;
      }
   }
}
