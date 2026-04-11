# N compiler and language notes

N is a mostly C-like systems language aimed at small targets, especially 8-bit machines. This document describes the language model and the current compiler/runtime behavior as implemented in this tree.

## Big differences from C

- Assignment uses `:=` instead of `=`.
- Braces are required on `if`, `else`, loops, and similar statements. There is no dangling-`else` ambiguity.
- Included files behave like `pragma once` automatically. Include-file identity is currently based on an MD5 of file contents, so duplicate content is only compiled once.
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
type bool   { $size:1 $integer:unsigned };
type *      { $size:2 $integer:unsigned $endian:little };
type s2     { $size:2 $integer:signed   $endian:little };
type u4     { $size:4 $integer:unsigned $endian:little };
type f4     { $size:4 $float:ieee754 $endian:little }; // IEEE 754 binary32
```

### Required type declarations

The compiler requires these declarations to exist in the program or its includes:

- `*` ... the machine pointer type
- `bool` ... boolean result type used by comparisons and logical expressions
- `void` ... the canonical no-value type used for empty parameter lists and no-result functions

`int` and `float` are **not** required and are not hard-coded semantic fallback types anymore.

Non-float scalar type declarations now have to say whether they are integer-like with `$integer:signed` or `$integer:unsigned`. The required `bool` type must use `$integer:unsigned`, while `void` remains flagless.

Bitfield reads follow the declared integer style of the field type: signed integer types sign-extend, unsigned integer types zero-extend.

### Type flags

Recognized flags include:

- `$size:N`
- `$integer:signed`
- `$integer:unsigned`
- `$exactops` ... same-type operators on this type must resolve through visible exact-name `operator...` overloads; the compiler does not fall back to generic helpers for that type
- `$float:ieee754` ... IEEE 754 packing for `$size:2`, `$size:4`, and `$size:8`
- `$float:simple` ... generic `SExMy` packing where `x = round(3 * log2(size) + 2)` and `y` is the remaining fraction bits

For generated arithmetic/comparison overloads on custom float-like types, see `libraries/float/gen.pl`. It emits `.n` code that cracks the value through union+bitfield overlays and now generates an exact-operator surface for the float type: binary `+ - * /`, unary `+ -`, `== != < > <= >=`, `operator{}` truthiness, and `++ --`. Build mode emits a `<typename>_decls.n` file that declares the type with `$exactops` plus the matching `extern operator...` prototypes. Classic single-file mode emits only the operator definitions, so the including translation unit must provide the matching `type ... $exactops` declaration itself.
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

`extern` function declarations are also supported and cause the compiler to emit an import for the referenced symbol. Direct calls require a visible function signature in the current translation unit or via an `extern` declaration; the compiler rejects bare calls to unknown symbols instead of guessing at a call ABI.

### Ordinary function overloading

Ordinary non-operator functions can now be overloaded by parameter signature. Overload resolution uses a best-viable-match search much like the current operator-overload matcher:

- exact matches win first
- safe integer promotions for plain value parameters are considered after exact matches
- `ref` parameters remain strict and require an lvalue of the exact declared type
- ambiguous best matches are rejected

Examples:

```n
s2 pick(s2 x) {
   return x;
}

s4 pick(s4 x) {
   return x;
}

s2 a(s2 x) {
   return pick(x);
}

s4 b(s4 x) {
   return pick(x);
}
```

If no viable overload exists, the compiler rejects the call. If multiple viable overloads tie for best cost, the compiler reports the call as ambiguous.

### Function pointers and indirect calls

Pointer-to-function declarators are supported.

```n
int twice(int x) {
   return x + x;
}

int (*fp)(int) := twice;

int main(void) {
   return fp(21);
}
```

Function names decay to function pointers in ordinary expression and initializer contexts, and `&name` also works when a function pointer is wanted.

Indirect calls through function pointers are implemented. The compiler lowers them through a small runtime helper so ordinary call-frame setup and result handling still work.

Functions that use `static` parameters are **not** allowed to have pointers formed to them. That prohibition applies both to bare decay and explicit `&name`, because the static-parameter calling convention needs caller knowledge that a plain function pointer does not carry.


### Variadic functions and `stdarg.n`

Parser and AST support exist for `...`, and the current backend implements variadic calls as a raw byte blob rather than C's promotion-heavy ABI.

There is no textual preprocessor yet, so the user-facing layer is a small builtin-style wrapper in `libraries/nlib/stdarg.n` rather than literal macros. Include it and use these compiler-recognized forms:

```n
include "stdarg.n"

