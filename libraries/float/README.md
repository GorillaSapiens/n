# float code generator

`gen.pl` writes `.n` source for float-like operator overloads.

Classic single-file mode still writes one monolithic implementation to stdout:

```sh
perl libraries/float/gen.pl typename little-or-big size-bytes exp-bits > mytype_ops.n
```

That output expects a matching `type typename { ... };` declaration to already exist in the including translation unit. It emits:

- `typename operator+(typename, typename)`
- `typename operator-(typename, typename)`
- `typename operator*(typename, typename)`
- `typename operator/(typename, typename)`
- `bool operator==(typename, typename)`
- `bool operator!=(typename, typename)`
- `bool operator<(typename, typename)`
- `bool operator>(typename, typename)`
- `bool operator<=(typename, typename)`
- `bool operator>=(typename, typename)`

Build mode writes archive-friendly generated sources and immediately compiles them:

```sh
perl libraries/float/gen.pl --build outdir typename little-or-big size-bytes exp-bits
```

That produces:

- `outdir/<typename>_decls.n` ... type declaration plus `extern operator...` prototypes
- `outdir/<typename>_operator_<name>.n` ... one self-contained source per operator member
- matching `.s` and `.o65` files for each operator source
- `outdir/<typename>.a65` ... archive containing all generated operator members

The per-operator build-mode units are self-contained and mark their scratch globals and helper routines `static`, so multiple generated members can coexist inside one archive without symbol collisions.

The implementation is pure `.n` code. It uses a union overlay plus a bitfield struct to expose sign, exponent, and mantissa, then performs manual `SExMy` arithmetic/comparison in generated helpers. It does not call `_faddN`, `_fsubN`, `_fmulN`, or `_fcmp` from `nlib`.

The generated helpers and scratch globals are ordinary user-defined `.n` symbols with an `nlf_` prefix. They intentionally do not start with `_`, and the compiler preserves that at the assembly/object-symbol layer too; raw `nlib` helper names remain separate assembly symbols like `_pushN` and `_callptr0`.

The repository root `Makefile` uses this build mode to regenerate the flat archive-fixture files under `test/` via the `generated_float_archive_fixtures` target. Those files exist only to feed the test harness; `libraries/float/gen.pl` remains the source of truth.

Current limits:

- supports sizes up to 8 bytes
- assumes the target format is `SExMy` with the supplied exponent width
- supports both little-endian and big-endian storage
- handles zeros, subnormals, infinities, and NaNs for the declared layout
- build-mode declaration inference only auto-tags known built-in `$float:` styles (`ieee754` / `simple`); other layouts fall back to a size/endian-only type declaration in `<typename>_decls.n`

Example:

```n
type gf32 { $size:4 $endian:little $float:ieee754 };
include "generated_gf32_ops.n"

union bits {
   gf32 f;
   char b[4];
};

void main(void) {
   bits a;
   bits b;
   bits c;
   a.f := 1.0;
   b.f := 0.5;
   c.f := a.f + b.f;
}
```
