#!/usr/bin/perl
use strict;
use warnings;
use Math::BigInt;

sub usage { die "usage: $0 <typename> <little|big> <size-bytes> <exp-bits>\n"; }
sub pow2s { my ($b)=@_; my $x=Math::BigInt->new(1); $x->blsft($b); return $x->bstr(); }
sub all1s { my ($b)=@_; my $x=Math::BigInt->new(1); $x->blsft($b); $x->bdec(); return $x->bstr(); }
sub typed { my ($v,$t)=@_; return "$v`$t"; }

my ($typename,$endian,$size,$expbits)=@ARGV;
usage() if @ARGV != 4;
usage() if $endian ne 'little' && $endian ne 'big';
usage() if $size !~ /^\d+$/ || $size <= 0;
usage() if $expbits !~ /^\d+$/ || $expbits <= 0;
my $total = $size * 8;
die "$0: invalid layout\n" if 1 + $expbits >= $total;
die "$0: generated arithmetic currently supports sizes up to 8 bytes\n" if $size > 8;
my $mbits = $total - 1 - $expbits;
my $last = $size - 1;
my $san = $typename; $san =~ s/[^A-Za-z0-9_]/_/g; $san = "_$san" if $san !~ /^[A-Za-z_]/;
my $u = "__nlf_${san}_u";
my $bits = "__nlf_${san}_bits";
my $ov = "__nlf_${san}_overlay";
my $vov = "__nlf_${san}_value_overlay";
my $uov = "__nlf_${san}_u_overlay";
my ($matht,$math_bytes);
if ($size <= 2) { $matht = 'int'; $math_bytes = 2; }
elsif ($size <= 4) { $matht = 'long'; $math_bytes = 4; }
else { $matht = 'longlong'; $math_bytes = 8; }
my $mov = "__nlf_${san}_math_overlay";

my $ga = "__nlf_${san}_a";
my $gb = "__nlf_${san}_b";
my $gr = "__nlf_${san}_r";
my $gt = "__nlf_${san}_t";
my $gmant_u = "__nlf_${san}_mant_u";
my $gsig_u = "__nlf_${san}_sig_u";
my $gtmp_u = "__nlf_${san}_tmp_u";
my $gmant_ll = "__nlf_${san}_mant_ll";
my $gsig_ll = "__nlf_${san}_sig_ll";
my $gtmp_ll = "__nlf_${san}_tmp_ll";
my $gamag = "__nlf_${san}_amag";
my $gbmag = "__nlf_${san}_bmag";
my $gexp_a = "__nlf_${san}_exp_a";
my $gexp_b = "__nlf_${san}_exp_b";
my $gsign_out = "__nlf_${san}_sign_out";
my $gsig_a = "__nlf_${san}_sig_a";
my $gsig_b = "__nlf_${san}_sig_b";
my $gsig_out = "__nlf_${san}_sig_out";
my $gcmp = "__nlf_${san}_cmp";
my $gunordered = "__nlf_${san}_unordered";
my $add_impl = "__nlf_${san}_add_impl";
my $cmp_impl = "__nlf_${san}_cmp_impl";

my $ZERO = typed(0,$u);
my $ONE = typed(1,$u);
my $EXP_MAX = typed(all1s($expbits),$u);
my $NAN_PAYLOAD = typed($mbits > 1 ? pow2s($mbits-1) : 1, $u);
my $M_ZERO = typed(0,$matht);
my $M_ONE = typed(1,$matht);
my $M_TWO = typed(2,$matht);
my $M_EIGHT = typed(8,$matht);
my $M_EXP_MAX = typed(all1s($expbits),$matht);
my $FRACMASK = typed(all1s($mbits),$u);
my $M_HIDDEN = typed(pow2s($mbits),$matht);
my $M_HIDDEN_EXT = typed(pow2s($mbits+3),$matht);
my $M_CARRY = typed(pow2s($mbits+1),$matht);
my $M_NAN_PAYLOAD = typed($mbits > 1 ? pow2s($mbits-1) : 1,$matht);

my $CMP_EQ = 0;
my $CMP_LT = 1;
my $CMP_GT = 2;

sub load_code {
   my ($ovname,$valname,$iv)=@_;
   return "   $ovname.value := $valname;\n" if $endian eq 'little';
   return "   $vov $iv;\n   int ${ovname}_i;\n   $iv.value := $valname;\n   ${ovname}_i := 0;\n   while (${ovname}_i < $size) {\n      $ovname.bytes[${ovname}_i] := $iv.bytes[$last - ${ovname}_i];\n      ${ovname}_i++;\n   }\n";
}

