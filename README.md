# n Compiler (nc)

`n` is a small, experimental C-like programming language designed for simplicity, low-level clarity, and eventual embedded use. The project focuses on human-readable syntax, a minimal type system, and ease of compiler implementation — making it ideal for teaching, systems tinkering, or just writing your own language from scratch.

This repository contains the lexer, parser, and AST generation logic for `n`.

---

## ⚙️ Features

- C-style syntax with modern streamlining
- Custom types with flags (e.g. `type Point { 8 $packed }`)
- Statement blocks only — no ambiguous single-line ifs
- AST construction via Bison/Yacc and Flex
- Pretty-printed tree output for debugging
- Simple flag parsing (`$` prefix, like `$packed`, `$aligned`)
- Optional parameter names in function declarations
- Unified parser+AST design with node names and source tracking

---

## 🔧 Building

```bash
make
```

You need:
- `bison` (GNU Bison)
- `flex`
- a C compiler (e.g. `gcc`, `clang`)

---

## 🚀 Usage

```bash
./nc input.n
```

Example output:

```
Parsing...
└── program
    └── type_decl
        ├── identifier  Point
        ├── int  8
        └── flag_list
            └── identifier  $packed
Parse successful.
```

---

## 📜 Syntax Overview

```n
type Point { 8 $packed };

func distance(Point* a, Point* b) {
    return sqrt((a->x - b->x)^2 + (a->y - b->y)^2);
}

main {
    Point p := Point();
    p.x := 10;
    p.y := 20;
}
```

---

## 📂 Project Structure

- `lexer.l` – Lexical rules
- `parser.y` – Grammar and AST generation
- `ast.h` / `ast.c` – AST node types and manipulation
- `main.c` – Entry point and AST dumping
- `Makefile` – Build script

---

## 🔮 Goals

- Add type inference and pointer safety
- Optional preprocessor
- Bytecode or WASM backend
- VM or interpreter
- Compile-to-C mode for portability

---

## 🧠 Why “n”?

It’s small, minimal, and meant to be a new start. One letter, no nonsense. Just `n`.

---

## 🪓 License

MIT License. Hack away.
