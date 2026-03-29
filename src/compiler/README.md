# N compiler and language notes

N is a mostly C-like systems language aimed at small targets, especially 8-bit machines. This document describes the language model and the current compiler/runtime behavior as implemented in this tree.

## Big differences from C

- Assignment uses `:=` instead of `=`.
- Braces are required on `if`, `else`, loops, and similar statements. There is no dangling-`else` ambiguity.
- Included files behave like `pragma once` automatically.
- There are no built-in integer or float type names. Types are declared explicitly.
- Struct and union names become types directly. There is no separate `typedef struct foo foo;` dance.
- Functions can return any value type supported by the compiler, including arrays.
- Static function parameters are supported.
- Some operators can be overloaded.
- Strings can be translated through named `xform` mappings.
- Inline assembly statements are supported as raw one-line passthroughs with `asm ...` inside functions.

## Type system

There are no implicit built-in scalar types other than the required pointer type `*`, the required boolean type `bool`, and the required empty type `void`.

Example:

```n
type void   { $size:0 };
type bool   { $size:1 };
type *      { $size:2 $unsigned $endian:little };
type s2     { $size:2 $signed   $endian:little };
type u4     { $size:4 $unsigned $endian:little };
type f4     { $size:4 $float    $endian:little };
```

### Required type declarations

The compiler requires these declarations to exist in the program or its includes:

- `*` ... the machine pointer type
- `bool` ... boolean result type used by comparisons and logical expressions
- `void` ... the canonical no-value type used for empty parameter lists and no-result functions

`int` and `float` are **not** required and are not hard-coded semantic fallback types anymore.

### Type flags

Recognized flags include:

- `$size:N`
- `$signed`
- `$unsigned`
- `$float`
- `$endian:little`
- `$endian:big`

## Declarators

The compiler supports:

- pointers
- arrays
- functions
- combinations such as arrays of pointers, pointer-to-function style declarators, and return-value arrays where the grammar allows them

Struct and union declarations immediately introduce their names as usable types.

### Function declarations

Ordinary function declarations work. Multiple compatible declarations are allowed, and a later definition may follow an earlier declaration. Incompatible redeclarations are rejected.

```n
int twice(int x);

int main(void) {
   return twice(21);
}

int twice(int x) {
   return x + x;
}
```

`extern` function declarations are also supported and cause the compiler to emit an import for the referenced symbol.

## Expressions

### Truthiness

Truth-testing is driven by `operator{}`.

```n
bool operator{}(box b) {
   return b.valid;
}
```

The compiler uses this hook for:

- `if (x)`
- `while (x)`
- `for (...; x; ...)`
- `!x`
- `x && y`
- `x || y`
- conditional-expression tests

`!`, `&&`, and `||` are builtin operators and are **not** overloadable. They short-circuit and use `operator{}` under the hood.

### Promotions

Builtin integer expressions use C-like widening rules:

- the smaller operand is widened to the larger width
- signed values are sign-extended
- unsigned values are zero-extended
- when signed and unsigned are mixed, the compiler chooses the smallest declared **signed** integer type that can represent the full range of both operands

This promotion logic is used for:

- ordinary binary arithmetic
- comparisons
- compound assignment, including overload-aware sugar for builtin compound operators

### Shifts

Shifts are more C-like than earlier versions:

- the result type is based on the promoted **left** operand
- the right operand supplies the shift count
- signed right shift uses arithmetic shift
- unsigned right shift uses logical shift

The compiler now diagnoses constant negative shift counts and constant oversized shift counts.

### Endianness in expressions and assignment

Mixed-endian assignments and mixed-endian integer promotions are supported.

The compiler now performs endian-aware conversion when values move between slots or symbols. When source and destination integer endianness differ, bytes are reordered instead of blindly copied.

For mixed-endian integer arithmetic, the promotion chooser prefers a little-endian work type when the operand endianness differs, because the current runtime helpers for multiply, divide, modulo, shifts, and ordered comparisons operate on little-endian buffers.

That means these are now handled sensibly:

- big-endian to little-endian assignment of equal-sized integers
- little-endian to big-endian assignment of equal-sized integers
- mixed-endian integer arithmetic after promotion
- mixed-endian comparisons after promotion

## Inline assembly

Inside a function body, a line of the form:

```n
asm nop
asm lda #$01
asm loop_start:
```

emits the remainder of the line directly into the generated assembler output at that point.

Current limits:

- it is a single-line statement
- it is emitted verbatim after the `asm ` prefix is removed
- operand checking and clobber tracking are entirely the programmer's responsibility

## Operator overloading

Operator functions are ordinary functions spelled with `operator...` names.

Examples:

```n
vec2 operator+(vec2 lhs, vec2 rhs) {
   vec2 out;
   out.x := lhs.x + rhs.x;
   out.y := lhs.y + rhs.y;
   return out;
}

bool operator{}(vec2 v) {
   return v.x || v.y;
}
```

### Implemented overloads

The compiler supports exact-signature operator overload resolution for:

- unary `+`, `-`, `~`
- binary `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`
- binary comparisons `==`, `!=`, `<`, `>`, `<=`, `>=`
- `operator{}` truthiness hook
- `++` and `--`

### `++` and `--`

Pre and post increment/decrement use the **same** overload. The compiler owns the sequencing difference:

- pre form returns the updated value
- post form returns the previous value

