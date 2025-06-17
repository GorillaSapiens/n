; add.s - Arbitrary-length and fixed-width addition routines (unsigned/signed)
;
; Adds two little-endian integers of length X bytes (in register X).
; Inputs:
;   ptr1 - pointer to operand A (low byte at lower address)
;   ptr2 - pointer to operand B
;   ptr3 - pointer to destination buffer
;   size - number of bytes
; Assumes:
;   ptr1, ptr2, ptr3 are 2-byte pointers in zero page
; Clobbers: A, X, Y, status flags

.include "nlib.inc"

.proc _addN
    ldx size
    ldy #0            ; Start at offset 0
    clc               ; Clear carry flag
@loop:
    lda (ptr1), y     ; Load byte from operand A
    adc (ptr2), y     ; Add byte from operand B with carry
    sta (ptr3), y     ; Store result
    iny               ; Next byte
    dex               ; Decrement byte counter
    bne @loop         ; Repeat if not zero
    rts
.endproc

.proc _add8
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc _add16
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc _add24
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc _add32
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc
