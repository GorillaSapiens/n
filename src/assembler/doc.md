# 6502 Assembler Documentation

## Overview

This assembler is a custom two-pass 6502 assembler with:

- Intel HEX output
- listing file output
- recursive `.include`
- expression evaluation
- labels and constants
- local labels
- addressing-mode specifiers
- relaxation from absolute-family encodings to zero-page-family encodings where legal
- simple source-level macros
- multi-error reporting

It is designed as a **final binary assembler**, not a relocatable object generator.

---

## Command Line

```sh
./na input.s output.hex output.lst
```

Arguments:

- `input.s` ... root assembly source file
- `output.hex` ... Intel HEX output
- `output.lst` ... listing output

If assembly fails, the assembler returns nonzero and suppresses final HEX output.

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

- segment bases are resolved during assembly
- output is still final absolute Intel HEX
- there is no relocatable object format
- there are no fixups or external relocation records

So segment support is real and useful, but it is still **final-assembly segment layout**, not full linker semantics.

---

# Current Limitations / Not Yet Implemented

The assembler does **not** currently document support for:

- relocatable object output
- linker/fixups
- `.equ` alias syntax
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

---

# Design Summary

This assembler deliberately keeps the parser focused on ordinary assembly syntax and moves source-language conveniences into a preprocessing phase:

- `.include` is source expansion
- macros are source expansion
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

If you later add `.equ`, map files, or anonymous labels, this document should be updated accordingly.