int sum(int count, ...) {
   va_list ap;
   int x;
   int total := 0;

   va_start(ap);
   while (count) {
      va_arg(ap, x);
      total += x;
      count--;
   }
   va_end(ap);
   return total;
}
```

`stdarg.n` defines:

```n
struct va_list {
   char *args;
   char *bytes;
   char *offset;
};
```

Behavior of the current variadic ABI:

- variadic arguments are packed left-to-right in source order
- there is no alignment padding between variadic arguments
- there are no C-style default promotions for variadic arguments
- each argument is copied using its actual runtime storage size and byte order
- `va_arg(ap, out)` copies `sizeof(out)` bytes into the destination lvalue and advances `ap.offset`
- `va_end(ap)` zeroes the `va_list` state

That means a call like `f(1`char, 2`int, 3`long)` is packed as 1 byte, then 2 bytes, then 4 bytes... not as promoted `int, int, long`.

The implementation names `__va_args` and `__va_arg_bytes` are reserved for compiler-generated variadic metadata and may not be declared by user code.

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

### Literal typing, casts, and mixed integer expressions

The intended language model for integer expressions is deliberately simpler than C. The current compiler still has older behavior in some places, but the target rule set is:

- a literal used only with other literals is folded on the host at compile time, and the result remains a literal
- a literal interacting with a typed nonliteral operand adopts that operand's type for the operation
- a literal consumed by a typed sink such as assignment, return, or argument passing adopts the sink type at that boundary
- two operands of the same type produce that same type
- for ordinary non-`$exactops` integers of different widths, the narrower operand widens to the wider width first
- widening sign-extends signed integers and zero-extends unsigned integers
- narrowing truncates bitwise; there is no saturation or range check by default
- if width adjustment still leaves one operand signed and the other unsigned, the expression is rejected unless the user writes an explicit cast
- `$exactops` values do not participate in mixed-type promotions; an `$exactops` value may only interact with another type after an explicit cast

This is intentionally less C-like than the usual arithmetic conversions. The compiler should widen by width automatically, but it should not guess signedness automatically.

### Cast forms

The language uses two cast families:

- backtick casts such as ``123`u2`` are literal-only and always happen immediately on the host
- parenthesized casts such as `(u2)expr` are ordinary expression casts; when applied to a literal they may also fold on the host at compile time

There are also two planned signedness-only shortcut casts:

- ``($signed)expr``
- ``($unsigned)expr``

These are intended to preserve width while changing signedness, but only for already-typed ordinary fixed-width integers. They are never legal on literals, `$exactops` types, floats, or pointers.

### Shifts

The intended shift rules are:

- the result type is the type of the left operand after ordinary literal typing and any explicit casts have been applied
- if the right operand is a literal, it adopts the type needed by the surrounding operation just like any other literal
- a literal-only shift is folded on the host and remains a literal until consumed by a typed sink
- signed right shift uses arithmetic shift
- unsigned right shift uses logical shift
- negative constant shift counts are hard errors
- oversized constant shift counts are hard errors
- runtime negative shift counts are not a supported language feature; codegen should not reinterpret `x << -n` as `x >> n`

### Current implementation note

As of this snapshot, parts of the compiler still implement older C-like promotion behavior in some paths. The rules above are the intended language contract and should be treated as the direction for ongoing cleanup work.

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

By default, same-type operators behave pragmatically:

- if a visible exact overload exists, the compiler uses it
- otherwise the compiler falls back to the builtin/generic lowering for that representation

`$exactops` changes that contract for the marked type. When both operands already have that exact declared type name, or when that type is used for unary operators, truthiness, or `++`/`--`, the compiler requires a visible exact-name overload and does **not** fall back to generic helpers. That means a type such as:

```n
type wideint { $size:4 $integer:signed $endian:little $exactops };
```

must provide the overloads it actually uses, for example `operator+`, `operator==`, `operator{}`, or `operator++`. If one is used without a visible declaration or definition, compilation fails immediately instead of emitting a symbolic call and hoping the linker finds something later.

This is not a full C++-style overload system. There is still no arbitrary narrowing conversion search and no user-defined conversion machinery. Ordinary function overloading now uses the same general best-viable-match style as operators for exact matches plus safe integer promotions.

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

### Absolute `ref` declarations

The compiler supports `ref` declarations bound directly to absolute addresses. This is intended for memory-mapped hardware registers and similar machine-defined storage that already exists outside normal compiler allocation.

Supported forms:

```n
ref u8 port@0x10;
ref u8 status@STATUS_REG;
ref u8 vsync@[none/0x00];
ref u8 cxm0p@[0x00/none];
ref u16 banked@[0x100/0x180];
```

Meaning:

- `ref T x@addr` is shorthand for `ref T x@[addr/addr]`
- `@[read/write]` gives separate address expressions for loads and stores
- either side may be `none` to model read-only or write-only hardware
- each side currently accepts a single integer literal or identifier, not an arbitrary expression

Current behavior and limits:

- reading uses the read address
- writing uses the write address
- storing to a `@[read/none]` declaration is rejected as write to a read-only absolute ref
- loading from a `@[none/write]` declaration is rejected as read from a write-only absolute ref
- taking the address of a split-address absolute ref such as `@[0x100/0x180]` is rejected, because it does not have one canonical address
- if both sides name the same address, `&name` behaves normally
- identifiers used in the address slots are passed through to the assembler/linker; the compiler does not require them to be declared as N symbols

Absolute address binding is only meaningful on `ref` declarations. Using `@...` on a non-`ref` declaration is accepted for compatibility but ignored, and the compiler now warns about it.

## Function parameters

### Ordinary parameters

Ordinary parameters are passed in the N argument stack frame.

### Symbol-backed parameters

Function parameters declared `static`, or parameters that use a `mem` modifier, are not pushed as ordinary call-frame arguments.

Instead:

- the callee owns a symbol-backed storage slot for that parameter
- the caller evaluates the argument and writes it directly into that storage before `jsr`

This includes zero-page-backed parameters and non-zero-page named memory regions such as `banana`.

Because that storage is owned by the callee rather than the call frame, symbol-backed parameters should be treated as re-entrancy-hostile unless the programmer arranges external protection. Recursive or interrupt-driven re-entry can overwrite the shared parameter slots.

The compiler now performs a direct-call graph check inside each translation unit and rejects any call-cycle strongly connected component that contains a function with symbol-backed parameters. That catches obvious self-recursion and mutual recursion cases before code generation completes.

The linker also performs the same check across the selected object files, so call cycles that only become visible after separate compilation are rejected before image generation.

Because symbol-backed parameters need named callee-owned storage, functions with symbol-backed parameters cannot be converted to plain function pointers.

## Storage classes and memory regions

### Globals, locals, static locals

The compiler supports:

- globals
- stack locals
- function-scope `static`
- mem-backed symbol storage for locals and parameters

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

### `switch` / `case`

`switch` compares the switch expression against each `case` label in source order.

`case` labels now accept either a single numeric primary expression or an inclusive range:

```n
switch (x) {
   case 1:
      break;
   case 2 to 5:
      break;
   default:
      break;
}
```

Range bounds are inclusive on both ends. If the programmer writes a reversed range such as `case 9 to 3:`, the compiler emits a warning and compiles it as `case 3 to 9:`.

`default` remains optional and may appear anywhere inside the switch body.

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

Startup initializes both `_nl_sp` and `_nl_fp` from `__stack_start`, not from a hard-coded constant. The N stack grows upward from there.

The runtime also seeds `_nl_sbrk` from `__stack_top`, so simple `sbrk` allocations can grow downward through the same free RAM arena.

### Frame pointer preservation

Compiled calls save and restore the caller's frame pointer around calls so nested calls do not smash the caller's frame-relative addressing.

## What is still incomplete or limited

This tree is much further along than the original state, but a few sharp edges remain:

- operator overload resolution only considers exact matches plus safe integer promotions for plain value parameters
- ordinary function overloading now supports exact matches plus safe integer promotions for plain value parameters, but there is still no user-defined conversion search or other C++-style ranking machinery
- some runtime helpers are still little-endian-specific, which is why mixed-endian arithmetic normalizes through a little-endian promoted work type when needed
- symbol-backed-parameter cycle checking now spans the selected object files at link time, but truly dynamic call targets still cannot be proven safe
- shift-count diagnostics are still lax

## Minimal example

```n
type void { $size:0 };
type bool { $size:1 $integer:unsigned };
type *    { $size:2 $integer:unsigned $endian:little };
type s2   { $size:2 $integer:signed   $endian:little };

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
