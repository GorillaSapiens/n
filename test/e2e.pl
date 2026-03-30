#!/usr/bin/perl

use strict;
use warnings;
use File::Spec;
use File::Temp qw(tempdir);
use Cwd qw(abs_path);

my $FAIL = "\e[31mFAIL\e[0m";
my $PASS = "\e[32mpass\e[0m";

my $test_root = abs_path('.');
my $repo_root = abs_path(File::Spec->catdir($test_root, '..'));
my $cases_root = File::Spec->catdir($test_root, 'e2e');

my $n65cc  = File::Spec->catfile($repo_root, 'compiler', 'n65cc');
my $n65asm = File::Spec->catfile($repo_root, 'assembler', 'n65asm');
my $n65ld  = File::Spec->catfile($repo_root, 'linker', 'n65ld');
my $n65ar  = File::Spec->catfile($repo_root, 'archiver', 'n65ar');
my $n65sim = File::Spec->catfile($repo_root, 'simulator', 'n65sim');
my $nlib = File::Spec->catfile($repo_root, 'libraries', 'nlib', 'nlib.a65');
my $nlib_inc = File::Spec->catdir($repo_root, 'libraries', 'nlib');

for my $tool ($n65cc, $n65asm, $n65ld, $n65ar, $n65sim, $nlib) {
   if (!-e $tool) {
      die "[$FAIL] missing required file: $tool\n";
   }
}

sub slurp_file {
   my ($path) = @_;
   open(my $fh, '<', $path) or die "[$FAIL] could not open $path: $!\n";
   local $/;
   my $data = <$fh>;
   close($fh);
   return $data;
}

