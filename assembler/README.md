# n65asm

## Overview

`n65asm` is a custom two-pass 6502 assembler with:

- Intel HEX output
- listing file output
- recursive `.include`
- expression evaluation
- labels and constants
- local labels
- addressing-mode specifiers
- relaxation from absolute-family encodings to zero-page-family encodings where legal
- simple source-level macros
- simple source-level textual aliases with `.def`
- multi-error reporting

It can operate in two modes:

- **final binary assembly** with Intel HEX output
- **relocatable object generation** with o65 output

The Intel HEX path still behaves like a final binary assembler. The o65 path emits relocatable object files with imported/exported symbols and relocation records.

---

## Command Line Parameters

This assembler uses flag-based command line options.

### Usage

```sh
n65asm -i <input.s> [--hex[=file]] [--lst[=file]] [--map[=file]] [--o65[=file]]
```

If assembly fails, the assembler returns nonzero and suppresses final HEX output.

### Required Parameters

#### `-i <file>`, `--input <file>`

Specifies the input assembly source file.

This parameter is required.

##### Example

```sh
n65asm -i program.s --hex
```

### Optional Output Parameters

Each output type is disabled unless its flag is provided.

#### `--hex[=file]`

Enables Intel HEX output.

- If a filename is provided, that exact file is used.
- If no filename is provided, the output filename is derived from the input filename using the `.hex` extension.

##### Examples

```sh
n65asm -i program.s --hex
n65asm -i program.s --hex=program.hex
n65asm -i src/program.s --hex
```

Derived filename examples:

- `program.s` -> `program.hex`
- `src/program.s` -> `src/program.hex`

#### `--lst[=file]`

Enables listing output.

- If a filename is provided, that exact file is used.
- If no filename is provided, the output filename is derived from the input filename using the `.lst` extension.

##### Examples

```sh
n65asm -i program.s --lst
n65asm -i program.s --lst=program.lst
```

Derived filename examples:

- `program.s` -> `program.lst`

#### `--o65[=file]`

Enables relocatable **o65 object** output.

- If a filename is provided, that exact file is used.
- If no filename is provided, the output filename is derived from the input filename using the `.o65` extension.

##### Examples

```sh
n65asm -i program.s --o65
n65asm -i program.s --o65=program.o65
```

Derived filename examples:

- `program.s` -> `program.o65`

#### `--map[=file]`

Enables map output.

- If a filename is provided, that exact file is used.
- If no filename is provided, the output filename is derived from the input filename using the `.map` extension.

##### Examples

```sh
n65asm -i program.s --map
n65asm -i program.s --map=program.map
```

Derived filename examples:

- `program.s` -> `program.map`

### Help Parameter

#### `-h`, `--help`

Displays command line usage information and exits.

##### Example

```sh
n65asm --help
```

### Behavior Notes

#### Optional argument syntax

For output options with optional filenames, use the `=` form when supplying a filename:

```sh
n65asm -i program.s --hex=program.hex --lst=program.lst --map=program.map --o65=program.o65
```

Do not rely on this form:

```sh
n65asm -i program.s --hex program.hex
```

That may be interpreted as a positional argument instead of an optional value, depending on how `getopt_long()` parses the command line.

#### No positional output arguments

This interface does not use positional output filenames. Output files are controlled entirely by flags.

#### Output generation is selective

Only the outputs explicitly requested are generated.

For example:

```sh
n65asm -i program.s --hex
```

generates only the HEX file.

```sh
n65asm -i program.s --lst --map
```

generates only the listing and map files.

### Examples

#### Generate only HEX output

```sh
n65asm -i test.s --hex
```

#### Generate HEX, listing, and map using default derived names

```sh
n65asm -i test.s --hex --lst --map
```

#### Generate all outputs with explicit filenames

```sh
n65asm -i test.s --hex=out.hex --lst=out.lst --map=out.map --o65=out.o65
```

#### Generate only an o65 object file

```sh
n65asm -i test.s --o65
```

#### Generate only a listing file

```sh
n65asm --input test.s --lst
```

### Summary Table

| Parameter | Meaning | Required |
|---|---|---|
| `-i <file>` | Input source file | Yes |
| `--input <file>` | Input source file | Yes |
| `--hex[=file]` | Enable HEX output, optional filename | No |
| `--lst[=file]` | Enable listing output, optional filename | No |
| `--map[=file]` | Enable map output, optional filename | No |
| `--o65[=file]` | Enable relocatable o65 object output, optional filename | No |
| `-h` | Show help | No |
| `--help` | Show help | No |

