# n65sim

`n65sim` loads a linked Intel HEX image into a 64 KiB 6502 memory array, resets the bundled MOS 6502 core, and runs until the simulated program exits through the simulator dispatch hook.

## Command line

```sh
./n65sim program.hex
```

The simulator expects exactly one Intel HEX input file.

## Dispatch hook

The simulator reserves `JSR $FFFF` as a host-call escape hatch.
When the CPU reaches program counter `$FFFF`, `n65sim` does **not** execute whatever byte happens to live there. Instead it:

1. reads the dispatch opcode from register `A`
2. reads a 16-bit argument from `Y:X` (`X` = low byte, `Y` = high byte)
3. calls the host-side `dispatch(op, arg)` function in `simulator/main.cpp`
4. temporarily plants an `RTS` at `$FFFF` so the guest code returns normally to the caller after the hook finishes

That means guest code can treat the hook like an ordinary subroutine call.

Example:

```asm
lda #$00
ldx #<message
ldy #>message
jsr $ffff
```

The simulator currently logs each dispatch first as:

```text
dispatch <op> <arg>
```

where `<op>` is two hex digits and `<arg>` is four hex digits.

## Currently defined dispatch functions

### `A = $00` ... print NUL-terminated string

- argument: `Y:X` points at a NUL-terminated byte string in simulated memory
- output: the bytes are printed to stdout as a C string

Example:

```asm
lda #$00
ldx #<message
ldy #>message
jsr $ffff
```

### `A = $FE` ... dump all memory as Intel HEX

- argument: ignored
- output: the entire 64 KiB `mem[]` array is written to stdout as Intel HEX records, wrapped in markers:

```text
---8<--- BEGIN MEMORY DUMP ---8<---
... Intel HEX records ...
---8<---  END MEMORY DUMP  ---8<---
```

The dump emits one 16-byte data record for each address range from `$0000` through `$FFFF`, followed by the normal Intel HEX EOF record.

Example:

```asm
lda #$fe
ldx #$00
ldy #$00
jsr $ffff
```

### `A = $FF` ... exit the simulator

- argument: process exit status in `Y:X`
- output: none beyond the usual `dispatch ff xxxx` log line
- effect: calls `exit(arg)` on the host process

Example:

```asm
lda #$ff
ldx #$00
ldy #$00
jsr $ffff
```

This exits with status 0.
