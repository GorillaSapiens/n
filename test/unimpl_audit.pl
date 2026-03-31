#!/usr/bin/perl
use strict;
use warnings;
use File::Basename qw(basename);

my @sources = ('../compiler/compile.c', '../compiler/float.c');
my %want;
for my $src (@sources) {
   open(my $fh, '<', $src) or die "could not open $src: $!\n";
   my $line = 0;
   while (my $text = <$fh>) {
      $line++;
      if ($text =~ /error_unimplemented\(/) {
         my $base = basename($src, '.c');
         $want{"unimpl_${base}_${line}.n"} = 1;
      }
   }
   close($fh);
}

my @missing;
for my $name (sort keys %want) {
   push @missing, $name unless -f $name;
}

if (@missing) {
   print "missing unimplemented coverage tests:\n";
   print "$_\n" for @missing;
   exit 1;
}

print "unimplemented coverage audit OK\n";