---

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

---

# Syntax

## Comments

A semicolon starts a comment:

```asm
LDA #1      ; load constant
```

---

## Labels

A label ends with `:`.

```asm
start:
   LDA #1
```

Labels may appear alone on a line or attached to a statement.

```asm
loop: DEX
```

---

## Local Labels

Local labels begin with `@` and are scoped to the most recent non-local label.

```asm
start:
@loop:
   DEX
   BNE @loop

next:
   NOP
```

Behavior:

- `@loop` inside `start` is different from `@loop` inside `next`
- before any global label, local labels live in the root scope
- local labels can be used in expressions too

Example:

```asm
start:
@here:
ptr = @here + 1
.word @here
```

Internally, local labels are stored as scoped names like:

```text
start::@loop
```

but source code always uses the plain local form.

---

## Constants / Symbols

Constants use:

```asm
name = expr
```

Examples:

```asm
foo = $20
bar = foo + 1
here = *
```

Notes:

- constants are symbols
- constants may reference labels, earlier constants, and `*`
- `here = *` captures the location at the point where the constant is defined
- constants and labels share one namespace

So this is illegal:

```asm
foo = $20
foo:
   NOP
```

and this is also illegal:

```asm
start:
   NOP
start:
   BRK
```

---

## Instructions

Basic instruction forms:

```asm
NOP
LDA #1
LDA value
LDA value,X
LDA value,Y
LDA (ptr)
LDA (ptr,X)
LDA (ptr),Y
```

Accumulator instructions may be written either way:

```asm
ASL
ASL A
LSR
LSR A
ROL
ROL A
ROR
ROR A
```

These assemble as accumulator mode.

---

## Directives

Currently supported directives:

- `.org`
- `.byte`
- `.word`
- `.text`
- `.ascii`
- `.include`
- `.def`

### `.def`

Defines a simple source-level textual alias.

```asm
.def sp _nl_sp
.def ptr0 _nl_ptr0
```

This behaves like a lightweight `#define` on identifier boundaries during source expansion.
It is intended for readable aliases of imported or exported symbols, especially zero-page runtime names.

Example:

```asm
.importzp _nl_sp
.def sp _nl_sp

   lda sp
```

The alias is expanded before parsing and expression evaluation, so it can be used with imported symbols where `=` or `.equ` would require a fully resolved value.

Current `.def` behavior:

- applies during source expansion, before parsing
- replaces whole identifiers only
- does not replace inside quoted strings
- does not replace inside comments
- remains in effect for subsequent lines after it is defined
- duplicate `.def` names are rejected

`.def` is for textual aliases, not numeric constants. Use `=` for ordinary assembly-time constants.

---

### `.org`

Sets the location counter.

```asm
.org $8000
```

`.org` must evaluate during pass 1.

---

### `.byte`

Emits one byte per expression.

```asm
.byte $01, $02, 3, 'A, 'B'
```

Character constants may be written with or without the closing tick if your lexer has been configured that way.

---

### `.word`

Emits 16-bit little-endian words.

```asm
.word $1234, target, start + 4
```

---

### `.text` / `.ascii`

Emit string bytes.

```asm
.text "Hello"
.ascii "World"
```

Current behavior is simple byte emission of string contents.

---

### `.include`

Include another source file.

```asm
.include "common/macros.s"
.include "subdir/vectors.s"
.include "/absolute/path/to/file.s"
```

Rules:

- path must be in double quotes
- relative paths are resolved from the including file’s directory
- `.include` must be on its own line
- comments after `.include` are allowed

Example:

```asm
.include "hardware/registers.s" ; pull in constants
```

The assembler preserves original file and line information through includes for diagnostics and listing output.

---

# Expressions

Expressions are allowed in operands, constants, and directive arguments.

## Numeric Forms

### Decimal

```asm
10
255
```

### Hex

```asm
$10
$FF
```

### Binary

```asm
%10101010
```

### Character Constants

```asm
'A
'B'
'\n'
```

Supported escapes include:

- `\n`
- `\r`
- `\t`
- `\0`
- `\'`
- `\"`
- `\\`

---

## Operators

Supported operators:

