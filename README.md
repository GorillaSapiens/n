# n Toolchain (`n65cc`, `n65asm`, `n65ar`, `n65ld`, `n65sim`)

`n` is a small, experimental C-like programming language designed for simplicity, low-level clarity, and eventual embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation... good for teaching, systems tinkering, or writing your own language from scratch.

This repository contains the `n` language compiler (`n65cc`) plus a companion 6502 assembler (`n65asm`), archiver (`n65ar`), linker (`n65ld`), simulator (`n65sim`), and support libraries.

## Tool CLI Notes

The command-line tools are being aligned with the usual GCC/binutils habits:

- `n65cc` compiles `input.n` to `-o output.s`
- `n65asm` takes a positional input file and uses `-o output.o65` for relocatable object output, similar to GNU `as`
- `n65ar` accepts GNU-`ar` style operation strings such as `rcs`
- `n65ld` accepts GNU-`ld` style `-o`, `-T`, and `-Map`

Examples:

```sh
n65cc -I test -o sieve.s test/sieve.n
n65asm -I libraries/nlib/ -o sieve.o65 sieve.s
n65ld -o sieve.hex sieve.o65 libraries/nlib/nlib.a65
n65sim sieve.hex
```

## Testing

Run `make test` at the repository root to execute both the compiler-side source tests in `test/test.pl` and the end-to-end `n65cc -> n65asm -> n65ld -> n65sim` regression tests in `test/e2e.pl`.
