#!/usr/bin/perl
use strict;
use warnings;

my $input_file = shift or die "Usage: $0 parser.output\n";
open my $fh, '<', $input_file or die "Cannot open $input_file: $!";

my $in_grammar = 0;
my $current_lhs = '';
my %recursion;
my $problems = 0;

while (<$fh>) {
    chomp;

    # Look for start of Grammar section
    if (/^Grammar$/) {
        $in_grammar = 1;
        next;
    }

    # End of Grammar section — Bison states begin like "State 0"
    last if $in_grammar && /^[A-Z]/;

    next unless $in_grammar;

    if (/^\s*\d+\s+(\w+):\s*(.*)$/) {
        $current_lhs = $1;
        process_rhs($current_lhs, $2);
    }
    elsif (/^\s*\|\s*(.*)$/) {
        process_rhs($current_lhs, $1);
    }
    elsif (/^\s+(.*)$/ && $current_lhs) {
        process_rhs($current_lhs, $1);
    }
}

close $fh;

# Output results
foreach my $rule (sort keys %recursion) {
    print "$rule: $recursion{$rule}\n";
}

sub process_rhs {
    my ($lhs, $rhs) = @_;
    $rhs =~ s/\s+/ /g;
    $rhs =~ s/^\s+|\s+$//g;
    return unless $rhs;

    my @tokens = split / /, $rhs;
    return unless @tokens;

    my $left = $tokens[0];
    my $right = $tokens[-1];

    my $is_left = ($left eq $lhs);
    my $is_right = ($right eq $lhs);

    if ($is_left && $is_right) {
        $recursion{$lhs} = 'BOTH recursive';
        $problems++;
    }
    elsif ($is_left) {
        $recursion{$lhs} = 'left recursive';
    }
    elsif ($is_right) {
        $recursion{$lhs} = 'RIGHT recursive';
        $problems++;
    }
}