- unary `-`
- unary `<` low byte
- unary `>` high byte
- binary `+`
- binary `-`
- binary `*`
- binary `/`
- `*` as current location counter

Examples:

```asm
LDA #<target
LDA #>target
.word (table + 4)
.byte -(1 + 2)
```

Operator precedence is the usual arithmetic structure:

1. unary
2. `*` `/`
3. `+` `-`

Grouping with parentheses is supported.

---

## Current Location Counter

`*` means the current assembly location.

Example:

```asm
here = *
.word *
```

Important distinction:

```asm
here = *
.word here
```

If `here` is defined before later code, it keeps the earlier address. It is not re-evaluated at the use site.

---

# Addressing Modes

The parser recognizes these addressing families:

- implied
- accumulator
- immediate
- direct (`zp` or `abs`)
- indexed `,X` (`zp,x` or `abs,x`)
- indexed `,Y` (`zp,y` or `abs,y`)
- indirect
- indexed indirect `(zp,X)`
- indirect indexed `(zp),Y`
- relative (branches)

---

## Addressing Mode Specifiers

You can force a specific encoding by adding a suffix to the mnemonic.

### Supported suffixes

- `.z` ... zero-page
- `.zx` ... zero-page,X
- `.zy` ... zero-page,Y
- `.a` ... absolute
- `.ax` ... absolute,X
- `.ay` ... absolute,Y
- `.i` ... indirect
- `.ix` ... indexed indirect `(zp,X)`
- `.iy` ... indirect indexed `(zp),Y`

Examples:

```asm
LDA.z $20
LDA.a target
LDA.ax table
JMP.i vector
LDA.ix $44
LDA.iy $44
```

### Meaning

A specifier is a **hard requirement**, not a hint.

Examples:

```asm
LDA.z $20       ; ok
LDA.z $1234     ; error
JSR.z target    ; error
JMP.i $1234     ; ok
```

### Surface Syntax Override

Some specifiers are allowed to override normal surface syntax.

These are accepted:

```asm
LDA.ix $44      ; same encoding as LDA ($44,X)
LDA.iy $44      ; same encoding as LDA ($44),Y
JMP.i  $1234    ; same encoding as JMP ($1234)
```

This avoids forcing both suffix and punctuation at the same time.

---

# Relaxation

The assembler implements **shrink-only relaxation**.

## Initial policy

Without an addressing-mode specifier:

- ambiguous direct forms start in a conservative mode
- wide forms are preferred when both wide and narrow encodings exist
- if an opcode only supports one concrete form, that form is used

Examples:

- `LDA value` initially prefers absolute if both zero-page and absolute exist
- `STX value,Y` uses zero-page,Y because 6502 has `STX zp,Y` but not `STX abs,Y`

## Relaxation step

After pass 1 layout:

- `ABS` may shrink to `ZP`
- `ABSX` may shrink to `ZPX`
- `ABSY` may shrink to `ZPY`

only if:

- no explicit mode specifier was used
- the opcode supports the narrower encoding
- the evaluated operand fits in `0..255`

Relaxation is monotonic and shrink-only, so it converges.

## Pass statistics

Each pass prints statistics like:

```text
pass 1 layout     bytes=1234 insns=400 dirs=12 labels=55 consts=20 zp=3 abs=190 errors=0
pass 1 relaxed    bytes=1202 insns=400 dirs=12 labels=55 consts=20 zp=35 abs=158 errors=0
pass 2 stable     bytes=1202 insns=400 dirs=12 labels=55 consts=20 zp=35 abs=158 errors=0
pass 999 emit     bytes=1202 insns=400 dirs=12 labels=55 consts=20 zp=35 abs=158 errors=0
```

---

# Branches

Branches use relative addressing automatically.

Supported branch mnemonics:

- `BCC`
- `BCS`
- `BEQ`
- `BMI`
- `BNE`
- `BPL`
- `BVC`
- `BVS`

The assembler checks branch range and reports an error if the displacement is outside `-128..127`.

---

# Macros

Macros are handled in the source loader before lexing/parsing.

## Definition

```asm
MACRO load16 ptr, value
   LDA #<value
   STA ptr
   LDA #>value
   STA ptr+1
ENDM
```

## Invocation

```asm
load16 $20, target
```

## Expansion behavior

The macro body is expanded into ordinary assembly text before parsing.

### Example

Source:

