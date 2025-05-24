# 6502 Arbitrary-Length Integer Math Library

This library provides routines to perform arithmetic, bitwise, and comparison operations on little-endian arbitrary-length integers (signed and unsigned), optimized for modularity and correctness. Each routine accepts pointers to input/output buffers and a byte count in X.

Compatible with ca65 and other standard 6502 assemblers.

## Modules
- `add.asm` – Addition
- `sub.asm` – Subtraction
- `mul.asm` – Multiplication
- `div.asm` – Division (quotient + remainder)
- `rem.asm` – Remainder-only
- `shift.asm` – Left/Right shifts
- `cmp.asm` – Comparison operators
- `incdec.asm` – Increment/Decrement
- `bitwise.asm` – AND/OR/NOT

Each module contains both arbitrary-width and fast-path versions for 8/16/24/32/64-bit integers.
