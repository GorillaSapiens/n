#!/usr/bin/perl

foreach $file (`ls *.n`) {
   $file =~ s/[\x0a\x0d]//g;
   print "=== $file\n";

   @runner = `head -1 $file`;
   $runner = $runner[0];
   $runner =~ s/[\x0a\x0d]//g;

   if (!($runner =~ /\/\/ nc /)) {
      print "[FAIL] missing runner\n";
      exit -1;
   }
   elsif ($runner =~ /[;`]/ || $runner =~ /\.\./) {
      print "[FAIL] hey! no sneaky shell shenanigans !!!\n";
      exit -1;
   }

   $runner =~ s/^.../..\/compiler\//g;
   $runner .= " $file";
   print "$runner\n";

   $status = system("$runner");
   $exit_code = $? >> 8;

   if ($exit_code == 0) {
      print "[pass]\n";
   }
   else {
      print "[FAIL] exit code $exit_code\n";
   }
}