```asm
MACRO delay_x count
   LDX #count
@loop:
   DEX
   BNE @loop
ENDM

delay_x 10
delay_x 20
```

Expanded conceptually to something like:

```asm
LDX #10
@__M1_loop:
DEX
BNE @__M1_loop

LDX #20
@__M2_loop:
DEX
BNE @__M2_loop
```

Macro-local labels beginning with `@` are renamed uniquely per expansion.

## Current macro limitations

This macro system is intentionally simple:

- source-level only
- parameter substitution on identifier boundaries
- unique renaming for `@local` labels inside macros

Not implemented yet:

- recursion
- conditionals
- default arguments
- variadic arguments
- expression-aware argument typing
- parser-level macro semantics

---

# Symbol Rules

All labels and constants share one symbol namespace.

The assembler reports duplicates with both locations:

```text
file.s:10: duplicate symbol 'foo'
file.s:3: first defined here
```

This applies across:

- label vs label
- constant vs constant
- label vs constant
- constant vs label

---

# Error Handling

The assembler accumulates multiple errors instead of stopping at the first one.

Behavior:

- pass 1 continues after duplicate symbols and unresolved constants
- pass 2 skips emission for a bad statement and continues to later statements
- final HEX output is suppressed if any errors occurred

Diagnostics preserve original filename and line through `.include` expansion.

---

# Output Files

## Intel HEX

The assembler writes Intel HEX format.

Currently used record types:

- data record (`00`)
- EOF record (`01`)

The assembler buffers bytes into a 64K image and dumps that image as Intel HEX after pass 2.

---

## Listing File

The `.lst` output includes:

- original filename
- address
- emitted bytes
- source line number
- reconstructed source statement

Example:

```text
tests/symbols.s          0000  A5 20                  5  LDA foo
tests/symbols.s          0002  A5 21                  6  LDA bar
tests/symbols.s          ----                         7  here = *
```

The listing currently reconstructs source from IR rather than preserving original formatting/comments exactly.

---

# Map File Output

The assembler can write a map file with the extension `.map`.

The map file is a plain text summary of the **final converged assembly layout** after:

- segment placement
- symbol resolution
- constant evaluation
- instruction relaxation

This means the `.map` file reflects the same final addresses used for output generation.

---
## Map File Sections

The map file currently contains two sections:

1. `SEGMENTS`
2. `SYMBOLS`

---

## `SEGMENTS` Section

The `SEGMENTS` section lists every known segment in final sorted order.

The columns are:

- `NAME`
- `BASE`
- `SIZE`
- `END`
- `CAPACITY`

Example:

```text
SEGMENTS
========

NAME                 BASE       SIZE       END        CAPACITY
ZEROPAGE             $00000000  $00000002  $00000002  $00000100
CODE                 $00008000  $0000000A  $0000800A  $00002000
RODATA               $0000800A  $00000006  $00008010  $00001FF6
```

### Meaning of Each Column

#### `NAME`

The segment name.

Examples:

- `ZEROPAGE`
- `CODE`
- `RODATA`
- `BSS`

#### `BASE`

The final absolute base address of the segment.

This is the value established by `.segmentdef`, possibly derived from earlier segment symbols such as `CODE_END`.

#### `SIZE`

The actual number of bytes used in that segment after final layout and relaxation.

This is the same value exported as the auto-generated symbol:

```text
NAME_SIZE
```

#### `END`

The final end address of the used portion of the segment.

This is computed as:

```text
END = BASE + SIZE
```

This is also the value exported as:

```text
NAME_END
```

#### `CAPACITY`

The declared capacity of the segment from `.segmentdef`.

This is the same value exported as:

```text
NAME_CAPACITY
```

Important distinction:

- `SIZE` is the actual used byte count
- `CAPACITY` is the declared maximum/planned space

If `SIZE` exceeds `CAPACITY`, the assembler emits an overflow warning during assembly.

---

## `SYMBOLS` Section

The `SYMBOLS` section lists all final symbols known to the assembler.

The columns are:

- `ADDRESS`
- `NAME`

Example:

```text
SYMBOLS
=======

ADDRESS    NAME
$00000000  ZEROPAGE_BASE
$00000002  ZEROPAGE_SIZE
$00000002  ZEROPAGE_END
$00000100  ZEROPAGE_CAPACITY
$00008000  CODE_BASE
$00008000  start
$0000800A  CODE_END
$0000800A  RODATA_BASE
$00008010  RODATA_END
```

