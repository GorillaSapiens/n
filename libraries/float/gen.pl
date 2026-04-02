#!/usr/bin/perl
use strict;
use warnings;
use Math::BigInt;

sub usage { die "usage: $0 <typename> <little|big> <size-bytes> <exp-bits>\n"; }
sub pow2s { my ($b)=@_; my $x=Math::BigInt->new(1); $x->blsft($b); return $x->bstr(); }
sub all1s { my ($b)=@_; my $x=Math::BigInt->new(1); $x->blsft($b); $x->bdec(); return $x->bstr(); }
sub typed { my ($v,$t)=@_; return "$v`$t"; }
sub biass {
   my ($b)=@_;
   my $x = Math::BigInt->new(1);
   $x->blsft($b - 1);
   $x->bdec();
   return $x->bstr();
}

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
my $wide_bytes = $size * 2;
my $san = $typename; $san =~ s/[^A-Za-z0-9_]/_/g; $san = "t_$san" if $san !~ /^[A-Za-z]/;
my $u = "nlf_${san}_u";
my $wu = "nlf_${san}_wide_u";
my $bits = "nlf_${san}_bits";
my $ov = "nlf_${san}_overlay";
my $vov = "nlf_${san}_value_overlay";
my $uov = "nlf_${san}_u_overlay";
my ($matht,$math_bytes);
if ($size <= 2) { $matht = 'int'; $math_bytes = 2; }
elsif ($size <= 4) { $matht = 'long'; $math_bytes = 4; }
else { $matht = 'longlong'; $math_bytes = 8; }
my $mov = "nlf_${san}_math_overlay";
my $wov = "nlf_${san}_wide_overlay";

my $ga = "nlf_${san}_a";
my $gb = "nlf_${san}_b";
my $gr = "nlf_${san}_r";
my $gt = "nlf_${san}_t";
my $gmant_u = "nlf_${san}_mant_u";
my $gsig_u = "nlf_${san}_sig_u";
my $gtmp_u = "nlf_${san}_tmp_u";
my $gmant_ll = "nlf_${san}_mant_ll";
my $gsig_ll = "nlf_${san}_sig_ll";
my $gtmp_ll = "nlf_${san}_tmp_ll";
my $gamag = "nlf_${san}_amag";
my $gbmag = "nlf_${san}_bmag";
my $gwide_a = "nlf_${san}_wide_a";
my $gwide_b = "nlf_${san}_wide_b";
my $gwide_p = "nlf_${san}_wide_p";
my $gwide_t = "nlf_${san}_wide_t";
my $gexp_a = "nlf_${san}_exp_a";
my $gexp_b = "nlf_${san}_exp_b";
my $gsign_out = "nlf_${san}_sign_out";
my $gsig_a = "nlf_${san}_sig_a";
my $gsig_b = "nlf_${san}_sig_b";
my $gsig_out = "nlf_${san}_sig_out";
my $gcmp = "nlf_${san}_cmp";
my $gunordered = "nlf_${san}_unordered";
my $add_impl = "nlf_${san}_add_impl";
my $cmp_impl = "nlf_${san}_cmp_impl";
my $mul_impl = "nlf_${san}_mul_impl";

my $ZERO = typed(0,$u);
my $ONE = typed(1,$u);
my $W_ZERO = typed(0,$wu);
my $W_ONE = typed(1,$wu);
my $EXP_MAX = typed(all1s($expbits),$u);
my $NAN_PAYLOAD = typed($mbits > 1 ? pow2s($mbits-1) : 1, $u);
my $M_ZERO = typed(0,$matht);
my $M_ONE = typed(1,$matht);
my $M_TWO = typed(2,$matht);
my $M_EIGHT = typed(8,$matht);
my $M_EXP_MAX = typed(all1s($expbits),$matht);
my $M_BIAS = typed(biass($expbits),$matht);
my $FRACMASK = typed(all1s($mbits),$u);
my $M_HIDDEN = typed(pow2s($mbits),$matht);
my $M_HIDDEN_EXT = typed(pow2s($mbits+3),$matht);
my $M_CARRY = typed(pow2s($mbits+1),$matht);

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

sub ll_to_wide_snippet {
   my ($dstw,$srcll,$indent)=@_;
   $indent //= '      ';
   return "${indent}$dstw.v := $srcll.v;\n";
}

sub wide_to_ll_snippet {
   my ($dstll,$srcw,$indent)=@_;
   $indent //= '      ';
   return "${indent}$dstll.v := $srcw.v;\n";
}

