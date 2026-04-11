# float code generator

`gen.pl` writes `.n` source for float-like exact operator overloads.

Classic single-file mode still writes one monolithic implementation to stdout:

```sh
perl libraries/float/gen.pl typename little-or-big size-bytes exp-bits > mytype_ops.n
```

That output emits the operator definitions only. The including translation unit must already declare the matching type, and should mark it `$exactops` if you want the compiler to require the generated exact-name operators instead of falling back to generic helpers. It emits:

- binary `typename operator+(typename, typename)`
- unary `typename operator+(typename)`
- binary `typename operator-(typename, typename)`
- unary `typename operator-(typename)`
- `typename operator*(typename, typename)`
- `typename operator/(typename, typename)`
- `bool operator==(typename, typename)`
- `bool operator!=(typename, typename)`
- `bool operator<(typename, typename)`
- `bool operator>(typename, typename)`
- `bool operator<=(typename, typename)`
- `bool operator>=(typename, typename)`
- `bool operator{}(typename)`
- `typename operator++(ref typename)`
- `typename operator--(ref typename)`

Build mode writes archive-friendly generated sources and immediately compiles them:

```sh
perl libraries/float/gen.pl --build outdir typename little-or-big size-bytes exp-bits
```

That produces:

- `outdir/<typename>_decls.n` ... type declaration with `$exactops` plus `extern operator...` prototypes
- `outdir/<typename>_operator_<name>.n` ... one self-contained source per operator member
- matching `.s` and `.o65` files for each operator source
- `outdir/<typename>.a65` ... archive containing all generated operator members

The generated operator surface is intentionally complete for same-type exactops use: binary `+ - * /`, unary `+ -`, `== != < > <= >=`, `operator{}` truthiness, and `++ --`. Several of those are emitted as thin wrappers around the smaller primitive set so the compiler sees the full exact-operator contract without paying full implementation cost for every member.

The per-operator build-mode units are self-contained and mark their scratch globals and helper routines `static`, so multiple generated members can coexist inside one archive without symbol collisions.

The generator now also tries to keep emitted code and member-local state tight: it uses direct typed assignments where the compiler already handles widening/narrowing correctly, build-mode members only declare the scratch globals they actually use, and several operators are emitted as thin derived wrappers instead of separate heavy implementations. In practice that trims both generated source size and linked archive-member size, especially for compare-only members and single-op archives on 64K targets.

The implementation is pure `.n` code. It uses a union overlay plus a bitfield struct to expose sign, exponent, and mantissa, then performs manual `SExMy` arithmetic/comparison in generated helpers. It does not call `_faddN`, `_fsubN`, `_fmulN`, or `_fcmp` from `nlib`.

The generated helpers and scratch globals are ordinary user-defined `.n` symbols with an `nlf_` prefix. They intentionally do not start with `_`, and the compiler preserves that at the assembly/object-symbol layer too; raw `nlib` helper names remain separate assembly symbols like `_pushN` and `_callptr0`.

Current limits:

- supports sizes up to 8 bytes
- assumes the target format is `SExMy` with the supplied exponent width
- supports both little-endian and big-endian storage
- handles zeros, subnormals, infinities, and NaNs for the declared layout
- build-mode declaration inference only auto-tags known built-in `$float:` styles (`ieee754` / `simple`); other layouts fall back to a size/endian-only type declaration in `<typename>_decls.n`

Example:

```n
type gf32 { $size:4 $endian:little $float:ieee754 $exactops };
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
