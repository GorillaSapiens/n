# n Toolchain (`n65cc`, `n65asm`, `n65ar`, `n65ld`, `n65sim`)

`n` is a small, experimental C-like programming language designed for simplicity, low-level clarity, and eventual embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation — making it ideal for teaching, systems tinkering, or just writing your own language from scratch.

This repository contains the `n` language compiler (`n65cc`) plus a companion 6502 assembler (`n65asm`), archiver (`n65ar`), linker (`n65ld`), simulator (`n65sim`), and support libraries.


## Testing

Run `make test` at the repository root to execute both the compiler-side source tests in `test/test.pl` and the end-to-end `n65cc -> n65asm -> n65ld -> n65sim` regression tests in `test/e2e.pl`.
