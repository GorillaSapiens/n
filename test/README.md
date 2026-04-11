# Test harness notes

`test/test.pl` is the single test runner for this tree.

It runs two kinds of files:

- `.n` source tests ... compiler-only checks by default, or full end-to-end `n65cc -> n65asm -> n65ld -> n65sim` when the header requests link/sim behavior
- `.test` script-style tests ... generic command wrappers driven entirely by header comments

## Common usage

Run the whole suite from `test/`:

```sh
./test.pl
```

Run only compile-side checks:

```sh
./test.pl --compile-only
```

Run only end-to-end and generic runtime tests:

```sh
./test.pl --e2e-only
```

Run one test, a few tests, or a whole subdirectory:

```sh
./test.pl weak_builtin_operator_codegen_test.n
./test.pl exactops_visible_operator_codegen_test.n e2e_generated_float_archive_exactops_verify.n
./test.pl .
```

The runner does not stop at the first failure. It prints per-test progress and summarizes all failures at the end.

## Header-driven behavior

The harness reads leading comment lines from each test file.

### `.n` tests

Most `.n` tests use the first header line to describe the compile command, for example:

```n
// n65cc -I .
```

Useful expectations include:

- `expectasm:` / `expectasmordered:` / `forbidasm:` ... search the emitted assembly
- `expecterr:` / `forbiderr:` ... search compiler stderr
- `expectfail` ... compilation should fail
- `expectexit:` ... run the full e2e pipeline and require a simulator exit code
- `archive:` / `archivegroup:` / `object:` ... extra link inputs for e2e cases
- `linkcfg:` / `simcfg:` / `simargs:` ... linker and simulator extras
- `phase: compile|e2e|any` ... force how the runner classifies the test

A plain `.n` file with only compile-side expectations is treated as a compile-only test. A `.n` file with link/sim expectations is treated as an e2e test.

### `.test` files

Generic tests use an explicit runner command:

```text
# runner: n65asm --illegals --hex=@TMP@/rich.hex @TEST_ROOT@/assembler_rich_opcode_smoke.s
# expectstdout: wrote
# expectexit: 0
```

Useful placeholders in `runner:` and related directives:

- `@REPO@` ... repository root
- `@TEST_ROOT@` ... `test/` directory
- `@FILE@` ... current test file
- `@FILEDIR@` ... directory containing the current test file
- `@TMP@` ... per-test temporary work directory
- `@NLIB@` ... default `libraries/nlib/nlib.a65`
- `@NLIB_INC@` ... default `libraries/nlib/` include directory

Useful generic expectations include:

- `expectstdout:` / `expectstdoutordered:` / `forbidstdout:`
- `expectstderr:` / `expectstderrordered:` / `forbidstderr:`
- `expectstdoutexact:` / `expectstderrexact:`
- `expectfile:` / `forbidfile:`
- `expectexit:`

## Generated float fixtures

Some tests read the generated float archive fixture files under `test/generated_float_archive_*`.
`test/Makefile` and `test/test.pl` both ensure those fixtures are refreshed before the suite runs, so compile-only and e2e runs see the same generated inputs.

Generated float declarations now use `$exactops`, and the generated operator surface includes binary `+ - * /`, unary `+ -`, comparisons, `operator{}` truthiness, and `++ --`. The exactops-focused tests in this directory check both the generated declarations and the runtime archive behavior.