### Symbol Sorting

Symbols are sorted as follows:

1. defined symbols before undefined symbols
2. defined symbols by ascending address
3. ties broken by name

Undefined symbols, if any remain, are shown as:

```text
???????? symbol_name
```

In a successful final assembly, all referenced/imported symbols should normally be resolved.

---

## Included Symbol Types

The map file includes **all symbols currently present in the symbol table**, including:

- user-defined labels
- user-defined constants
- exported/global symbols
- file-local/internal symbols
- auto-generated segment symbols

### Auto-Generated Segment Symbols

For every segment, the assembler generates:

- `NAME_BASE`
- `NAME_SIZE`
- `NAME_END`
- `NAME_CAPACITY`

These appear in the map file just like ordinary symbols.

Example:

```text
CODE_BASE
CODE_SIZE
CODE_END
CODE_CAPACITY
```

This makes the map file useful both for human inspection and for debugging derived segment placement.

---

## Local and Internal Symbol Names

Because the assembler supports scoped local labels and file-local symbol mangling, the map file may contain internal symbol names, such as:

```text
start::@loop
main.s::helper
```

Examples:

- `start::@loop` ... a local label scoped under `start`
- `main.s::helper` ... a file-local top-level symbol in `main.s`

Exported/global symbols appear with their plain public names.

---

## Relationship to the Listing File

The `.lst` file is source-oriented.

It shows:

- source lines
- addresses
- emitted bytes
- reconstructed source text

The `.map` file is layout-oriented.

It shows:

- final segment placement
- final used sizes
- final symbol addresses

So the listing answers:

> what bytes came from what line?

while the map answers:

> where did everything finally land?

---

## Relationship to Segment Symbols

The map file reflects the final values of the same segment symbols that can be used in expressions:

- `NAME_BASE`
- `NAME_SIZE`
- `NAME_END`
- `NAME_CAPACITY`

So if source uses:

```asm
.segmentdef "CODE",   $8000, $2000
.segmentdef "RODATA", CODE_END, $2000 - CODE_SIZE
```

the map file shows the final resolved values of those symbols after relaxation and layout convergence.

---

## Example

Source:

```asm
.segmentdef "ZEROPAGE", $0000, $0100
.segmentdef "CODE",     $8000, $2000
.segmentdef "RODATA",   CODE_END, $2000 - CODE_SIZE

.segment "ZEROPAGE"
ptr:
   .res 2

.segment "CODE"
start:
   LDA #<msg
   STA ptr
   LDA #>msg
   STA ptr+1
   RTS

.segment "RODATA"
msg:
   .asciiz "hello"
```

Possible map output:

```text
SEGMENTS
========

NAME                 BASE       SIZE       END        CAPACITY
ZEROPAGE             $00000000  $00000002  $00000002  $00000100
CODE                 $00008000  $00000009  $00008009  $00002000
RODATA               $00008009  $00000006  $0000800F  $00001FF7

SYMBOLS
=======

ADDRESS    NAME
$00000000  ZEROPAGE_BASE
$00000000  ptr
$00000002  ZEROPAGE_SIZE
$00000002  ZEROPAGE_END
$00000100  ZEROPAGE_CAPACITY
$00008000  CODE_BASE
$00008000  start
$00008009  CODE_SIZE
$00008009  CODE_END
$00008009  RODATA_BASE
$00008009  msg
$0000800F  RODATA_END
```

---

## Practical Uses of the Map File

The `.map` file is useful for:

- checking final addresses of labels and constants
- verifying segment placement
- verifying that derived segment placement resolved the way you expected
- spotting segment overflow conditions
- confirming where local/file-scoped/internal names ended up
- debugging code size changes caused by relaxation

---

## Current Limitations

The map file currently includes **all** symbols in one flat symbol table dump.

That means:

- auto-generated segment symbols are mixed in with user symbols
- internal/file-scoped names are shown directly
- there is not yet a separate “public symbols only” view
- there is not yet a separate subsection for imported/exported symbols

Even so, the current output is complete and accurate, which is usually the part that matters when things are on fire.

---

# Scoping Rules

## Global labels

A non-local label starts a new scope.

```asm
start:
```

## Local labels

A local label belongs to the most recent global label.

```asm
start:
@loop:
```

