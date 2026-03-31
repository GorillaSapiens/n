; add.s - Arbitrary-length and fixed-width addition routines (unsigned/signed)
;
; Adds two little-endian integers of length X bytes (in register X).
; Inputs:
;   ptr0 - pointer to operand A (low byte at lower address)
;   ptr1 - pointer to operand B
;   ptr2 - pointer to destination buffer
;   arg0 - number of bytes
; Assumes:
;   ptr0, ptr1, ptr2 are 2-byte pointers in zero page
; Clobbers: A, X, Y, status flags

.include "nlib.inc"

.proc _addN
    ldx arg0
    ldy #0            ; Start at offset 0
    clc               ; Clear carry flag
@loop:
    lda (ptr0), y     ; Load byte from operand A
    adc (ptr1), y     ; Add byte from operand B with carry
    sta (ptr2), y     ; Store result
    iny               ; Next byte
    dex               ; Decrement byte counter
    bne @loop         ; Repeat if not zero
    rts
.endproc

.proc _add8
    ; setup here is long, so the compiler just does it
    ; and this is never used
    ldy #0
    clc
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _add16
    ; setup here is long, so the compiler just does it
    ; and this is never used
    ldy #0
    clc
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _add24
    ldy #0
    clc
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _add32
    ldy #0
    clc
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr2), y
    rts
.endproc
