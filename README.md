# n Toolchain (`n65driver`, `n65cc`, `n65asm`, `n65ar`, `n65ld`, `n65sim`)

`n` is a small C-like programming language designed for simplicity, low-level clarity, and embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation... good for teaching, systems tinkering, or writing your own language from scratch.

This repository contains the `n` language compiler (`n65cc`) plus a companion 6502 assembler (`n65asm`), archiver (`n65ar`), linker (`n65ld`), simulator (`n65sim`), a GCC-like driver (`n65driver`), and support libraries.

## Tool CLI Notes

The command-line tools are being aligned with the usual GCC/binutils habits:

- `n65driver` is the high-level GCC-like entry point; it drives `n65cc`, `n65asm`, and `n65ld` for the normal compile/assemble/link flow
- `n65cc` now accepts a GCC-`cc1`-style single input file anywhere on the line, uses `-o output.s`, and accepts `-quiet`, `-dumpbase`, `-dumpbase-ext`, and `-dumpdir` as compatibility flags
- `n65asm` takes a positional input file and uses `-o output.o65` for relocatable object output, similar to GNU `as`; it auto-loads `assembler/default.cfg`, can add `assembler/illegals.cfg` with `--illegals`, supports extra opcode tables with `--opcode-cfg`, and still keeps `.def` aliases plus raw `opXX` tokens
- `n65ar` accepts GNU-`ar` style operation strings such as `rcs`
- `n65ld` accepts GNU-`ld` style `-o`, `-T`, and `-Map`

Examples:

High-level driver flow:

```sh
n65driver -I test test/sieve.n -o sieve.hex
n65sim sieve.hex
```

Direct stage-by-stage flow:

```sh
n65cc -quiet -I test test/sieve.n -o sieve.s -dumpbase sieve.n -dumpbase-ext .n -dumpdir ./
n65asm -I libraries/nlib/ -o sieve.o65 sieve.s
n65ld -o sieve.hex sieve.o65 libraries/nlib/nlib.a65
n65sim sieve.hex
```


## Installing

The tree now supports staged installs and relocatable packaging:

```sh
make install PREFIX=/usr/local
make install DESTDIR=/tmp/n-pkg PREFIX=/usr/local
make uninstall PREFIX=/usr/local
make package PACKAGE_PREFIX=/usr/local
```

Installed layout:

- `$(PREFIX)/bin/` ... `n65driver`, `n65cc`, `n65asm`, `n65ar`, `n65ld`, `n65sim`
- `$(PREFIX)/lib/n/` ... default runtime archives such as `nlib.a65` and `nint.a65`
- `$(PREFIX)/include/n/` ... `nlib.h` and `nlib.inc`
- `$(PREFIX)/share/n/` ... packaged library/config/source extras such as `nlib/n.cfg`, `float/gen.pl`, and `vcs/` files

The installed `n65driver` will first use the built source-tree layout when run from the repository, and otherwise will find sibling installed tools in `bin/` plus the default runtime assets under `lib/n/` and `include/n/`.

## Testing

Run `make test` at the repository root to execute the unified `test/test.pl` harness across both compiler-side source tests and end-to-end `n65cc -> n65asm -> n65ld -> n65sim` regression tests. Use `make unit` for compile-only cases, `make e2e` for end-to-end cases, and `make sieve` for a quick `n65driver` smoke build.

`test/test.pl` is now the one runner for both `.n` source tests and generic `.test` wrapper tests. It does not stop at the first failure, shows progress for every case, and prints a final summary of all failures. You can also run one file, a few files, or a whole subdirectory directly, for example:

```sh
cd test
./test.pl weak_builtin_operator_codegen_test.n
./test.pl --compile-only exactops_visible_operator_codegen_test.n
./test.pl --e2e-only e2e_generated_float_archive_exactops_verify.n
```

See `test/README.md` for the header directives, placeholder tokens, and the generic `.test` file format.

# Additional Details

For additional details, see the README.md files in the various subdirectories.

## Licensing

Unless a subdirectory says otherwise, the toolchain sources and top-level build/test glue are licensed under GPL-3.0-or-later.
The runtime libraries in `libraries/nlib/` and `libraries/nint/` are licensed under BSD-2-Clause so code linked into user binaries stays permissive.
The exact license texts live in the repository root `LICENSE`/`COPYING` files and in the per-library `LICENSE` files.

## Floating-point style flags

Float types now use a style-based flag: `$float:ieee754` or `$float:simple`.

Examples:

```n
type half   { $size:2 $endian:little $float:ieee754 }; // IEEE 754 binary16
type float  { $size:4 $endian:little $float:ieee754 }; // IEEE 754 binary32
type double { $size:8 $endian:little $float:ieee754 }; // IEEE 754 binary64
type f3     { $size:3 $endian:little $float:simple  }; // generic simple SExMy format
```

`$float:ieee754` supports only `$size:2`, `$size:4`, and `$size:8`.
`$float:simple` supports any positive size and always uses an `SExMy` layout where `x = round(3 * log2(size) + 2)` and `y` is the remaining fraction bits. For `$size:2`, `$size:4`, and `$size:8`, that yields the same exponent widths as IEEE 754 binary16/binary32/binary64.

`libraries/float/gen.pl` can generate a full exact-operator surface for a float-like type using union/bitfield `SExMy` arithmetic instead of the old nlib float helpers. In classic single-file mode it emits the operator definitions only, so the including translation unit must declare the matching type and should mark it `$exactops` to get the same compile-time contract. In build mode it emits a generated type declaration with `$exactops` plus exact overload declarations for binary `+ - * /`, unary `+ -`, `== != < > <= >=`, `operator{}` truthiness, and `++ --`. Run `perl libraries/float/gen.pl typename little-or-big size-bytes exp-bits > mytype_ops.n` for monolithic output, or use `--build outdir ...` for archive-friendly split output.

If a type should use exact declared-type operator names only, add `$exactops` to the `type` declaration. Without `$exactops`, same-type operators fall back to the generic builtin lowering when no visible exact overload is available. With `$exactops`, the compiler requires visible exact overloads for the operators you actually use.
