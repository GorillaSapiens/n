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
- rich opcode-table support via bundled `default.cfg`, optional `illegals.cfg`, and user-supplied opcode config files
- raw `opXX` opcode tokens for hand-written illegal or undocumented opcode includes
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

### Opcode-table options

#### `--opcode-cfg <file>`

Load an additional opcode configuration file after the bundled `default.cfg`. May be repeated. Later files can extend or override earlier mnemonic ... mode mappings.

```sh
n65asm --opcode-cfg cpu65c02.cfg -o program.o65 program.s
```

#### `--illegals`

Load the bundled `illegals.cfg` in addition to the always-loaded `default.cfg`. This enables named unofficial or illegal opcodes such as `LAX`, `SAX`, `DCP`, `ISC`, `SLO`, `RLA`, `SRE`, and `RRA`.

```sh
n65asm --illegals --hex=program.hex program.s
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

During assembly, `n65asm` prints a per-pass progress summary in plain English. For example:

```text
pass 001 layout: bytes 2697, instructions 1147, directives 46, labels 19, constants 1, zero-page 0, absolute 438, long branches 7, still relaxable 3, errors 0
pass 001 after relaxation: bytes 2466 (-231), instructions 1147, directives 46, labels 19, constants 1, zero-page 222 (+222), absolute 216 (-222), long branches 4 (-3), still relaxable 0 (-3), errors 0
pass 004 stable: bytes 2463, instructions 1147, directives 46, labels 19, constants 1, zero-page 222, absolute 216, long branches 3, still relaxable 0, errors 0
pass 999 final emission: bytes 2463, instructions 1147, directives 46, labels 19, constants 1, zero-page 222, absolute 216, long branches 3, still relaxable 0, errors 0
```

The signed deltas show what changed since the previous line, so it is easier to see when relaxation actually saved bytes or collapsed long branches.

This means `.include` and macros are **source-level features**, not parser-level features.

## Syntax

### Rich opcode support

`n65asm` now loads its opcode table from config files:

- `default.cfg` is always loaded automatically from the assembler directory
- `--illegals` additionally loads `illegals.cfg`
- `--opcode-cfg <file>` loads one or more extra opcode tables

Opcode config files are line-oriented and use this syntax:

```text
MNEMONIC MODE OPCODE
```

For example:

```text
LDA imm  $A9
LDA zp   $A5
LDA abs  $AD
LAX imm  $AB
```

Supported mode names are:

- `imp`, `acc`, `imm`
- `zp`, `zpx`, `zpy`
- `abs`, `absx`, `absy`
- `ind`, `indx`, `indy`
- `rel`

Opcode bytes may be written as plain hex, `$xx`, or `0xXX`. Blank lines and lines beginning with `#` or `;` are ignored.

Once a mnemonic is present in the loaded opcode tables, the assembler picks the opcode byte from the parsed addressing mode and emits an error if that mnemonic has no mapping for the requested mode.

```asm
LDA #$42      ; uses LDA imm from default.cfg
LAX $10,Y     ; requires --illegals or an extra opcode config file
```

### `.def` textual aliases

`.def` performs a simple source-level textual replacement on identifier boundaries before lexing/parsing.

```asm
.def sp _nl_sp
.def XYZ LDA
```

That means opcode aliases work too:

```asm
XYZ #$42     ; same as LDA #$42
```

The replacement text runs to end-of-line (before any `;` comment), so it can also expand to other identifiers or tokens, not just a single bare word. The substitution is not applied inside strings or comments.

### Why `illegals.cfg` omits some unofficial opcodes

The bundled `illegals.cfg` is intentionally **not** a complete catalog of every known unofficial 6502 opcode encoding.

The current rich-opcode model is:

- one mnemonic
- one addressing mode
- one opcode byte

That works well for most unofficial mnemonics such as `LAX`, `SAX`, `DCP`, `ISC`, `SLO`, `RLA`, `SRE`, and `RRA`, but it does **not** represent families where the same mnemonic has multiple opcode bytes for the **same** addressing mode.

#### Why `HLT` is missing

`HLT` (also called `KIL` or `JAM` in some references) is the clearest example. It has many implied-mode encodings, including:

```text
$02 $12 $22 $32 $42 $52 $62 $72 $92 $B2 $D2 $F2
```

Under the current config model, a single entry such as:

```text
HLT imp $02
```

would only keep one encoding and would silently discard the rest. Rather than pretend there is one canonical `HLT` byte, the bundled config leaves `HLT` out.

#### Other omissions for the same reason

Other unofficial encodings are omitted or only partially represented for similar reasons:

- the unofficial `NOP` family, because multiple unofficial `NOP` bytes exist for the same modes, while official `NOP imp $EA` already exists in `default.cfg`
- duplicate same-mode aliases such as unofficial `SBC imm $EB`, because official `SBC imm $E9` already occupies that mnemonic ... mode slot in `default.cfg`
- duplicate same-mode encodings such as `ANC`, where only one immediate encoding is named in `illegals.cfg`

So `illegals.cfg` is best understood as a **useful named subset** of unofficial opcodes that fit the current table shape honestly.

#### Workarounds when the config model is too small

When you need an omitted unofficial encoding, there are two practical escape hatches.

##### 1. Use raw `opXX`

This is the most direct workaround, and it is why raw `opXX` support remains available:

```asm
op02              ; one HLT/KIL/JAM encoding
op12              ; another HLT/KIL/JAM encoding
opEB #$42         ; unofficial SBC immediate encoding
op1A              ; one of the unofficial NOP encodings
```

Use explicit addressing-mode suffixes when the operand shape would otherwise be ambiguous:

```asm
op0C.a $1234      ; absolute NOP form
op14.zx $20       ; zero-page,X NOP form
```

##### 2. Use `.def` as a source-level alias

If you want a nicer local spelling, `.def` can alias an identifier to a raw opcode token or to an existing mnemonic:

```asm
.def HLT op02
.def NOPABS op0C.a
.def XYZ LDA

HLT
NOPABS $1234
XYZ #$42
```

This is still only a textual alias, not a second rich-opcode table. It is useful for private include files or one-off projects where you are willing to choose the exact encoding yourself.

### Raw `opXX` opcode form

The assembler still accepts `opXX` where `XX` is a hexadecimal opcode byte:

```asm
opA9 #$42        ; emit opcode $A9 with immediate operand
op8D.a $1234     ; emit opcode $8D with explicit absolute operand size
opF0 target      ; conditional-branch opcodes still infer relative mode
```

This remains useful for hand-written opcode includes, duplicate unofficial encodings, or one-off bytes that you do not want to name in a config file. For ambiguous operand shapes, use the existing mode suffixes (`.z`, `.zx`, `.zy`, `.a`, `.ax`, `.ay`, `.i`, `.ix`, `.iy`) instead of expecting relaxation to rescue you later.

See the source and tests for the full directive and expression surface.