sub store_code {
   my ($ovname,$vv)=@_;
   return "   return $ovname.value;\n" if $endian eq 'little';
   return "   $vov $vv;\n   int ${ovname}_o;\n   ${ovname}_o := 0;\n   while (${ovname}_o < $size) {\n      $vv.bytes[${ovname}_o] := $ovname.bytes[$last - ${ovname}_o];\n      ${ovname}_o++;\n   }\n   return $vv.value;\n";
}

sub to_ll_snippet {
   my ($dst,$srcu,$indent)=@_;
   $indent //= '      ';
   my $s = "${indent}$dst.v := $M_ZERO;\n";
   for my $i (0 .. $last) {
      $s .= "${indent}$dst.bytes[$i] := $srcu.bytes[$i];\n";
   }
   for my $i ($size .. $math_bytes - 1) {
      $s .= "${indent}$dst.bytes[$i] := 0;\n";
   }
   return $s;
}

sub from_ll_snippet {
   my ($dstu,$srcll,$indent)=@_;
   $indent //= '      ';
   my $s = "";
   for my $i (0 .. $last) {
      $s .= "${indent}$dstu.bytes[$i] := $srcll.bytes[$i];\n";
   }
   return $s;
}

sub raw_to_math_snippet {
   my ($dst,$src,$indent)=@_;
   $indent //= '      ';
   my $s = "${indent}$dst.v := $M_ZERO;
";
   for my $i (0 .. $last-1) {
      $s .= "${indent}$dst.bytes[$i] := $src.bytes[$i];
";
   }
   $s .= "${indent}$dst.bytes[$last] := ($src.bytes[$last] & 255) & 127;
";
   for my $i ($size .. $math_bytes - 1) {
      $s .= "${indent}$dst.bytes[$i] := 0;
";
   }
   return $s;
}

sub mag_cmp_snippet {
   my ($dst,$lhs,$rhs,$indent)=@_;
   $indent //= '   ';
   my $s = "${indent}$dst := $CMP_EQ;
";
   for (my $i = $last; $i >= 0; --$i) {
      my $l = "($lhs.bytes[$i] & 255)";
      my $r = "($rhs.bytes[$i] & 255)";
      if ($i == $last) {
         $l = "($l & 127)";
         $r = "($r & 127)";
      }
      $s .= "${indent}if ($dst == $CMP_EQ) {
";
      $s .= "${indent}   if ($l < $r) { $dst := $CMP_LT; }
";
      $s .= "${indent}   else { if ($l > $r) { $dst := $CMP_GT; } }
";
      $s .= "${indent}}
";
   }
   return $s;
}

sub pack_snippet {
   my ($sig,$exp,$sign,$out,$mant,$tmpu,$tmpll)=@_;
   return "      $out.raw := $ZERO;\n"
      . "      if ($sig == $M_ZERO) {\n"
      . "         $out.bits.sign := $sign;\n"
      . "      }\n"
      . "      else {\n"
      . "         ${matht} low;\n"
      . "         low := $sig & 7`$matht;\n"
      . "         $mant := $sig / $M_EIGHT;\n"
      . "         if ((low & 4`$matht) != $M_ZERO && (((low & 3`$matht) != $M_ZERO) || (($mant & $M_ONE) != $M_ZERO))) {\n"
      . "            $mant++;\n"
      . "         }\n"
      . "         if ($mant >= $M_CARRY) {\n"
      . "            $mant /= $M_TWO;\n"
      . "            $exp++;\n"
      . "         }\n"
      . "         if ($exp >= $M_EXP_MAX) {\n"
      . "            $out.bits.sign := $sign;\n"
      . "            $out.bits.exponent := $EXP_MAX;\n"
      . "            $out.bits.mantissa := $ZERO;\n"
      . "         }\n"
      . "         else {\n"
      . "            $out.bits.sign := $sign;\n"
      . "            if ($mant >= $M_HIDDEN) {\n"
      . "               $tmpll.v := $exp;\n"
      . from_ll_snippet($tmpu,$tmpll,'               ')
      . "               $out.bits.exponent := $tmpu.v;\n"
      . "               $tmpll.v := $mant;\n"
      . from_ll_snippet($tmpu,$tmpll,'               ')
      . "               $out.bits.mantissa := $tmpu.v & $FRACMASK;\n"
      . "            }\n"
      . "            else {\n"
      . "               $out.bits.exponent := $ZERO;\n"
      . "               $tmpll.v := $mant;\n"
      . from_ll_snippet($tmpu,$tmpll,'               ')
      . "               $out.bits.mantissa := $tmpu.v;\n"
      . "            }\n"
      . "         }\n"
      . "      }\n";
}