is scoped under `start`.

Before the first global label, locals belong to a root scope.

---

# Cross-File Symbols and Object-Mode Directives

The assembler supports cross-file symbol directives for relocatable o65 output.

## `.import`

Declares one or more symbols that are **referenced here but defined elsewhere**.

```asm
.import ext1, ext2
```

These symbols may remain undefined during assembly when writing o65 output. The undefined names are emitted in the o65 import table and referenced by relocation records.

## `.export`

Declares one or more symbols that are **defined in this file and should be visible to the linker**.

```asm
.export foo, bar
```

A symbol exported from this file must still be defined somewhere in the file.

## `.global`

Hybrid import/export declaration.

```asm
.global foo, bar
```

A symbol listed in `.global` behaves as follows:

- if it is defined in this file, it is exported
- if it is not defined in this file, it is treated as an import

This matches the common ca65-style convenience behavior.

## Zero-page forms

The assembler also supports explicit zero-page forms:

```asm
.globalzp ptr
.importzp extptr
.exportzp localptr
```

These mark the symbol as zero-page-sized for relaxation and for o65 segment tagging.

## Address-size annotations

The assembler also accepts ca65-style address-size annotations on `.global`, `.import`, and `.export`:

```asm
.global ptr : zp
.import extptr : zp
.export localptr : zeropage
```

Accepted zero-page annotation spellings are:

- `: zp`
- `: zeropage`

These annotations are treated the same as the corresponding `...zp` directive forms.

## Relaxation of imported zero-page symbols

When an imported symbol is declared as zero-page, the assembler may relax legal absolute-family instructions to zero-page-family encodings even though the symbol is unresolved in the current file.

Example:

```asm
.import tableptr : zp

   LDA tableptr
```

This may be emitted as a zero-page load in the final assembled instruction stream.

## `.proc` and `.endproc`

`.proc name` defines `name` at the current location and also opens a scope for local labels until the matching `.endproc`.

```asm
.proc foo
   LDA #0
.endproc
```

In object mode, `.proc` and `.endproc` do not create special object-file records on their own. They affect symbol definition and local-label scoping.

---

# Supported 6502 Opcode Coverage

The assembler currently has opcode table coverage for these standard documented 6502 opcodes:

- `ADC`
- `AND`
- `ASL`
- `BCC`
- `BCS`
- `BEQ`
- `BIT`
- `BMI`
- `BNE`
- `BPL`
- `BRK`
- `BVC`
- `BVS`
- `CLC`
- `CLD`
- `CLI`
- `CLV`
- `CMP`
- `CPX`
- `CPY`
- `DEC`
- `DEX`
- `DEY`
- `EOR`
- `INC`
- `INX`
- `INY`
- `JMP`
- `JSR`
- `LDA`
- `LDX`
- `LDY`
- `LSR`
- `NOP`
- `ORA`
- `PHA`
- `PHP`
- `PLA`
- `PLP`
- `ROL`
- `ROR`
- `RTI`
- `RTS`
- `SBC`
- `SEC`
- `SED`
- `SEI`
- `STA`
- `STX`
- `STY`
- `TAX`
- `TAY`
- `TSX`
- `TXA`
- `TXS`
- `TYA`

Illegal/unofficial opcodes are not part of the documented feature set.

---

# Examples

## Simple program

```asm
.org $8000

start:
   LDA #1
   STA $0200
   RTS
```

## Constants and expressions

```asm
screen = $0400
color  = $D800
msgptr = $20

start:
   LDA #<message
   STA msgptr
   LDA #>message
   STA msgptr+1
   RTS

message:
   .text "HELLO"
```

## Local labels

```asm
start:
   LDX #10
@loop:
   DEX
   BNE @loop

next:
   RTS
```

## Forced addressing modes

```asm
LDA.z $20
LDA.a $20
LDA.ax table
LDA.ix $44
LDA.iy $44
JMP.i vector
```

## Includes

`main.s`:

```asm
.include "hardware.s"
.include "macros.s"

start:
   LDA #1
```

`hardware.s`:

```asm
VIC_BORDER = $D020
```

## Macros

```asm
MACRO load16 ptr, value
   LDA #<value
   STA ptr
   LDA #>value
   STA ptr+1
ENDM

load16 $20, target
```

---
# Segments

The assembler supports **named segments** with independent location counters.

Each segment has:

- a **base address**
- a **declared capacity**
- its own **PC / used size**

This means code and data can be assembled in separate streams and placed at different absolute addresses without pretending everything lives in one flat linear source order.

---

## Segment Directives

### `.segmentdef`

Defines a segment’s placement and capacity.

```asm
.segmentdef "NAME", base_expr, size_expr
```

Parameters:

- `"NAME"` ... segment name in double quotes
- `base_expr` ... absolute base address for the segment
- `size_expr` ... declared capacity of the segment in bytes

Example:

```asm
.segmentdef "ZEROPAGE", $0000, $0100
.segmentdef "CODE",     $8000, $2000
.segmentdef "RODATA",   $A000, $1000
```

Notes:

- segment names are strings, not identifiers
- base and size must evaluate to non-negative values
- a segment that is used but never defined is an error
- the assembler provides an implicit default segment named `__default__` at base `$0000` with capacity `$10000`

---

### `.segment`

Switches the current segment.

```asm
.segment "CODE"
```

All following statements belong to that segment until another `.segment` is encountered.

Example:

```asm
.segment "CODE"
start:
   LDA #1
   RTS

.segment "RODATA"
msg:
   .asciiz "hello"
```

---

## Segment PCs

Each segment has its own location counter.

That means:

- instructions advance the PC of the **current segment**
- `.byte`, `.word`, `.text`, `.ascii`, `.asciiz`, and `.res` advance the PC of the **current segment**
- labels are assigned addresses using the current segment’s base plus its current segment-local offset

So segment layout is independent.

For example:

```asm
.segmentdef "CODE",   $8000, $2000
.segmentdef "RODATA", $A000, $1000

.segment "CODE"
start:
   NOP

.segment "RODATA"
msg:
   .byte 1, 2, 3
```

Results:

- `start = $8000`
- `msg = $A000`

even though `msg` appears later in source.

---

## Segment Overflow Warnings

If a segment’s used size exceeds its declared capacity, the assembler emits a **warning**.

Example:

```asm
.segmentdef "CODE", $8000, $0010
.segment "CODE"

start:
   .res $20
```

This will warn that `CODE` overflowed its declared size.

Important:

- this is currently a **warning**, not a hard error
- assembly continues
- final addresses are still based on the segment base and actual used offsets

---

## `.org` Inside Segments

`.org` works relative to the **current segment base**.

```asm
.segmentdef "CODE", $8000, $2000
.segment "CODE"
.org $8100
```

This sets the `CODE` segment PC to:

```text
$8100 - $8000 = $0100
```

So the next label or emitted byte will appear at absolute address `$8100`.

This is invalid:

```asm
.segmentdef "CODE", $8000, $2000
.segment "CODE"
.org $7000
```

because `$7000` is below the base of the current segment.

---

## Segment Symbols

For every defined segment, the assembler automatically defines four symbols:

- `NAME_BASE`
- `NAME_SIZE`
- `NAME_END`
- `NAME_CAPACITY`

For example, for:

```asm
.segmentdef "CODE", $8000, $2000
```

the assembler defines:

- `CODE_BASE`
- `CODE_SIZE`
- `CODE_END`
- `CODE_CAPACITY`

### Meaning

- `NAME_BASE` ... segment base address
- `NAME_SIZE` ... actual number of bytes currently used by the segment
- `NAME_END` ... `NAME_BASE + NAME_SIZE`
- `NAME_CAPACITY` ... declared size from `.segmentdef`

These are ordinary expression symbols and can be used anywhere expressions are allowed.

Example:

```asm
.segmentdef "CODE",   $8000, $2000
.segmentdef "RODATA", CODE_END, $2000 - CODE_SIZE
```

This places `RODATA` immediately after the used portion of `CODE`, and gives it the remaining space up to a total combined size of `$2000`.

---

## Derived Segment Placement

Because segment symbols are available as expressions, segment bases and capacities can depend on earlier segments.

Example:

```asm
.segmentdef "CODE",   $8000, $2000
.segmentdef "RODATA", CODE_END, $0800
.segmentdef "BSS",    RODATA_END, $0400
```

This creates a packed layout where each segment starts immediately after the previous segment’s used bytes.

You can also derive capacities:

```asm
.segmentdef "CODE",   $8000, $2000
.segmentdef "RODATA", CODE_END, $2000 - CODE_SIZE
```