sub parse_case_directives {
   my ($main_path) = @_;
   my %meta = (
      expectsim => [],
      expecterr => [],
      expectlinkerr => [],
      expectmap => [],
      archive   => [],
      object    => [],
      linkcfg   => undef,
      expectfail => 0,
      expectlinkfail => 0,
   );

   open(my $fh, '<', $main_path) or die "[$FAIL] could not open $main_path: $!\n";
   while (my $line = <$fh>) {
      last if $line !~ m{^//};
      $line =~ s/[\x0a\x0d]//g;
      if ($line =~ m{^//\s*expectsim:\s*(.*?)\s*$}) {
         push @{$meta{expectsim}}, $1;
      }
      elsif ($line =~ m{^//\s*expecterr:\s*(.*?)\s*$}) {
         push @{$meta{expecterr}}, $1;
      }
      elsif ($line =~ m{^//\s*expectlinkerr:\s*(.*?)\s*$}) {
         push @{$meta{expectlinkerr}}, $1;
      }
      elsif ($line =~ m{^//\s*archive:\s*(.*?)\s*$}) {
         push @{$meta{archive}}, $1;
      }
      elsif ($line =~ m{^//\s*object:\s*(.*?)\s*$}) {
         push @{$meta{object}}, $1;
      }
      elsif ($line =~ m{^//\s*expectmap:\s*(.*?)\s*$}) {
         push @{$meta{expectmap}}, $1;
      }
      elsif ($line =~ m{^//\s*linkcfg:\s*(.*?)\s*$}) {
         $meta{linkcfg} = $1;
      }
      elsif ($line =~ m{^//\s*expectfail\s*$}) {
         $meta{expectfail} = 1;
      }
      elsif ($line =~ m{^//\s*expectlinkfail\s*$}) {
         $meta{expectlinkfail} = 1;
      }
   }
   close($fh);
   return \%meta;
}

sub run_cmd {
   my ($cmd, $out_path, $err_path) = @_;
   my $pid = fork();
   die "[$FAIL] fork failed: $!\n" if !defined $pid;

   if ($pid == 0) {
      open(STDOUT, '>', $out_path) or die "open stdout failed: $!\n";
      open(STDERR, '>', $err_path) or die "open stderr failed: $!\n";
      exec @$cmd;
      die "exec failed for @$cmd: $!\n";
   }

   waitpid($pid, 0);
   return ($? >> 8, $? & 127);
}

sub run_cmd_with_timeout {
   my ($cmd, $out_path, $err_path, $seconds) = @_;
   my $pid = fork();
   die "[$FAIL] fork failed: $!\n" if !defined $pid;

   if ($pid == 0) {
      open(STDOUT, '>', $out_path) or die "open stdout failed: $!\n";
      open(STDERR, '>', $err_path) or die "open stderr failed: $!\n";
      exec @$cmd;
      die "exec failed for @$cmd: $!\n";
   }

   my $timed_out = 0;
   my ($exit_code, $signal) = (0, 0);
   eval {
      local $SIG{ALRM} = sub { die "TIMEOUT\n"; };
      alarm $seconds;
      waitpid($pid, 0);
      alarm 0;
      $exit_code = $? >> 8;
      $signal = $? & 127;
   };
   if ($@) {
      if ($@ eq "TIMEOUT\n") {
         $timed_out = 1;
         kill 'TERM', $pid;
         select(undef, undef, undef, 0.2);
         kill 'KILL', $pid;
         waitpid($pid, 0);
         $exit_code = $? >> 8;
         $signal = $? & 127;
      }
      else {
         die $@;
      }
   }

   return ($timed_out, $exit_code, $signal);
}

sub require_substrings {
   my ($haystack, $needles, $label, $case_name, $log_path) = @_;
   for my $needle (@$needles) {
      if (index($haystack, $needle) < 0) {
         print "[$FAIL] $case_name missing $label fragment: $needle\n";
         print "$log_path\n";
         exit(-1);
      }
   }
}

opendir(my $dh, $cases_root) or die "[$FAIL] could not open $cases_root: $!\n";
my @cases = sort grep { -d File::Spec->catdir($cases_root, $_) && !/^\./ } readdir($dh);
closedir($dh);

for my $case (@cases) {
   my $case_dir = File::Spec->catdir($cases_root, $case);
   my $main_src = File::Spec->catfile($case_dir, 'main.n');
   next if !-f $main_src;

   my $meta = parse_case_directives($main_src);
   my $tmp = tempdir("n_e2e_${case}_XXXX", TMPDIR => 1, CLEANUP => 1);
   my @compiled_objects;
   my @archives;

   my $main_s   = File::Spec->catfile($tmp, 'main.s');
   my $main_o65 = File::Spec->catfile($tmp, 'main.o65');
   my $hex_path = File::Spec->catfile($tmp, 'out.hex');
   my $map_path = File::Spec->catfile($tmp, 'out.map');

   my $compile_out = File::Spec->catfile($tmp, 'compile.out');
   my $compile_err = File::Spec->catfile($tmp, 'compile.err');
   my @compile_cmd = ($n65cc, '-quiet', '-I', $case_dir, '-I', $test_root, $main_src, '-o', $main_s, '-dumpbase', 'main.n', '-dumpbase-ext', '.n', '-dumpdir', $tmp);
   my ($compile_exit) = run_cmd(\@compile_cmd, $compile_out, $compile_err);
   my $compile_stderr = slurp_file($compile_err);

   if ($meta->{expectfail}) {
      if ($compile_exit == 0) {
         print "[$FAIL] $case expected failure but compiler exited 0\n";
         print join(' ', @compile_cmd), "\n";
         exit(-1);
      }
      require_substrings($compile_stderr, $meta->{expecterr}, 'stderr', $case, $compile_err);
      print "[$PASS] $case\n";
      next;
   }

   if ($compile_exit != 0) {
      print "[$FAIL] $case compiler exit code $compile_exit\n";
      print join(' ', @compile_cmd), "\n";
      print $compile_stderr;
      exit(-1);
   }

   my $asm_out = File::Spec->catfile($tmp, 'main_asm.out');
   my $asm_err = File::Spec->catfile($tmp, 'main_asm.err');
   my @asm_cmd = ($n65asm, '-I', $nlib_inc, '-o', $main_o65, $main_s);
   my ($asm_exit) = run_cmd(\@asm_cmd, $asm_out, $asm_err);
   if ($asm_exit != 0) {
      print "[$FAIL] $case assembler exit code $asm_exit\n";
      print join(' ', @asm_cmd), "\n";
      print slurp_file($asm_err);
      exit(-1);
   }
   push @compiled_objects, $main_o65;

   for my $obj_src_name (@{$meta->{object}}) {
      my ($stem) = $obj_src_name =~ /^(.*)\.n$/;
      my $src_path = File::Spec->catfile($case_dir, $obj_src_name);
      my $s_path   = File::Spec->catfile($tmp, "$stem.s");
      my $o_path   = File::Spec->catfile($tmp, "$stem.o65");
      my $out_path = File::Spec->catfile($tmp, "$stem.compile.out");
      my $err_path = File::Spec->catfile($tmp, "$stem.compile.err");
      my @cmd = ($n65cc, '-quiet', '-I', $case_dir, '-I', $test_root, $src_path, '-o', $s_path, '-dumpbase', $obj_src_name, '-dumpbase-ext', '.n', '-dumpdir', $tmp);
      my ($exit) = run_cmd(\@cmd, $out_path, $err_path);
      if ($exit != 0) {
         print "[$FAIL] $case extra object compile exit code $exit\n";
         print join(' ', @cmd), "\n";
         print slurp_file($err_path);
         exit(-1);
      }
      my @acmd = ($n65asm, '-I', $nlib_inc, '-o', $o_path, $s_path);
      my ($aexit) = run_cmd(\@acmd, File::Spec->catfile($tmp, "$stem.asm.out"), File::Spec->catfile($tmp, "$stem.asm.err"));
      if ($aexit != 0) {
         print "[$FAIL] $case extra object assemble exit code $aexit\n";
         print join(' ', @acmd), "\n";
         print slurp_file(File::Spec->catfile($tmp, "$stem.asm.err"));
         exit(-1);
      }
      push @compiled_objects, $o_path;
   }

   for my $arc_src_name (@{$meta->{archive}}) {
      my ($stem) = $arc_src_name =~ /^(.*)\.n$/;
      my $src_path = File::Spec->catfile($case_dir, $arc_src_name);
      my $s_path   = File::Spec->catfile($tmp, "$stem.s");
      my $o_path   = File::Spec->catfile($tmp, "$stem.o65");
      my $a_path   = File::Spec->catfile($tmp, "$stem.a65");
      my @ccmd = ($n65cc, '-quiet', '-I', $case_dir, '-I', $test_root, $src_path, '-o', $s_path, '-dumpbase', $arc_src_name, '-dumpbase-ext', '.n', '-dumpdir', $tmp);
      my ($cexit) = run_cmd(\@ccmd, File::Spec->catfile($tmp, "$stem.compile.out"), File::Spec->catfile($tmp, "$stem.compile.err"));
      if ($cexit != 0) {
         print "[$FAIL] $case archive member compile exit code $cexit\n";
         print join(' ', @ccmd), "\n";
         print slurp_file(File::Spec->catfile($tmp, "$stem.compile.err"));
         exit(-1);
      }
      my @acmd = ($n65asm, '-I', $nlib_inc, '-o', $o_path, $s_path);
      my ($aexit) = run_cmd(\@acmd, File::Spec->catfile($tmp, "$stem.asm.out"), File::Spec->catfile($tmp, "$stem.asm.err"));
      if ($aexit != 0) {
         print "[$FAIL] $case archive member assemble exit code $aexit\n";
         print join(' ', @acmd), "\n";
         print slurp_file(File::Spec->catfile($tmp, "$stem.asm.err"));
         exit(-1);
      }
      my @ncmd = ($n65ar, 'rcs', $a_path, $o_path);
      my ($nexit) = run_cmd(\@ncmd, File::Spec->catfile($tmp, "$stem.n65ar.out"), File::Spec->catfile($tmp, "$stem.n65ar.err"));
      if ($nexit != 0) {
         print "[$FAIL] $case archive creation exit code $nexit\n";
         print join(' ', @ncmd), "\n";
         print slurp_file(File::Spec->catfile($tmp, "$stem.n65ar.err"));
         exit(-1);
      }
      push @archives, $a_path;
   }

   my @link_cmd = ($n65ld, '-o', $hex_path, '-Map', $map_path);
   if (defined $meta->{linkcfg}) {
      push @link_cmd, '-T', File::Spec->catfile($case_dir, $meta->{linkcfg});
   }
   push @link_cmd, @compiled_objects, @archives, $nlib;
   my $link_out = File::Spec->catfile($tmp, 'link.out');
   my $link_err = File::Spec->catfile($tmp, 'link.err');
   my ($link_exit) = run_cmd(\@link_cmd, $link_out, $link_err);
   my $link_stderr = slurp_file($link_err);
   if ($meta->{expectlinkfail}) {
      if ($link_exit == 0) {
         print "[$FAIL] $case expected link failure but linker exited 0\n";
         print join(' ', @link_cmd), "\n";
         exit(-1);
      }
      require_substrings($link_stderr, $meta->{expectlinkerr}, 'link stderr', $case, $link_err);
      print "[$PASS] $case\n";
      next;
   }
   if ($link_exit != 0) {
      print "[$FAIL] $case linker exit code $link_exit\n";
      print join(' ', @link_cmd), "\n";
      print $link_stderr;
      exit(-1);
   }

   if (@{$meta->{expectmap}}) {
      my $map_text = slurp_file($map_path);
      require_substrings($map_text, $meta->{expectmap}, 'map', $case, $map_path);
   }

   my $sim_out = File::Spec->catfile($tmp, 'sim.out');
   my $sim_err = File::Spec->catfile($tmp, 'sim.err');
   my @sim_cmd = ('stdbuf', '-o0', $n65sim, $hex_path);
   my ($timed_out, $sim_exit, $sim_sig) = run_cmd_with_timeout(\@sim_cmd, $sim_out, $sim_err, 2);
   my $sim_stdout = slurp_file($sim_out);
   my $sim_stderr = slurp_file($sim_err);

   if (!$timed_out && $sim_exit != 0) {
      print "[$FAIL] $case simulator exit code $sim_exit signal $sim_sig\n";
      print join(' ', @sim_cmd), "\n";
      print $sim_stderr;
      exit(-1);
   }

   require_substrings($sim_stdout, $meta->{expectsim}, 'simulator output', $case, $sim_out);
   print "[$PASS] $case\n";
}

my $asm_smoke_tmp = tempdir("n_e2e_asm_smoke_XXXX", TMPDIR => 1, CLEANUP => 1);
my $asm_src = File::Spec->catfile($asm_smoke_tmp, 'rich.s');
my $asm_hex = File::Spec->catfile($asm_smoke_tmp, 'rich.hex');
my $asm_out = File::Spec->catfile($asm_smoke_tmp, 'rich.out');
my $asm_err = File::Spec->catfile($asm_smoke_tmp, 'rich.err');
open(my $asmfh, '>', $asm_src) or die "[$FAIL] could not write $asm_src: $!\n";
print $asmfh <<'EOF_ASM';
.segmentdef "CODE", $8000, $0100
.segment "CODE"
.def XYZ LDA
.proc demo
   XYZ #$12
   LAX #$34
   op8D.a $1234
   opF0 target
   opEA
target:
   opEA
.endproc
EOF_ASM
close($asmfh);
my @asm_smoke_cmd = ($n65asm, '--illegals', '--hex=' . $asm_hex, $asm_src);
my ($asm_smoke_exit) = run_cmd(\@asm_smoke_cmd, $asm_out, $asm_err);
if ($asm_smoke_exit != 0) {
   print "[$FAIL] assembler rich-opcode smoke exit code $asm_smoke_exit\n";
   print join(' ', @asm_smoke_cmd), "\n";
   print slurp_file($asm_err);
   exit(-1);
}
my $asm_hex_text = slurp_file($asm_hex);
if (index($asm_hex_text, 'A912AB348D3412F001EAEA') < 0) {
   print "[$FAIL] assembler rich-opcode smoke missing expected bytes\n";
   print "$asm_hex\n";
   exit(-1);
}
print "[$PASS] assembler_rich_opcode_smoke\n";

my $asm_bad_src = File::Spec->catfile($asm_smoke_tmp, 'rich_bad.s');
my $asm_bad_hex = File::Spec->catfile($asm_smoke_tmp, 'rich_bad.hex');
my $asm_bad_out = File::Spec->catfile($asm_smoke_tmp, 'rich_bad.out');
my $asm_bad_err = File::Spec->catfile($asm_smoke_tmp, 'rich_bad.err');
open(my $badfh, '>', $asm_bad_src) or die "[$FAIL] could not write $asm_bad_src: $!\n";
print $badfh <<'EOF_BAD';
.segmentdef "CODE", $8000, $0100
.segment "CODE"
.proc demo
   LAX ($44)
.endproc
EOF_BAD
close($badfh);
my @asm_bad_cmd = ($n65asm, '--illegals', '--hex=' . $asm_bad_hex, $asm_bad_src);
my ($asm_bad_exit) = run_cmd(\@asm_bad_cmd, $asm_bad_out, $asm_bad_err);
if ($asm_bad_exit == 0) {
   print "[$FAIL] assembler rich-opcode illegal-mode smoke unexpectedly succeeded\n";
   print join(' ', @asm_bad_cmd), "\n";
   exit(-1);
}
my $asm_bad_text = slurp_file($asm_bad_err);
if (index($asm_bad_text, 'illegal addressing mode for LAX') < 0) {
   print "[$FAIL] assembler rich-opcode illegal-mode smoke missing expected diagnostic\n";
   print "$asm_bad_err\n";
   exit(-1);
}
print "[$PASS] assembler_rich_opcode_illegal_mode_smoke\n";
