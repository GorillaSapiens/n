#!/usr/bin/perl

use strict;
use warnings;
use File::Spec;
use File::Temp qw(tempdir tempfile);
use File::Path qw(make_path);
use Cwd qw(abs_path);
use Getopt::Long qw(GetOptions);

my $FAIL = "\e[31mFAIL\e[0m";
my $PASS = "\e[32mpass\e[0m";

my $compile_only = 0;
my $e2e_only = 0;
GetOptions(
   'compile-only' => \$compile_only,
   'e2e-only' => \$e2e_only,
) or die "usage: $0 [--compile-only] [--e2e-only]\n";
die "usage: $0 [--compile-only] [--e2e-only]\n" if $compile_only && $e2e_only;

my $test_root = abs_path('.');
my $repo_root = abs_path(File::Spec->catdir($test_root, '..'));

my $n65cc  = File::Spec->catfile($repo_root, 'compiler', 'n65cc');
my $n65asm = File::Spec->catfile($repo_root, 'assembler', 'n65asm');
my $n65ld  = File::Spec->catfile($repo_root, 'linker', 'n65ld');
my $n65ar  = File::Spec->catfile($repo_root, 'archiver', 'n65ar');
my $n65sim = File::Spec->catfile($repo_root, 'simulator', 'n65sim');
my $nlib = File::Spec->catfile($repo_root, 'libraries', 'nlib', 'nlib.a65');
my $nlib_inc = File::Spec->catdir($repo_root, 'libraries', 'nlib');

sub slurp_file {
   my ($path) = @_;
   open(my $fh, '<', $path) or die "[$FAIL] could not open $path: $!\n";
   local $/;
   my $data = <$fh>;
   close($fh);
   return $data;
}

sub first_line {
   my ($path) = @_;
   open(my $fh, '<', $path) or die "[$FAIL] could not open $path: $!\n";
   my $line = <$fh>;
   close($fh);
   $line = '' if !defined $line;
   $line =~ s/[\x0a\x0d]//g;
   return $line;
}

