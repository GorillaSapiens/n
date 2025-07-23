#!/usr/bin/perl
use strict;
use warnings;

my $file = 'parser.output';
open my $fh, '<', $file or die "Cannot open $file: $!";

my %rules;
my %terminals = (
   "ADD_ASSIGN"	=>	"+=",
   "AND"	=>	"&&",
   "AND_ASSIGN"   	=>	"&=",
   "ARROW"	=>	"->",
   "ASSIGN"	=>	":=",
   "BREAK"	=>	"break",
   "CASE"	=>	"case",
   "CONST"	=>	"const",
   "CONTINUE"	=>	"continue",
   "DEC"	=>	"--",
   "DEFAULT"	=>	"default",
   "DIV_ASSIGN"	=>	"/=",
   "DO"	=>	"do",
   "ELSE"	=>	"else",
   "EQ"	=>	"==",
   "EXTERN"	=>	"extern",
   "FLAG"	=>	"\$flag",
   "FLOAT"	=>	"1.0",
   "FOR"	=>	"for",
   "GE"	=>	">=",
   "GOTO"	=>	"goto",
   "IDENTIFIER"	=>	"identifier",
   "IF"	=>	"if",
   "INC"	=>	"++",
   "INCLUDE"	=>	"include",
   "INTEGER"	=>	"1",
   "LE"	=>	"<=",
   "LSHIFT"	=>	"<<",
   "LSHIFT_ASSIGN"	=>	"<<=",
   "MOD_ASSIGN"	=>	"%=",
   "MUL_ASSIGN"	=>	"*=",
   "NE"	=>	"!=",
   "OPERATOR"	=>	"operator+",
   "OR"	=>	"||",
   "OR_ASSIGN"	=>	"|=",
   "QUICK"	=>	"quick",
   "REF"	=>	"ref",
   "RETURN"	=>	"return",
   "RSHIFT"	=>	">>",
   "RSHIFT_ASSIGN"	=>	">>=",
   "STATIC"	=>	"static",
   "STRING"	=>	"\"hello\"",
   "STRUCT"	=>	"struct",
   "SUB_ASSIGN"	=>	"-=",
   "SWITCH"	=>	"switch",
   "TYPE"	=>	"type",
   "TYPENAME"	=>	"typename",
   "UNION"	=>	"union",
   "WHILE"	=>	"while",
   "XOR_ASSIGN"	=>	"^=",
   "ε" => ""
);
my $in_rules = 0;
my $in_terminals = 0;
my $prevlhs;

while (<$fh>) {
    if (/^[A-Z]/) {
        $in_rules = 0;
        $in_terminals = 0;
    }
    if (/^Grammar/) {
        $in_rules = 1;
        $in_terminals = 0;
        next;
    }
    if (/^Terminals, with rules where they appear/) {
        $in_rules = 0;
        $in_terminals = 1;
        next;
    }

    if ($in_rules) {
        if (/^\s*(\d+)\s+(\S+)\s*:\s*(.*)/) {
            my ($num, $lhs, $rhs) = ($1, $2, $3);
            $prevlhs = $lhs;
            if (!($lhs =~ /^\$\@/)) {
                $rules{$num} = "$lhs : $rhs";
            }
        }
        elsif (/^\s*(\d+)\s*\|\s*(.*)/) {
            my ($num, $rhs) = ($1, $2);
            if (!($prevlhs =~ /^\$\@/)) {
               $rules{$num} = "$prevlhs : $rhs" if defined $rhs;
            }
        }
    } elsif ($in_terminals) {
        if (/^\s*'([^']+)'/) {
            $terminals{"\'$1\'"} //= $1;
        } elsif (/^\s*([A-Z_]+)\s+/) {
            $terminals{$1} //= $1;
        }
    }
}

close $fh;

print "Grammar:\n";
foreach my $num (sort { $a <=> $b } keys %rules) {
    print "$num: $rules{$num}\n";
}

print "\nTerminals:\n";
foreach my $term (sort keys %terminals) {
    print "$term $terminals{$term}\n";
}
