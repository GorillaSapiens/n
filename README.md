# n Compiler (nc)

`n` is a small, experimental C-like programming language designed for simplicity, low-level clarity, and eventual embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation — making it ideal for teaching, systems tinkering, or just writing your own language from scratch.

This repository contains the lexer, parser, and AST generation logic for `n`.
It also contains a 6502 assembler, archiver, linker, simulator, and
support libraries.


## Testing

Run `make test` at the repository root to execute both the compiler-side source tests in `test/test.pl` and the end-to-end `nc -> na -> nl -> ns` regression tests in `test/e2e.pl`.
