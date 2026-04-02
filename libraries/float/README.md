# float code generator

`gen.pl` writes `.n` source for float-like operator overloads:

```sh
perl libraries/float/gen.pl typename little-or-big size-bytes exp-bits > mytype_ops.n
```

The generated file expects a matching `type typename { ... };` declaration to already exist. It then emits:

- `typename operator+(typename, typename)`
- `typename operator-(typename, typename)`
- `bool operator==(typename, typename)`
- `bool operator!=(typename, typename)`
- `bool operator<(typename, typename)`
- `bool operator>(typename, typename)`
- `bool operator<=(typename, typename)`
- `bool operator>=(typename, typename)`

The implementation is pure `.n` code. It uses a union overlay plus a bitfield struct to expose sign, exponent, and mantissa, then performs manual `SExMy` arithmetic/comparison in generated helpers. It does not call `_faddN`, `_fsubN`, or `_fcmp` from `nlib`.

Current limits:

- supports sizes up to 8 bytes
- assumes the target format is `SExMy` with the supplied exponent width
- supports both little-endian and big-endian storage
- handles zeros, subnormals, infinities, and NaNs for the declared layout

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