sub parse_runner {
   my ($file, $runner_line) = @_;
   if (!($runner_line =~ m{^//\s*n65cc\b})) {
      return undef;
   }
   if ($runner_line =~ /[;`]/) {
      die "[$FAIL] $file :: hey! no sneaky shell shenanigans !!!\n";
   }
   $runner_line =~ s{^//\s*n65cc\b}{ };
   $runner_line =~ s/^\s+//;
   $runner_line =~ s/\s+$//;
   return grep { length($_) } split(/\s+/, $runner_line);
}

sub parse_directives {
   my ($path) = @_;
   my %meta = (
      expectasm => [],
      expectasmordered => [],
      expecterr => [],
      expectsim => [],
      expectlinkerr => [],
      expectmap => [],
      archive => [],
      archivegroup => [],
      object => [],
      linkcfg => undef,
      expectfail => 0,
      expectlinkfail => 0,
      expectexit => undef,
   );

   open(my $fh, '<', $path) or die "[$FAIL] could not open $path: $!\n";
   while (my $line = <$fh>) {
      $line =~ s/[\x0a\x0d]//g;
      if ($line =~ /^\/\/ expectasm:\s*(.*?)\s*$/) {
         push @{$meta{expectasm}}, $1;
      }
      elsif ($line =~ /^\/\/ expectasmordered:\s*(.*?)\s*$/) {
         push @{$meta{expectasmordered}}, $1;
      }
      elsif ($line =~ /^\/\/ expecterr:\s*(.*?)\s*$/) {
         push @{$meta{expecterr}}, $1;
      }
      elsif ($line =~ /^\/\/ expectsim:\s*(.*?)\s*$/) {
         push @{$meta{expectsim}}, $1;
      }
      elsif ($line =~ /^\/\/ expectlinkerr:\s*(.*?)\s*$/) {
         push @{$meta{expectlinkerr}}, $1;
      }
      elsif ($line =~ /^\/\/ expectmap:\s*(.*?)\s*$/) {
         push @{$meta{expectmap}}, $1;
      }
      elsif ($line =~ /^\/\/ archive:\s*(.*?)\s*$/) {
         push @{$meta{archive}}, $1;
      }
      elsif ($line =~ /^\/\/ archivegroup:\s*(\S+)\s+(.*?)\s*$/) {
         my ($group, $rest) = ($1, $2);
         my @members = grep { length($_) } split(/\s+/, $rest);
         push @{$meta{archivegroup}}, [$group, @members];
      }
      elsif ($line =~ /^\/\/ object:\s*(.*?)\s*$/) {
         push @{$meta{object}}, $1;
      }
      elsif ($line =~ /^\/\/ linkcfg:\s*(.*?)\s*$/) {
         $meta{linkcfg} = $1;
      }
      elsif ($line =~ /^\/\/ expectfail\s*$/) {
         $meta{expectfail} = 1;
      }
      elsif ($line =~ /^\/\/ expectlinkfail\s*$/) {
         $meta{expectlinkfail} = 1;
      }
      elsif ($line =~ /^\/\/ expectexit:\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*$/) {
         my $value = $1;
         $meta{expectexit} = ($value =~ /^0x/i) ? hex($value) : int($value);
      }
   }
   close($fh);
   return \%meta;
}

sub is_e2e_case {
   my ($meta) = @_;
   return scalar(@{$meta->{expectsim}})
      || scalar(@{$meta->{expectlinkerr}})
      || scalar(@{$meta->{expectmap}})
      || scalar(@{$meta->{archive}})
      || scalar(@{$meta->{archivegroup}})
      || scalar(@{$meta->{object}})
      || defined($meta->{linkcfg})
      || $meta->{expectlinkfail}
      || defined($meta->{expectexit});
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

sub run_compile_case {
   my ($file, $runner_args, $meta) = @_;

   my ($outfh, $outfile) = tempfile('test_output_XXXX', UNLINK => 1);
   my ($errfh, $errfile) = tempfile('test_error_XXXX', UNLINK => 1);
   close($outfh);
   close($errfh);

   my @cmd = ($n65cc, '-quiet', @$runner_args, $file);
   my ($exit_code) = run_cmd(\@cmd, $outfile, $errfile);

   if ($meta->{expectfail}) {
      if ($exit_code == 0) {
         print "[$FAIL] $file expected failure but exited 0\n";
         print join(' ', @cmd), "\n";
         exit(-1);
      }
   }
   elsif ($exit_code != 0) {
      print "[$FAIL] $file exit code $exit_code\n";
      print join(' ', @cmd), "\n";
      exit(-1);
   }

   if (@{$meta->{expectasm}} || @{$meta->{expectasmordered}}) {
      my $asm = slurp_file($outfile);
      require_substrings($asm, $meta->{expectasm}, 'assembly', $file, $outfile);
      my $start = 0;
      for my $needle (@{$meta->{expectasmordered}}) {
         my $pos = index($asm, $needle, $start);
         if ($pos < 0) {
            print "[$FAIL] $file missing ordered assembly fragment: $needle\n";
            print "$outfile\n";
            exit(-1);
         }
         $start = $pos + length($needle);
      }
   }

   if (@{$meta->{expecterr}}) {
      my $stderr = slurp_file($errfile);
      require_substrings($stderr, $meta->{expecterr}, 'stderr', $file, $errfile);
   }

   print "[$PASS] $file\n";
}

sub compile_n_to_object {
   my ($src_name, $runner_args, $tmp, $test_name) = @_;
   my ($stem) = $src_name =~ /^(.*)\.n$/;
   my $src_path = File::Spec->catfile($test_root, $src_name);
   my $s_path   = File::Spec->catfile($tmp, "$stem.s");
   my $o_path   = File::Spec->catfile($tmp, "$stem.o65");
   my $out_path = File::Spec->catfile($tmp, "$stem.compile.out");
   my $err_path = File::Spec->catfile($tmp, "$stem.compile.err");
   for my $path ($s_path, $o_path, $out_path, $err_path) {
      my ($vol, $dir, undef) = File::Spec->splitpath($path);
      make_path(File::Spec->catpath($vol, $dir, '')) if length($dir);
   }
   my @cmd = ($n65cc, '-quiet', @$runner_args, $src_path, '-o', $s_path, '-dumpbase', $src_name, '-dumpbase-ext', '.n', '-dumpdir', $tmp);
   my ($exit_code) = run_cmd(\@cmd, $out_path, $err_path);
   if ($exit_code != 0) {
      print "[$FAIL] $test_name extra compile exit code $exit_code\n";
      print join(' ', @cmd), "\n";
      print slurp_file($err_path);
      exit(-1);
   }
   my @asm_cmd = ($n65asm, '-I', $nlib_inc, '-o', $o_path, $s_path);
   my ($asm_exit) = run_cmd(\@asm_cmd, File::Spec->catfile($tmp, "$stem.asm.out"), File::Spec->catfile($tmp, "$stem.asm.err"));
   if ($asm_exit != 0) {
      print "[$FAIL] $test_name extra assemble exit code $asm_exit\n";
      print join(' ', @asm_cmd), "\n";
      print slurp_file(File::Spec->catfile($tmp, "$stem.asm.err"));
      exit(-1);
   }
   return $o_path;
}

sub run_e2e_case {
   my ($file, $runner_args, $meta) = @_;
   for my $tool ($n65cc, $n65asm, $n65ld, $n65ar, $n65sim, $nlib) {
      if (!-e $tool) {
         die "[$FAIL] missing required file: $tool\n";
      }
   }

   my $tmp = tempdir("n_e2e_${file}_XXXX", TMPDIR => 1, CLEANUP => 1);
   my @compiled_objects;
   my @archives;

   my $src_path = File::Spec->catfile($test_root, $file);
   my ($stem) = $file =~ /^(.*)\.n$/;
   my $main_s   = File::Spec->catfile($tmp, "$stem.s");
   my $main_o65 = File::Spec->catfile($tmp, "$stem.o65");
   my $hex_path = File::Spec->catfile($tmp, 'out.hex');
   my $map_path = File::Spec->catfile($tmp, 'out.map');

   my $compile_out = File::Spec->catfile($tmp, 'compile.out');
   my $compile_err = File::Spec->catfile($tmp, 'compile.err');
   my @compile_cmd = ($n65cc, '-quiet', @$runner_args, $src_path, '-o', $main_s, '-dumpbase', $file, '-dumpbase-ext', '.n', '-dumpdir', $tmp);
   my ($compile_exit) = run_cmd(\@compile_cmd, $compile_out, $compile_err);
   my $compile_stderr = slurp_file($compile_err);

   if ($meta->{expectfail}) {
      if ($compile_exit == 0) {
         print "[$FAIL] $file expected failure but compiler exited 0\n";
         print join(' ', @compile_cmd), "\n";
         exit(-1);
      }
      require_substrings($compile_stderr, $meta->{expecterr}, 'stderr', $file, $compile_err);
      print "[$PASS] $file\n";
      return;
   }

   if ($compile_exit != 0) {
      print "[$FAIL] $file compiler exit code $compile_exit\n";
      print join(' ', @compile_cmd), "\n";
      print $compile_stderr;
      exit(-1);
   }

   my @asm_cmd = ($n65asm, '-I', $nlib_inc, '-o', $main_o65, $main_s);
   my ($asm_exit) = run_cmd(\@asm_cmd, File::Spec->catfile($tmp, 'main_asm.out'), File::Spec->catfile($tmp, 'main_asm.err'));
   if ($asm_exit != 0) {
      print "[$FAIL] $file assembler exit code $asm_exit\n";
      print join(' ', @asm_cmd), "\n";
      print slurp_file(File::Spec->catfile($tmp, 'main_asm.err'));
      exit(-1);
   }
   push @compiled_objects, $main_o65;

   for my $obj_src_name (@{$meta->{object}}) {
      push @compiled_objects, compile_n_to_object($obj_src_name, $runner_args, $tmp, $file);
   }

   for my $arc_src_name (@{$meta->{archive}}) {
      my ($stem2) = $arc_src_name =~ /^(.*)\.n$/;
      my $o_path = compile_n_to_object($arc_src_name, $runner_args, $tmp, $file);
      my $a_path = File::Spec->catfile($tmp, "$stem2.a65");
      my @ncmd = ($n65ar, 'rcs', $a_path, $o_path);
      my ($nexit) = run_cmd(\@ncmd, File::Spec->catfile($tmp, "$stem2.n65ar.out"), File::Spec->catfile($tmp, "$stem2.n65ar.err"));
      if ($nexit != 0) {
         print "[$FAIL] $file archive creation exit code $nexit\n";
         print join(' ', @ncmd), "\n";
         print slurp_file(File::Spec->catfile($tmp, "$stem2.n65ar.err"));
         exit(-1);
      }
      push @archives, $a_path;
   }

   if (@{$meta->{archivegroup}}) {
      my %groups;
      for my $entry (@{$meta->{archivegroup}}) {
         my ($group, @members) = @$entry;
         push @{$groups{$group}}, @members;
      }
      for my $group (sort keys %groups) {
         my @group_objects;
         for my $src_name (@{$groups{$group}}) {
            push @group_objects, compile_n_to_object($src_name, $runner_args, $tmp, $file);
         }
         my $archive_name = $group;
         $archive_name .= '.a65' if $archive_name !~ /\.a65$/;
         my $a_path = File::Spec->catfile($tmp, $archive_name);
         my @ncmd = ($n65ar, 'rcs', $a_path, @group_objects);
         my ($nexit) = run_cmd(\@ncmd, File::Spec->catfile($tmp, "$group.n65ar.out"), File::Spec->catfile($tmp, "$group.n65ar.err"));
         if ($nexit != 0) {
            print "[$FAIL] $file archivegroup creation exit code $nexit
";
            print join(' ', @ncmd), "
";
            print slurp_file(File::Spec->catfile($tmp, "$group.n65ar.err"));
            exit(-1);
         }
         push @archives, $a_path;
      }
   }

   my @link_cmd = ($n65ld, '-o', $hex_path, '-Map', $map_path);
   if (defined $meta->{linkcfg}) {
      push @link_cmd, '-T', File::Spec->catfile($test_root, $meta->{linkcfg});
   }
   push @link_cmd, @compiled_objects, @archives, $nlib;
   my $link_out = File::Spec->catfile($tmp, 'link.out');
   my $link_err = File::Spec->catfile($tmp, 'link.err');
   my ($link_exit) = run_cmd(\@link_cmd, $link_out, $link_err);
   my $link_stderr = slurp_file($link_err);
   if ($meta->{expectlinkfail}) {
      if ($link_exit == 0) {
         print "[$FAIL] $file expected link failure but linker exited 0\n";
         print join(' ', @link_cmd), "\n";
         exit(-1);
      }
      require_substrings($link_stderr, $meta->{expectlinkerr}, 'link stderr', $file, $link_err);
      print "[$PASS] $file\n";
      return;
   }
   if ($link_exit != 0) {
      print "[$FAIL] $file linker exit code $link_exit\n";
      print join(' ', @link_cmd), "\n";
      print $link_stderr;
      exit(-1);
   }

   if (@{$meta->{expectmap}}) {
      my $map_text = slurp_file($map_path);
      require_substrings($map_text, $meta->{expectmap}, 'map', $file, $map_path);
   }

   my $sim_out = File::Spec->catfile($tmp, 'sim.out');
   my $sim_err = File::Spec->catfile($tmp, 'sim.err');
   my @sim_cmd = ('stdbuf', '-o0', $n65sim, $hex_path);
   my ($timed_out, $sim_exit, $sim_sig) = run_cmd_with_timeout(\@sim_cmd, $sim_out, $sim_err, 2);
   my $sim_stdout = slurp_file($sim_out);
   my $sim_stderr = slurp_file($sim_err);

   if (defined $meta->{expectexit}) {
      if ($timed_out) {
         print "[$FAIL] $file simulator timed out before expected exit $meta->{expectexit}\n";
         print join(' ', @sim_cmd), "\n";
         print $sim_stderr;
         exit(-1);
      }
      if ($sim_exit != $meta->{expectexit}) {
         print "[$FAIL] $file simulator exit code $sim_exit signal $sim_sig expected $meta->{expectexit}\n";
         print join(' ', @sim_cmd), "\n";
         print $sim_stderr;
         exit(-1);
      }
   }
   elsif ($sim_exit != 0) {
      print "[$FAIL] $file simulator exit code $sim_exit signal $sim_sig\n";
      print join(' ', @sim_cmd), "\n";
      print $sim_stderr;
      exit(-1);
   }

   require_substrings($sim_stdout, $meta->{expectsim}, 'simulator output', $file, $sim_out);
   print "[$PASS] $file\n";
}

sub run_assembler_smoke_tests {
   return if $compile_only;

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
}

opendir(my $dh, $test_root) or die "[$FAIL] could not open $test_root: $!\n";
my @files = sort grep { /\.n$/ && -f File::Spec->catfile($test_root, $_) } readdir($dh);
closedir($dh);

for my $file (@files) {
   my $runner_line = first_line($file);
   next if !defined parse_runner($file, $runner_line);
   my @runner_args = parse_runner($file, $runner_line);
   my $meta = parse_directives($file);
   my $e2e = is_e2e_case($meta);
   next if $compile_only && $e2e;
   next if $e2e_only && !$e2e;
   if ($e2e) {
      run_e2e_case($file, \@runner_args, $meta);
   }
   else {
      run_compile_case($file, \@runner_args, $meta);
   }
}

run_assembler_smoke_tests();
