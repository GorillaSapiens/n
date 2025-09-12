#!/usr/bin/env perl
use strict;
use warnings;
use utf8;

# Usage: perl check_parser.pl grammar.y
# Goal: For every action block in a Bison/Yacc .y grammar, verify that
#       all NONTERMINALS on the RHS so far are referenced via $n.
# Assumptions:
#   - ALL-CAPS identifiers are terminals.
#   - Single-quoted character literals are terminals.
#   - Mid-rule actions are supported; they create a synthetic symbol which is
#     NOT required to be referenced.
# Output:
#   file:line: unused $n in action for rule 'LHS' (alternative starts here)

my $file = shift @ARGV or die "Usage: $0 grammar.y\n";
open my $fh, "<:raw", $file or die "Cannot open $file: $!\n";
local $/;
my $src = <$fh>;
close $fh;

# Extract grammar section between %% ... %%
my $i1 = index($src, "%%");
$i1 >= 0 or die "No '%%' section delimiter found.\n";
my $i2 = index($src, "%%", $i1 + 2);
$i2 >= 0 or die "Only one '%%' delimiter found; need two.\n";
my $grammar = substr($src, $i1 + 2, $i2 - ($i1 + 2));

# --- Utilities ---------------------------------------------------------------

sub line_of_pos {
    my ($s, $p) = @_;
    $p = 0 if $p < 0;
    $p = length($s) if $p > length($s);
    # Count newlines up to $p
    my $sub = substr($s, 0, $p);
    my $c = ($sub =~ tr/\n//);
    return $c + 1;
}

sub is_terminal_id {
    my ($id) = @_;
    return $id =~ /^[A-Z][A-Z0-9_]*$/;
}

# Parse a single-quoted char literal starting at $pos in $grammar.
# Returns new position (past the literal) and the literal text; dies if unterminated.
sub parse_char_token_at {
    my ($start) = @_;
    my $p = $start;
    my $len = length($grammar);
    die "Internal: parse_char_token_at not at quote\n" unless substr($grammar, $p, 1) eq "'";
    $p++; # consume opening '
    my $esc = 0;
    while ($p < $len) {
        my $ch = substr($grammar, $p, 1);
        $p++;
        if ($esc) { $esc = 0; next; }
        if ($ch eq "\\") { $esc = 1; next; }
        if ($ch eq "'") {
            my $text = substr($grammar, $start, $p - $start);
            return ($p, $text);
        }
    }
    my $line = line_of_pos($grammar, $start);
    die "Unterminated character literal at line $line\n";
}

# Parse a { ... } action block starting at $pos (on '{').
# Handles nested braces, strings, chars, // and /* */ comments.
sub parse_action_at {
    my ($start) = @_;
    my $p   = $start;
    my $len = length($grammar);
    die "Internal: parse_action_at not at '{'\n" unless substr($grammar, $p, 1) eq '{';
    my $start_line = line_of_pos($grammar, $p);

    my $code = '';
    my $depth = 1;          # we're ON the first '{'
    my $in_sl = 0;          # //
    my $in_ml = 0;          # /* */
    my $in_sq = 0;          # '...'
    my $in_dq = 0;          # "..."
    my $esc   = 0;

    $code .= '{';           # include the opening brace
    $p++;

    while ($p < $len) {
        my $ch = substr($grammar, $p, 1);
        my $n2 = ($p + 1 < $len) ? substr($grammar, $p, 2) : '';
        $code .= $ch;
        $p++;

        # inside single-line comment
        if ($in_sl) { $in_sl = 0 if $ch eq "\n"; next; }
        # inside multi-line comment
        if ($in_ml) { if ($n2 eq '*/') { $code .= '/'; $p++; $in_ml = 0; } next; }
        # inside char literal
        if ($in_sq) { if ($ch eq "\\" && !$esc){ $esc=1; next } if ($ch eq "'" && !$esc){ $in_sq=0 } $esc=0; next; }
        # inside string literal
        if ($in_dq) { if ($ch eq "\\" && !$esc){ $esc=1; next } if ($ch eq '"' && !$esc){ $in_dq=0 } $esc=0; next; }

        # starting comments/strings
        if ($n2 eq '//') { $code .= '/'; $p++; $in_sl = 1; next; }
        if ($n2 eq '/*') { $code .= '*'; $p++; $in_ml = 1; next; }
        if ($ch eq "'")  { $in_sq = 1; next; }
        if ($ch eq '"')  { $in_dq = 1; next; }

        # braces (only count when not in comment/string)
        if ($ch eq '{') { $depth++; next; }
        if ($ch eq '}') {
            $depth--;
            if ($depth == 0) {
                # strip outer braces from returned code
                (my $raw = $code) =~ s/^\{//s;
                $raw =~ s/\}$//s;
                return ($p, $raw, $start_line);
            }
            next;
        }
    }
    die "Unterminated action block starting at line $start_line\n";
}

# --- Tokenizer ---------------------------------------------------------------

# We keep an explicit $pos pointer and always run regexes directly on $grammar.
my $pos = 0;
pos($grammar) = 0;

my @look = (); # token lookahead buffer

# Token structure: { type => ..., line => N, ... }
sub peek_token {
    my $t = next_token();
    unshift @look, $t if defined $t;
    return $t;
}

sub next_token {
    return shift @look if @look;

    my $len = length($grammar);

    # Skip whitespace and comments
    while ($pos < $len) {
        pos($grammar) = $pos;
        # spaces/tabs/formfeeds
        if ($grammar =~ /\G[ \t\r\f]+/gc) { $pos = pos($grammar); next; }
        # newlines
        if ($grammar =~ /\G\n/gc) { $pos = pos($grammar); next; }
        # // comment
        if ($grammar =~ /\G\/\/[^\n]*/gc) { $pos = pos($grammar); next; }
        # /* */ comment
        if ($grammar =~ /\G\/\*.*?\*\//gcs) { $pos = pos($grammar); next; }
        last;
    }
    return undef if $pos >= $len;

    my $start_line = line_of_pos($grammar, $pos);

    # Punctuation
    my $ch = substr($grammar, $pos, 1);
    if ($ch eq ':' or $ch eq '|' or $ch eq ';') {
        $pos++;
        return { type => ($ch eq ':' ? 'colon' : $ch eq '|' ? 'bar' : 'semi'), line => $start_line };
    }

    # Single-quoted char token
    if ($ch eq "'") {
        my ($newpos, $text) = parse_char_token_at($pos);
        my $tok = { type => 'term_char', text => $text, line => $start_line };
        $pos = $newpos;
        return $tok;
    }

    # Action block
    if ($ch eq '{') {
        my ($newpos, $code, $aline) = parse_action_at($pos);
        my $tok = { type => 'action', code => $code, line => $aline };
        $pos = $newpos;
        return $tok;
    }

    # Identifier
    pos($grammar) = $pos;
    if ($grammar =~ /\G([A-Za-z_][A-Za-z_0-9]*)/gc) {
        my $id = $1;
        $pos = pos($grammar);
        return { type => 'id', value => $id, line => $start_line };
    }

    # Unrecognized: advance one char to avoid infinite loop
    $pos++;
    return next_token();
}

# --- Parser & Checker --------------------------------------------------------

# Parse: LHS ':' alt ('|' alt)* ';'
# alt := sequence (action? where action before |/; is final-action)
# sequence items: id (terminal/nonterminal by rule), term_char, mid-rule action

while (defined (my $tok = next_token())) {
    next unless $tok->{type} eq 'id';
    my $lhs = $tok->{value};

    my $colon = next_token();
    next unless $colon && $colon->{type} eq 'colon';

    ALT: while (1) {
        my @seq = (); # items so far in this alternative

        while (defined (my $t = next_token())) {
            if ($t->{type} eq 'id') {
                my $is_term = is_terminal_id($t->{value});
                push @seq, { type => ($is_term ? 'terminal' : 'nonterminal'), synthetic => 0 };
                next;
            }
            if ($t->{type} eq 'term_char') {
                push @seq, { type => 'terminal', synthetic => 0 };
                next;
            }
            if ($t->{type} eq 'action') {
                # Final action if next token is '|' or ';' (peek without consuming)
                my $after = peek_token();
                my $is_final = ($after && ($after->{type} eq 'bar' || $after->{type} eq 'semi')) ? 1 : 0;

                my $visible = scalar(@seq);
                my %expected;
                for my $i (1..$visible) {
                    my $sym = $seq[$i-1];
                    next unless $sym->{type} eq 'nonterminal';
                    next if $sym->{synthetic};
                    $expected{$i} = 1;
                }

                # Which $n are used in this action
                my %used;
                my $code = $t->{code} // '';
                while ($code =~ /\$(\d+)/g) {
                    my $n = $1 + 0;
                    $used{$n} = 1 if $n >= 1 && $n <= $visible;
                }

                # Report missing
                for my $n (sort { $a <=> $b } keys %expected) {
                    next if $used{$n};
                    printf "%s:%d: unused \$%d in action for rule '%s' (alternative starts here)\n",
                        $file, $t->{line}, $n, $lhs;
                }

                # Mid-rule action creates a synthetic nonterminal
                if (!$is_final) {
                    push @seq, { type => 'nonterminal', synthetic => 1 };
                }
                next;
            }
            if ($t->{type} eq 'bar') {
                next ALT;
            }
            if ($t->{type} eq 'semi') {
                last ALT;
            }
            # else ignore unknowns
        }
        last ALT; # safety
    }
}

exit 0;
