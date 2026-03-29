# n65asm

## Overview

`n65asm` is a custom two-pass 6502 assembler with:

- Intel HEX output
- relocatable o65 object output
- listing file output
- map output
- recursive `.include`
- expression evaluation
- labels and constants
- local labels
- addressing-mode specifiers
- relaxation from absolute-family encodings to zero-page-family encodings where legal
- simple source-level macros
- simple source-level textual aliases with `.def`
- multi-error reporting

It can operate in two primary modes:

- **relocatable object generation** with o65 output
- **final binary assembly** with Intel HEX output

Listing and map output can be requested alongside either mode.

## Command Line Parameters

`n65asm` now follows the usual GNU `as` shape more closely:

- the input source is a positional operand
- `-o` selects the primary relocatable object output
- if no primary output is requested, the assembler writes an o65 object to `a.out`

### Usage

```sh
n65asm [options] file
```

### Primary options

#### `file`

Input assembly source file.

#### `-o <file>`, `--output <file>`

Write relocatable o65 object output to `<file>`.

```sh
n65asm -o program.o65 program.s
```

If neither `-o` nor `--hex` is given, `n65asm` still writes relocatable output, using the GNU-`as` style default name `a.out`.

```sh
n65asm program.s
```

#### `-I <dir>`, `--include <dir>`

Add a directory to the include search path. May be repeated.

```sh
n65asm -I common -I board -o program.o65 program.s
```

### Auxiliary outputs

These outputs are optional and keep the existing n65-specific spelling.

#### `--hex[=file]`

Write Intel HEX output. If no filename is supplied, the name is derived from the input path with a `.hex` extension.

```sh
n65asm --hex program.s
n65asm --hex=program.hex program.s
```

#### `--lst[=file]`

Write a listing file. If no filename is supplied, the name is derived from the input path with a `.lst` extension.

```sh
n65asm --lst program.s
n65asm --lst=program.lst program.s
```

#### `--map[=file]`

Write a map file. If no filename is supplied, the name is derived from the input path with a `.map` extension.

```sh
n65asm --map program.s
n65asm --map=program.map program.s
```

### Compatibility aliases

These older forms are still accepted so existing scripts do not instantly catch fire.

#### `-i <file>`, `--input <file>`

Compatibility alias for the positional input file.

```sh
n65asm --input program.s --lst
```

#### `--o65[=file]`

Compatibility alias for object output.

- `n65asm --o65 program.s` writes `program.o65`
- `n65asm --o65=custom.o65 program.s` writes `custom.o65`

```sh
n65asm --o65 program.s
n65asm --o65=program.o65 program.s
```

### Help

#### `-h`, `--help`

Show usage and exit.

```sh
n65asm --help
```

## Examples

Generate a default object file named `a.out`:

```sh
n65asm program.s
```

Generate a named object file:

```sh
n65asm -o program.o65 program.s
```

Generate Intel HEX plus listing and map files using derived names:

```sh
n65asm --hex --lst --map program.s
```

Generate every output explicitly:

```sh
n65asm -o out.o65 --hex=out.hex --lst=out.lst --map=out.map test.s
```

## Behavior Notes

### Optional argument syntax

For `--hex`, `--lst`, `--map`, and `--o65`, use the `=` form when supplying an optional filename:

```sh
n65asm --hex=program.hex --lst=program.lst --map=program.map --o65=program.o65 program.s
```

Avoid relying on a space-separated optional value such as:

```sh
n65asm --hex program.hex program.s
```

With `getopt_long()`, that extra token may be treated as a positional operand instead of an option value.

### Output defaults

- No primary output flags: write relocatable o65 output to `a.out`
- `-o <file>`: write relocatable o65 output to `<file>`
- `--o65` without a filename: write relocatable o65 output to `<input>.o65`
- `--hex`, `--lst`, `--map` without filenames: derive names from the input path

## Source File Processing Order

The assembler preprocesses the root source before lexing/parsing:

1. `.include` expansion
2. macro definition collection
3. macro expansion
4. marker insertion for original file/line preservation
5. lexing/parsing
6. pass 1 layout
7. relaxation
8. pass 2 emission

This means `.include` and macros are **source-level features**, not parser-level features.

## Syntax

See the source and tests for the full directive and expression surface. The command-line changes above do not alter assembly syntax.