print <<"EOF";
// generated by libraries/float/gen.pl
// typename: $typename
// endian: $endian
// size: $size
// expbits: $expbits
// implementation: manual SExMy arithmetic using bitfields and unions

type $u { \$size:$size \$unsigned \$endian:little };

struct $bits {
   $u mantissa:$mbits;
   $u exponent:$expbits;
   $u sign:1;
};
EOF

if ($endian eq 'little') {
print <<"EOF";
union $ov {
   $typename value;
   $u raw;
   char bytes[$size];
   $bits bits;
};
EOF
} else {
print <<"EOF";
union $vov {
   $typename value;
   char bytes[$size];
};

union $ov {
   $u raw;
   char bytes[$size];
   $bits bits;
};
EOF
}

print <<"EOF";
union $uov {
   $u v;
   char bytes[$size];
};

union $mov {
   $matht v;
   char bytes[$math_bytes];
};

$ov $ga;
$ov $gb;
$ov $gr;
$ov $gt;
$uov $gmant_u;
$uov $gsig_u;
$uov $gtmp_u;
$mov $gmant_ll;
$mov $gsig_ll;
$mov $gtmp_ll;
$mov $gamag;
$mov $gbmag;
$matht $gexp_a;
$matht $gexp_b;
int $gsign_out;
$matht $gsig_a;
$matht $gsig_b;
$matht $gsig_out;
int $gcmp;
int $gunordered;
EOF

print "void $add_impl(void) {\n";
print "   if ($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa != $ZERO) {\n      $gr.raw := $ga.raw;\n      return;\n   }\n";
print "   if ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa != $ZERO) {\n      $gr.raw := $gb.raw;\n      return;\n   }\n";
print "   if ($ga.bits.exponent == $EXP_MAX && $gb.bits.exponent == $EXP_MAX && $ga.bits.sign != $gb.bits.sign) {\n      $gr.raw := $ZERO;\n      $gr.bits.sign := $ZERO;\n      $gr.bits.exponent := $EXP_MAX;\n      $gr.bits.mantissa := $NAN_PAYLOAD;\n      return;\n   }\n";
print "   if ($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa == $ZERO) {\n      $gr.raw := $ga.raw;\n      return;\n   }\n";
print "   if ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa == $ZERO) {\n      $gr.raw := $gb.raw;\n      return;\n   }\n";
print "   if ($ga.bits.exponent == $ZERO && $ga.bits.mantissa == $ZERO && $gb.bits.exponent == $ZERO && $gb.bits.mantissa == $ZERO) {\n      $gr.raw := $ZERO;\n      if ($ga.bits.sign == $gb.bits.sign) { $gr.bits.sign := $ga.bits.sign; } else { $gr.bits.sign := $ZERO; }\n      return;\n   }\n";
print "   if ($ga.bits.exponent == $ZERO && $ga.bits.mantissa == $ZERO) {\n      $gr.raw := $gb.raw;\n      return;\n   }\n";
print "   if ($gb.bits.exponent == $ZERO && $gb.bits.mantissa == $ZERO) {\n      $gr.raw := $ga.raw;\n      return;\n   }\n";
print "   $gsig_u.v := $ga.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'   ');
print "   $gsig_a := $gsig_ll.v;\n   $gsig_u.v := $gb.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'   ');
print "   $gsig_b := $gsig_ll.v;\n   $gexp_a := $ga.bits.exponent;\n   $gexp_b := $gb.bits.exponent;\n   if ($gexp_a == $M_ZERO) { $gexp_a := $M_ONE; }\n   if ($gexp_b == $M_ZERO) { $gexp_b := $M_ONE; }\n   if ($ga.bits.exponent != $ZERO) { $gsig_a |= $M_HIDDEN; }\n   if ($gb.bits.exponent != $ZERO) { $gsig_b |= $M_HIDDEN; }\n   $gsig_a *= $M_EIGHT;\n   $gsig_b *= $M_EIGHT;\n   if ($ga.bits.sign == $gb.bits.sign) {\n      while ($gexp_a < $gexp_b) { $matht lost; lost := $gsig_a & $M_ONE; $gsig_a /= $M_TWO; if (lost != $M_ZERO) { $gsig_a |= $M_ONE; } $gexp_a++; }\n      while ($gexp_b < $gexp_a) { $matht lost; lost := $gsig_b & $M_ONE; $gsig_b /= $M_TWO; if (lost != $M_ZERO) { $gsig_b |= $M_ONE; } $gexp_b++; }\n      $gsig_out := $gsig_a + $gsig_b;\n      $gsign_out := $ga.bits.sign;\n";
print pack_snippet($gsig_out,$gexp_a,$gsign_out,$gr,"$gmant_ll.v",$gtmp_u,$gtmp_ll);
print "      return;\n   }\n";
print mag_cmp_snippet($gcmp,$ga,$gb,'   ');
print "   if ($gcmp == $CMP_EQ) { $gr.raw := $ZERO; return; }\n   if ($gcmp == $CMP_LT) {\n      $gt.raw := $ga.raw;\n      $ga.raw := $gb.raw;\n      $gb.raw := $gt.raw;\n      $gsig_u.v := $ga.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'      ');
print "      $gsig_a := $gsig_ll.v;\n      $gsig_u.v := $gb.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'      ');
print "      $gsig_b := $gsig_ll.v;\n      $gexp_a := $ga.bits.exponent;\n      $gexp_b := $gb.bits.exponent;\n      if ($gexp_a == $M_ZERO) { $gexp_a := $M_ONE; }\n      if ($gexp_b == $M_ZERO) { $gexp_b := $M_ONE; }\n      if ($ga.bits.exponent != $ZERO) { $gsig_a |= $M_HIDDEN; }\n      if ($gb.bits.exponent != $ZERO) { $gsig_b |= $M_HIDDEN; }\n      $gsig_a *= $M_EIGHT;\n      $gsig_b *= $M_EIGHT;\n   }\n   while ($gexp_a < $gexp_b) { $matht lost; lost := $gsig_a & $M_ONE; $gsig_a /= $M_TWO; if (lost != $M_ZERO) { $gsig_a |= $M_ONE; } $gexp_a++; }\n   while ($gexp_b < $gexp_a) { $matht lost; lost := $gsig_b & $M_ONE; $gsig_b /= $M_TWO; if (lost != $M_ZERO) { $gsig_b |= $M_ONE; } $gexp_b++; }\n   $gsig_out := $gsig_a - $gsig_b;\n   $gsign_out := $ga.bits.sign;\n   while ($gsig_out != $M_ZERO && $gexp_a > $M_ONE && $gsig_out < $M_HIDDEN_EXT) { $gsig_out *= $M_TWO; $gexp_a--; }\n";
print pack_snippet($gsig_out,$gexp_a,$gsign_out,$gr,"$gmant_ll.v",$gtmp_u,$gtmp_ll);
print "}\n\n";

