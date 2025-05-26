# c08 — A Compact C Dialect for 8-bit Systems

**c08** is a lightweight systems programming language inspired by C, designed for simplicity, efficiency, and embedded applications on 8-bit microcontrollers. It provides a modern parsing model, better syntax hygiene, and zero legacy bloat — with powerful low-level features and customizability.

---

## 🚀 Why c08?

- **Clean Syntax** – No typedef hell, no implicit casts, and no need for forward declarations.
- **Embedded Friendly** – Supports fixed-width types, memory-mapped I/O, and explicit address placement.
- **Self-Hosting Goals** – Intended to compile its own toolchain eventually.
- **Compiler-as-a-Library** – AST generation and error reporting are easy to integrate.
- **Deterministic Parsing** – Based on LALR(1), no lookahead gymnastics needed.

---

## 🔧 Language Highlights

- **Explicit Types**
  ```c
  type uint16 { 2 $unsigned $endian:little };
  ```

- **Variable Declarations with Addresses**
  ```c
  int counter @ 0x20;
  ```

- **Structs and Unions**
  ```c
  struct Point { int x; int y; };
  union Data  { int i; float f; };
  ```

- **Function Declarations and Definitions**
  ```c
  int sum(int a, int b) {
      return a + b;
  }
  ```

- **Operator Overloading**
  ```c
  int operator+(int a, int b) { return a + b; }
  ```

- **Struct/Union Literals**
  ```c
  Point p := Point { x := 4; y := 5; };
  ```

- **Control Flow**
  ```c
  if (x < 10) { ... }
  while (true) { ... }
  switch (val) {
      case 0: return;
      default: break;
  }
  ```

- **Labeled Loops and Goto**
  ```c
  myloop: for (...) {
      if (...) break myloop;
      if (...) continue myloop;
  }
  goto error;
  ```

- **Comments**
  - Line: `// comment`
  - Block: `/* comment */`

---

## 🛠️ Build

To build the compiler:

```sh
make
```

To parse a file and print the AST:

```sh
./c08 source.c08
```

---

## 🧪 Sample

```c
type uint8 { 1 $byte $packed };

struct Node {
    int value;
    Node *left;
    Node *right;
};

Node root := Node {
    value := 42;
    left := NULL;
    right := NULL;
};
```

---

## 📦 Status

- [x] Lexer and Parser using Flex/Bison
- [x] AST generation with location tracking
- [x] Operator overloading
- [x] Struct/Union declarations and literals
- [x] Labels, Goto, and Loops
- [ ] Code generation backend (planned)
- [ ] Optimizer and linker (future)
- [ ] Self-hosting compiler (ambitious)

---

## 🧑‍💻 Author

Created and maintained by [Gorilla](mailto:gorilla@guest), retired computer engineer, systems builder, and nomad.

---

## 📝 License

MIT License — do what you want, just don't sue anyone.
