#!/usr/bin/perl

sub rplreset() {
%rpl = ();
$rpl{"sp"}="_nl_sp";
$rpl{"fp"}="_nl_fp";
$rpl{"arg0"}="_nl_arg0";
$rpl{"arg1"}="_nl_arg1";
$rpl{"ptr0"}="_nl_ptr0";
$rpl{"ptr1"}="_nl_ptr1";
$rpl{"ptr2"}="_nl_ptr2";
$rpl{"ptr3"}="_nl_ptr3";
$rpl{"tmp0"}="_nl_tmp0";
$rpl{"tmp1"}="_nl_tmp1";
$rpl{"tmp2"}="_nl_tmp2";
$rpl{"tmp3"}="_nl_tmp3";
$rpl{"tmp4"}="_nl_tmp4";
$rpl{"tmp5"}="_nl_tmp5";
}

`mkdir -p wrk`;
`cp nlib.inc wrk`;
foreach $file (`ls asm/*.asm`) {
   rplreset();
   $file =~ s/[\x0a\x0d]//g;

   print "== $file\n";
   open FILE, $file;
   @file = <FILE>;
   close FILE;

   @head = ();
   @constants = ();
   $mode = 0;
   foreach $line (@file) {
      if ($line =~ /^[^;]*=/) {
         $line =~ s/;.*//g;
         $line =~ s/ //g;
         $line =~ s/[\x0a\x0d]//g;
         ($l,$r) = split /=/, $line;
         $rpl{$l}=$r;
         #push @constants, $line;
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

         print "--- $tmp.s\n";
         open FILE, ">wrk/$tmp.s";
         print FILE ";;; $tmp wrk from $file\n";
         print FILE @head;
         print FILE ".export $tmp\n";
         print FILE "\n";
         if ($#import >= 0) {
            print FILE ".import " . join(", ", @import) . "\n";
            print FILE "\n";
         }
         print FILE ".include \"../nlib.inc\"\n";
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

         if ($line =~ /=/) {
            $line =~ s/;.*//g;
            $line =~ s/ //g;
            $line =~ s/[\x0a\x0d]//g;
            ($l,$r) = split /=/, $line;
            $rpl{$l}=$r;
         }
         else {
            foreach $rpl(keys(%rpl)) {
               $line =~ s/\b$rpl\b/$rpl{$rpl}/ge;
            }
            push @func, $line;
         }
      }
   }
}