print "void $cmp_impl(void) {\n   $gunordered := 0;\n   if ($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa != $ZERO) { $gunordered := 1; $gcmp := 0; return; }\n   if ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa != $ZERO) { $gunordered := 1; $gcmp := 0; return; }\n   if ($ga.bits.exponent == $ZERO && $ga.bits.mantissa == $ZERO && $gb.bits.exponent == $ZERO && $gb.bits.mantissa == $ZERO) { $gcmp := 0; return; }\n   if ($ga.bits.sign != $gb.bits.sign) { if ($ga.bits.sign != $ZERO) { $gcmp := $CMP_LT; } else { $gcmp := $CMP_GT; } return; }\n";
print mag_cmp_snippet($gcmp,$ga,$gb,'   ');
print "   if ($ga.bits.sign != $ZERO) { if ($gcmp == $CMP_LT) { $gcmp := $CMP_GT; } else { if ($gcmp == $CMP_GT) { $gcmp := $CMP_LT; } } }\n}\n\n";

print "$typename operator+($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $add_impl();\n";
print store_code($gr,'rv');
print "}\n\n";

print "$typename operator-($typename lhs, $typename rhs) {\n";
# correct for reversed 2-arg call order: rhs is left operand, lhs is right operand
print load_code($ga,'rhs','av');
print load_code($gb,'lhs','bv');
print "   $gb.bits.sign ^= $ONE;\n   $add_impl();\n";
print store_code($gr,'rv');
print "}\n\n";

print "bool operator==($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp == $CMP_EQ) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator!=($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 1`bool; }\n   if ($gcmp != $CMP_EQ) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator<($typename lhs, $typename rhs) {\n";
print load_code($ga,'rhs','av');
print load_code($gb,'lhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp == $CMP_LT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator>($typename lhs, $typename rhs) {\n";
print load_code($ga,'rhs','av');
print load_code($gb,'lhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp == $CMP_GT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator<=($typename lhs, $typename rhs) {\n";
print load_code($ga,'rhs','av');
print load_code($gb,'lhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp != $CMP_GT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator>=($typename lhs, $typename rhs) {\n";
print load_code($ga,'rhs','av');
print load_code($gb,'lhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp != $CMP_LT) { return 1`bool; }\n   return 0`bool;\n}\n";
