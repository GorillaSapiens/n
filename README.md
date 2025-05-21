# 8c — A Minimal C-Like Language for Embedded Systems

**8c** is a small, clean, and modern language inspired by C, designed for embedded systems and low-resource environments. It streamlines type management, improves syntactic clarity, and eliminates legacy C quirks — making it easier to parse, reason about, and implement.

---

## 🎯 Key Design Goals

- Simplicity and deterministic grammar
- Strong support for structs, unions, and explicit types
- Optional metadata via `$flags`
- No preprocessor or undefined behavior
- Friendly to compiler writers and toolchain builders

---

## 🔑 Differences from C

### ✅ Declarations

- `:=` is used for assignment and initialization.
- All types are declared explicitly, including pointers.

```c
type uint8 { 1 $byte $packed };
type * { 2 };

uint8 a := 42;
```

### ✅ Type Definitions

```c
type foo { 4 $custom $volatile };
```

- Trailing semicolons are required: `type foo { 4 $flag };`
- `$flags` are optional and treated as named metadata

---

## 🧱 Structs and Unions

Struct and union declarations require semicolon-separated fields:

```c
struct Point {
    int x;
    int y;
};

union Value {
    int i;
    float f;
};
```

- `type`, `struct`, and `union` are unified under the same registration mechanism
- Self-referential fields are allowed

---

## 🧮 Initialization and Literals

Structs and unions can be initialized with field-value syntax:

```c
Point p := Point { x := 1; y := 2; };
Value v := Value { i := 42; };
```

---

## 🧵 Function Definitions

Functions support optional parameter names:

```c
int sum(int, int);         // declaration
int sum(int a, int b) {    // definition
    return a + b;
}
```

- Parameters can be named or unnamed
- Returns and arguments must be typed

---

## 🔁 Control Flow

```c
if (cond) { ... }
else { ... }

while (cond) { ... }
do { ... } while (cond);

for (i := 0; i < 10; i := i + 1) { ... }

switch (x) {
    case 1:
        break;
    default:
        break;
}
```

### Labels and Goto

```c
retry:
goto retry;
```

### Labeled break/continue

```c
loop:
for (...) {
    if (...) break loop;
}
```

---

## 🧩 Syntax Summary

- `:=` for all assignment
- No implicit fallthrough
- All types declared up front with sizes
- `$flag` sigils for type metadata
- Only block-based control structures — no dangling `else`

---

## 🛠 Example

```c
type uint8 { 1 $unsigned }; // uint8 is 1 byte, unsigned
type * { 2 $unsigned $endian:little}; // pointers are 2 bytes, unsigned

struct LED {
    uint8 pin;
    uint8 state;
};

LED status := LED { pin := 1; state := 0 };

void main(void) {
   for (i := 0; i < 5; i := i + 1) {
      if (status.state) break;
   }
}
```

---

## 🧪 Tooling-Friendly

- Simple grammar with no shift/reduce conflicts
- Bison/Yacc compatible
- No preprocessor, no header files, no legacy C traps

---

## 📦 Status

Actively developed for experimentation in compiler design and low-level runtime environments. Not intended to be a full C replacement — it’s better.

