# driver (`n65driver`)

`n65driver` is a small GCC-like front-end for the `n` 6502 toolchain.
It sits above `n65cc`, `n65asm`, and `n65ld` and invokes them in the usual compile ... assemble ... link pipeline, much like `gcc` drives `cc1`, `as`, and `ld`.

## What it does

`n65driver` understands the most useful high-level build modes:

- compile and link by default
- `-c` to stop after producing `.o65`
- `-S` to stop after producing assembly
- `-I`, `-L`, and `-l` in the usual GCC style
- `-T` and `-Map` passthrough for the linker
- `-Wc,...`, `-Wa,...`, `-Wl,...` and `-Xcompiler`, `-Xassembler`, `-Xlinker` for stage-specific arguments
- `-v` and `-###` to print the subordinate commands

By default it links `libraries/nlib/nlib.a65` unless `-nostdlib` is used.

## What it requires

`n65driver` is only a coordinator.
It needs the rest of the toolchain plus the default runtime archive.

When run from the built repository tree, it finds:

- `compiler/n65cc`
- `assembler/n65asm`
- `linker/n65ld`
- `archiver/n65ar` (only for path reporting via `-print-prog-name=ar`)
- `simulator/n65sim` (only for path reporting via `-print-prog-name=sim`)
- `libraries/nlib/nlib.a65` for default linking

When installed, it expects this layout under the same prefix:

- `bin/n65driver`, `bin/n65cc`, `bin/n65asm`, `bin/n65ld`, `bin/n65ar`, `bin/n65sim`
- `lib/n/nlib.a65`
- `include/n/nlib.inc` for the assembler's implicit runtime include path

So the same binary works both from the source tree and from an installed prefix without extra path flags.

## Input kinds

`n65driver` classifies inputs by suffix:

- `.n` ... compile with `n65cc`
- `.s` or `.asm` ... assemble with `n65asm`
- `.o65` ... pass directly to `n65ld`
- `.a65` ... pass directly to `n65ld`

## Examples

Build and link a program:

```sh
./driver/n65driver -I test test/sieve.n -o sieve.hex
```

Compile only:

```sh
./driver/n65driver -c -I libraries/nlib demo.n
```

Stop after assembly:

```sh
./driver/n65driver -S demo.n
```

Link extra archives from a search directory:

```sh
./driver/n65driver crt0.o65 main.o65 -L libraries/nlib -lnlib -nostdlib -o app.hex
```

Show the exact subordinate commands without running them:

```sh
./driver/n65driver -### -I test test/sieve.n -o sieve.hex
```

## Intentional non-goals

This is not a full `gcc` clone.
It does not try to emulate every GCC switch, preprocess separately, or manage every obscure language mode.
It just covers the normal compile/assemble/link flow without making you type three commands every time.
