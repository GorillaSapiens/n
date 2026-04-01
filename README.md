# n Toolchain (`n65driver`, `n65cc`, `n65asm`, `n65ar`, `n65ld`, `n65sim`)

`n` is a small, experimental C-like programming language designed for simplicity, low-level clarity, and eventual embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation... good for teaching, systems tinkering, or writing your own language from scratch.

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

## Testing

Run `make test` at the repository root to execute the unified `test/test.pl` harness across both compiler-side source tests and end-to-end `n65cc -> n65asm -> n65ld -> n65sim` regression tests. Use `make unit` for compile-only cases, `make e2e` for end-to-end cases, and `make sieve` for a quick `n65driver` smoke build.

# Additional Details

For additional details, see the README.md files in the various subdirectories.

## Licensing

Unless a subdirectory says otherwise, the toolchain sources and top-level build/test glue are licensed under GPL-3.0-or-later.
The runtime libraries in `libraries/nlib/` and `libraries/nint/` are licensed under BSD-2-Clause so code linked into user binaries stays permissive.
The exact license texts live in the repository root `LICENSE`/`COPYING` files and in the per-library `LICENSE` files.

## Floating-point layout flags

The compiler now accepts explicit float layout flags in type declarations with the form `$float:SExMy`, where the bit layout is always sign/exponent/mantissa from most-significant bit to least-significant bit.

Examples:

```n
type half   { $size:2 $endian:little $float:SE5M10  }; // IEEE 754 binary16
type float  { $size:4 $endian:little $float:SE8M23  }; // IEEE 754 binary32
type double { $size:8 $endian:little $float:SE11M52 }; // IEEE 754 binary64
```

Bare `$float` still uses the compiler's default IEEE-style layout for the supported sizes.
