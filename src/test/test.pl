#!/usr/bin/perl

foreach $file (`ls *.n`) {
   $file =~ s/[\x0a\x0d]//g;

   @runner = `head -1 $file`;
   $runner = $runner[0];
   $runner =~ s/[\x0a\x0d]//g;

   if (!($runner =~ /\/\/ nc/)) {
      print "[FAIL] $file missing runner\n";
      exit -1;
   }
   elsif ($runner =~ /[;`]/ || $runner =~ /\.\./) {
      print "[FAIL] $file hey! no sneaky shell shenanigans !!!\n";
      exit -1;
   }

   $runner =~ s/^.../..\/compiler\//g;
   $runner .= " $file";

   $runner =~ s/  / /g; # remove extra spaces, for aesthetics

   $status = system("$runner >/dev/null 2>/dev/null");
   $exit_code = $? >> 8;

   if ($exit_code == 0) {
      print "[pass] $file\n";
   }
   else {
      print "[FAIL] $file exit code $exit_code\n";
      print "$runner\n";
      exit(-1);
   }
}
