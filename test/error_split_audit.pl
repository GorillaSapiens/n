#!/usr/bin/perl
use strict;
use warnings;
use FindBin;
use File::Spec;

my $root = File::Spec->catdir($FindBin::Bin, '..');
my @files = glob(File::Spec->catfile($root, 'compiler', '*.[ch]'));
push @files, glob(File::Spec->catfile($root, 'compiler', '*.l'));
my @bad;
my %counts = (
   error_user => 0,
   error_unimplemented => 0,
   error_unreachable => 0,
);

for my $file (@files) {
   open(my $fh, '<', $file) or die "could not open $file: $!\n";
   my $lineno = 0;
   while (my $line = <$fh>) {
      $lineno++;
      $counts{error_user}++ if $line =~ /\berror_user\s*\(/;
      $counts{error_unimplemented}++ if $line =~ /\berror_unimplemented\s*\(/;
      $counts{error_unreachable}++ if $line =~ /\berror_unreachable\s*\(/;
      next if $file =~ /compiler\/messages\.c$/ && $line =~ /^void error\s*\(/;
      next if $file =~ /compiler\/messages\.h$/ && $line =~ /^void noreturn error\s*\(/;
      next if $line =~ /^\s*\/\//;
      if ($line =~ /\berror\s*\(/) {
         push @bad, "$file:$lineno:$line";
      }
   }
   close($fh);
}

for my $name (qw(error_user error_unimplemented error_unreachable)) {
   die "$name was never referenced\n" if !$counts{$name};
}

if (@bad) {
   die "bare error() call(s) remain:\n" . join('', @bad);
}

print "[pass] error split audit\n";