That means:

- `RODATA_BASE = CODE_END`
- `RODATA_CAPACITY = $2000 - CODE_SIZE`

---

## Relaxation and Segment Symbols

Instruction relaxation and derived segment placement interact.

Because the assembler can shrink some instructions from absolute-family encodings to zero-page-family encodings, segment sizes may change during relaxation. That means symbols like:

- `CODE_SIZE`
- `CODE_END`

may also change from pass to pass.

The assembler handles this by including segment layout in the same fixed-point iteration as relaxation:

1. lay out segments
2. compute segment symbols
3. resolve expressions
4. relax shrinkable instructions
5. repeat until stable

So derived segment placement and instruction relaxation converge together.

---

## Segment Example

```asm
.segmentdef "ZEROPAGE", $0000, $0100
.segmentdef "CODE",     $8000, $2000
.segmentdef "RODATA",   CODE_END, $2000 - CODE_SIZE

.segment "ZEROPAGE"
ptr:
   .res 2

.segment "CODE"
start:
   LDA #<msg
   STA ptr
   LDA #>msg
   STA ptr+1
   RTS

.segment "RODATA"
msg:
   .asciiz "hello"
```

In this example:

- `ptr` is placed in zero page
- `start` is placed in `CODE`
- `msg` is placed in `RODATA`
- `RODATA` starts immediately after the used portion of `CODE`

---

## Current Limitations of Segments

Segments are currently **assembler-placed**, not linker-placed.

That means:

- in Intel HEX mode, segment bases are resolved during assembly
- in o65 mode, only the classic o65 segments are meaningful to the object writer: `TEXT`, `DATA`, `BSS`, and `ZEROPAGE`
- `.segmentdef` is ignored in o65 mode, because final placement belongs to the linker
- `.org` in o65 mode changes only the relative offset within the current segment and does not record an absolute placement

So segment support is still useful in both modes, but only the final-assembly path treats segments as fully placed absolute regions.

---

# Current Limitations / Not Yet Implemented

The assembler does **not** currently document support for:

- ELF or other object formats besides o65
- fully general linker semantics
- `.equ` directive
- `.set`
- conditional assembly
- anonymous `+` / `-` labels
- exact raw-source preservation in `.lst`
- macro recursion
- macro conditionals
- macro defaults/variadics
- include cycle reporting with a full stack trace
- typo suggestions for undefined symbols
- map file output
- binary output file directly


## Current o65 Limitations

The o65 writer is intentionally a practical subset, not a complete general-purpose linker front end. Current important limits are:

- supported object segments are limited to `TEXT`, `DATA`, `BSS`, and `ZEROPAGE`
- relocation support is limited to the common 6502 low-byte, high-byte, and 16-bit word cases
- branch targets are not emitted as relocatable fixups
- expressions with more than one relocatable term are rejected
- relocation arithmetic such as multiplying or dividing relocatable values is rejected
- `.segmentdef` is ignored in o65 mode and produces a warning
- `.org` in o65 mode produces a warning because it affects only the segment-relative offset

The intent is to support ordinary 6502 relocatable code and data cleanly, not to pretend every imaginable linker expression is safe or portable.

---

# Design Summary

This assembler deliberately keeps the parser focused on ordinary assembly syntax and moves source-language conveniences into a preprocessing phase:

- `.include` is source expansion
- macros are source expansion
- `.def` aliases are source expansion
- file/line provenance is preserved with internal markers
- parser builds IR
- pass 1 lays out addresses and resolves symbols/constants
- relaxation shrinks legal ambiguous forms
- pass 2 emits bytes and listings

That keeps the real assembler logic smaller and avoids stuffing Flex/Bison with jobs they do not need.

---

# Practical Notes

- use explicit mode specifiers when you care about exact encoding
- use locals `@name` to avoid global label clutter
- use constants for hardware addresses and derived values
- use macros for repeated instruction sequences
- treat `name = *` as “capture address now,” not “re-evaluate later”

If you later add true `.equ`, map files, or anonymous labels, this document should be updated accordingly.


## Weak symbols

Use `.weak foo` to mark `foo` as a weak exported symbol.
Within the source file the symbol is still written as `foo`, but when the symbol is exported to an o65 object file its external name becomes `__weak_foo`.
The linker may use `__weak_foo` as a fallback definition for an unresolved `foo` when no strong `foo` is present.