sub mag_cmp_snippet {
   my ($dst,$lhs,$rhs,$indent)=@_;
   $indent //= '   ';
   my $s = "${indent}$dst := $CMP_EQ;\n";
   for (my $i = $last; $i >= 0; --$i) {
      my $l = "($lhs.bytes[$i] & 255)";
      my $r = "($rhs.bytes[$i] & 255)";
      if ($i == $last) {
         $l = "($l & 127)";
         $r = "($r & 127)";
      }
      $s .= "${indent}if ($dst == $CMP_EQ) {\n";
      $s .= "${indent}   if ($l < $r) { $dst := $CMP_LT; }\n";
      $s .= "${indent}   else { if ($l > $r) { $dst := $CMP_GT; } }\n";
      $s .= "${indent}}\n";
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
type $wu { \$size:$wide_bytes \$unsigned \$endian:little };

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

union $wov {
   $wu v;
   char bytes[$wide_bytes];
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
$wov $gwide_a;
$wov $gwide_b;
$wov $gwide_p;
$wov $gwide_t;
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

print "void $mul_impl(void) {\n";
print "   if ($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa != $ZERO) {\n      $gr.raw := $ga.raw;\n      return;\n   }\n";
print "   if ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa != $ZERO) {\n      $gr.raw := $gb.raw;\n      return;\n   }\n";
print "   $gsign_out := $ga.bits.sign ^ $gb.bits.sign;\n";
print "   if (($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa == $ZERO && $gb.bits.exponent == $ZERO && $gb.bits.mantissa == $ZERO) || ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa == $ZERO && $ga.bits.exponent == $ZERO && $ga.bits.mantissa == $ZERO)) {\n      $gr.raw := $ZERO;\n      $gr.bits.sign := $ZERO;\n      $gr.bits.exponent := $EXP_MAX;\n      $gr.bits.mantissa := $NAN_PAYLOAD;\n      return;\n   }\n";
print "   if ($ga.bits.exponent == $EXP_MAX && $ga.bits.mantissa == $ZERO) {\n      $gr.raw := $ZERO;\n      $gr.bits.sign := $gsign_out;\n      $gr.bits.exponent := $EXP_MAX;\n      return;\n   }\n";
print "   if ($gb.bits.exponent == $EXP_MAX && $gb.bits.mantissa == $ZERO) {\n      $gr.raw := $ZERO;\n      $gr.bits.sign := $gsign_out;\n      $gr.bits.exponent := $EXP_MAX;\n      return;\n   }\n";
print "   if (($ga.bits.exponent == $ZERO && $ga.bits.mantissa == $ZERO) || ($gb.bits.exponent == $ZERO && $gb.bits.mantissa == $ZERO)) {\n      $gr.raw := $ZERO;\n      $gr.bits.sign := $gsign_out;\n      return;\n   }\n";
print "   $gsig_u.v := $ga.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'   ');
print "   $gsig_a := $gsig_ll.v;\n   $gsig_u.v := $gb.bits.mantissa;\n";
print to_ll_snippet($gsig_ll,$gsig_u,'   ');
print "   $gsig_b := $gsig_ll.v;\n   $gexp_a := $ga.bits.exponent;\n   $gexp_b := $gb.bits.exponent;\n   if ($gexp_a == $M_ZERO) { $gexp_a := $M_ONE; }\n   if ($gexp_b == $M_ZERO) { $gexp_b := $M_ONE; }\n   if ($ga.bits.exponent != $ZERO) { $gsig_a |= $M_HIDDEN; }\n   if ($gb.bits.exponent != $ZERO) { $gsig_b |= $M_HIDDEN; }\n   $gexp_a := $gexp_a + $gexp_b - $M_BIAS;\n   $gtmp_ll.v := $gsig_a;\n";
print ll_to_wide_snippet($gwide_a,$gtmp_ll,'   ');
print "   $gtmp_ll.v := $gsig_b;\n";
print ll_to_wide_snippet($gwide_b,$gtmp_ll,'   ');
print "   $gwide_p.v := $gwide_a.v * $gwide_b.v;\n";
if ($mbits > 3) {
   my $shift = $mbits - 3;
   print "   $gwide_t.v := $gwide_p.v >> $shift;\n";
   print "   $gwide_a.v := $gwide_t.v << $shift;\n";
   print "   if ($gwide_a.v != $gwide_p.v) { $gwide_t.v |= $W_ONE; }\n";
   print "   $gwide_p.v := $gwide_t.v;\n";
}
elsif ($mbits < 3) {
   my $shift = 3 - $mbits;
   print "   $gwide_p.v := $gwide_p.v << $shift;\n";
}
print wide_to_ll_snippet($gtmp_ll,$gwide_p,'   ');
print "   $gsig_out := $gtmp_ll.v;\n";
print "   while ($gsig_out != $M_ZERO && $gexp_a > $M_ONE && $gsig_out < $M_HIDDEN_EXT) { $gsig_out *= $M_TWO; $gexp_a--; }\n";
print "   while ($gsig_out != $M_ZERO && $gexp_a < $M_ONE) { $matht lost; lost := $gsig_out & $M_ONE; $gsig_out /= $M_TWO; if (lost != $M_ZERO) { $gsig_out |= $M_ONE; } $gexp_a++; }\n";
print pack_snippet($gsig_out,$gexp_a,$gsign_out,$gr,"$gmant_ll.v",$gtmp_u,$gtmp_ll);
print "}\n\n";

print "$typename operator+($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $add_impl();\n";
print store_code($gr,'rv');
print "}\n\n";

print "$typename operator-($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $gb.bits.sign ^= $ONE;\n   $add_impl();\n";
print store_code($gr,'rv');
print "}\n\n";

print "$typename operator*($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $mul_impl();\n";
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
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp == $CMP_LT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator>($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp == $CMP_GT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator<=($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp != $CMP_GT) { return 1`bool; }\n   return 0`bool;\n}\n\n";
print "bool operator>=($typename lhs, $typename rhs) {\n";
print load_code($ga,'lhs','av');
print load_code($gb,'rhs','bv');
print "   $cmp_impl();\n   if ($gunordered != 0) { return 0`bool; }\n   if ($gcmp != $CMP_LT) { return 1`bool; }\n   return 0`bool;\n}\n";