A typical overload looks like this:

```n
counter operator++(ref counter x) {
   x.value := x.value + 1;
   return x;
}
```

### Compound assignment sugar

Compound assignments are treated as syntactic sugar over the corresponding binary operator plus ordinary assignment.

Examples:

- `a += b` behaves like `a := a + b`
- `a <<= b` behaves like `a := a << b`
- `a &= b` behaves like `a := a & b`

That means the compiler first tries the matching overloaded binary operator such as `operator+`, `operator<<`, or `operator&`. If no matching overload is available, it falls back to the builtin compound-assignment implementation.

The left-hand side is still treated as an lvalue target for the final store, so the result of the binary operator is converted and assigned back using ordinary assignment rules.

### Overload matching limits

Operator overload matching now prefers exact matches first and then considers safe integer promotions for plain value parameters. Smaller integers may widen, and mixed signed/unsigned operands may promote to a signed type that can represent the full range of the actual argument. `ref` parameters remain strict and still require an lvalue of the exact declared type.

This is not a full C++-style overload system. There is still no arbitrary narrowing conversion search, no user-defined conversion machinery, and no general function-overload ranking outside operator overloads.

## `ref` parameters

`ref` parameters are real pass-by-reference parameters.

- callers pass an address, not a copied value
- reads and writes in the callee dereference the referenced object
- mangling and overload matching distinguish `ref` parameters

Example:

```n
void swap(ref s2 a, ref s2 b) {
   s2 t;
   t := a;
   a := b;
   b := t;
}
```

### `static ref` parameters

`static ref` parameters work. Their backing symbol storage is pointer-sized, not referent-sized.

## Function parameters

### Ordinary parameters

Ordinary parameters are passed in the N argument stack frame.

### Static parameters

Function parameters declared `static` are not pushed as ordinary call-frame arguments.

Instead:

- the callee owns a symbol-backed storage slot for that parameter
- the caller evaluates the argument and writes it directly into that storage before `jsr`

This matches the advertised "static parameter" model.

### Zeropage-backed parameters

If a parameter uses a mem modifier that resolves to a zero-page region, the compiler treats it as zero-page-backed storage.

## Storage classes and memory regions

### Globals, locals, static locals

The compiler supports:

- globals
- stack locals
- function-scope `static`
- zero-page-backed storage

### Memory regions

Memory region handling is driven by `mem` declarations, not by hard-coded names.

A declaration is treated as zero-page only if its referenced `mem` declaration fits entirely inside `$0000..$00FF` according to `$start` plus `$size` or `$end`.

So a region named `banana` can still be zero-page if its declared address range fits there, and a region literally named `zeropage` is **not** magically zero-page if its range does not fit.

## Initializers

### Static and global initializers

The compiler supports real constant-expression evaluation for static/global initializers, including:

- integers
- floats
- booleans
- comparisons and logical expressions
- ternary expressions
- nested aggregate initializers
- simple relocatable address constants such as `&symbol + 1`

When a non-constant global initializer cannot be emitted as static bytes, the compiler now places the object in writable storage and emits a translation-unit `__init_*` function so startup code can perform the runtime initialization before `main`.

### String initializers

Strings can initialize pointer values and byte arrays where appropriate. String bytes may be translated through an `xform`.

## Arrays

### Local arrays

Automatic local arrays reserve their full declared size.

### Array returns

Functions can return arrays. The compiler now sizes:

- the callee return slot
- the caller temporary/copy area
- expression value size tracking

using the full declared return-object size instead of the base element size.

## Control flow

The compiler supports:

- `if` / `else`
- `while`
- `do` / `while`
- `for`
- `switch`
- labeled `break` and `continue`
- `goto`

## Strings and xforms

A string literal may optionally specify an `xform` name after a backtick.

```n
char msg1[] = "hello"`cp437;
char msg2[] = "hello";
```

## ABI and runtime notes

### Hardware stack vs N stack

The 6502 hardware stack is still used for `jsr`, `rts`, temporary saves, and similar low-level operations.

The language-level argument/local storage model uses `_nl_sp` and `_nl_fp`.

### `_nl_sp` and `_nl_fp`

Startup initializes both `_nl_sp` and `_nl_fp` from `__bss_end`, not from a hard-coded constant. The N stack grows upward from there.

### Frame pointer preservation

Compiled calls save and restore the caller's frame pointer around calls so nested calls do not smash the caller's frame-relative addressing.

## What is still incomplete or limited

This tree is much further along than the original state, but a few sharp edges remain:

- operator overload resolution only considers exact matches plus safe integer promotions for plain value parameters
- ordinary non-operator function overloading and generic best-viable-match search are still not implemented
- arbitrary non-zero-page named memory regions are not yet fully honored as distinct backend storage segments
- some runtime helpers are still little-endian-specific, which is why mixed-endian arithmetic normalizes through a little-endian promoted work type when needed
- shift-count diagnostics are still lax

## Minimal example

```n
type void { $size:0 };
type bool { $size:1 };
type *    { $size:2 $unsigned $endian:little };
type s2   { $size:2 $signed   $endian:little };

bool operator{}(s2 v) {
   return v != 0;
}

void bump(ref s2 x) {
   x++;
}

s2 main(void) {
   s2 x;
   x := 1;
   bump(x);
   if (x) {
      x += 2;
   }
   return x;
}
```
