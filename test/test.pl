#!/usr/bin/perl

use strict;
use warnings;
use File::Temp qw(tempfile);

foreach my $file (`ls *.n`) {
   $file =~ s/[\x0a\x0d]//g;

   open(my $fh, '<', $file) or die "[FAIL] could not open $file: $!\n";
   my @lines = <$fh>;
   close($fh);

   my $runner = $lines[0] // '';
   $runner =~ s/[\x0a\x0d]//g;

   if (!($runner =~ /\/\/ nc/)) {
      print "[FAIL] $file missing runner\n";
      exit -1;
   }
   elsif ($runner =~ /[;`]/ || $runner =~ /\.\./) {
      print "[FAIL] $file hey! no sneaky shell shenanigans !!!\n";
      exit -1;
   }

   my @expectasm;
   my @expecterr;
   my $expectfail = 0;
   for my $line (@lines) {
      if ($line =~ /^\/\/ expectasm:\s*(.*?)\s*$/) {
         push @expectasm, $1;
      }
      if ($line =~ /^\/\/ expecterr:\s*(.*?)\s*$/) {
         push @expecterr, $1;
      }
      if ($line =~ /^\/\/ expectfail\s*$/) {
         $expectfail = 1;
      }
   }

   $runner =~ s/^.../..\/compiler\//g;
   $runner .= " $file";
   $runner =~ s/  / /g; # remove extra spaces, for aesthetics

   my ($outfh, $outfile) = tempfile('test_output_XXXX', UNLINK => 1);
   my ($errfh, $errfile) = tempfile('test_error_XXXX', UNLINK => 1);
   close($outfh);
   close($errfh);

   my $status = system("$runner >$outfile 2>$errfile");
   my $exit_code = $? >> 8;

   if ($expectfail) {
      if ($exit_code == 0) {
         print "[FAIL] $file expected failure but exited 0\n";
         print "$runner\n";
         exit(-1);
      }
   }
   elsif ($exit_code != 0) {
      print "[FAIL] $file exit code $exit_code\n";
      print "$runner\n";
      exit(-1);
   }

   if (@expectasm) {
      open(my $out, '<', $outfile) or die "[FAIL] could not read $outfile: $!\n";
      local $/;
      my $asm = <$out>;
      close($out);

      for my $needle (@expectasm) {
         if (index($asm, $needle) < 0) {
            print "[FAIL] $file missing assembly fragment: $needle\n";
            print "$runner\n";
            exit(-1);
         }
      }
   }

   if (@expecterr) {
      open(my $err, '<', $errfile) or die "[FAIL] could not read $errfile: $!\n";
      local $/;
      my $stderr = <$err>;
      close($err);

      for my $needle (@expecterr) {
         if (index($stderr, $needle) < 0) {
            print "[FAIL] $file missing error fragment: $needle\n";
            print "$runner\n";
            exit(-1);
         }
      }
   }

   print "[pass] $file\n";
}
