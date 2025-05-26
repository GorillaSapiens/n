; add24.s - Arbitrary-length and fixed-width addition routines (unsigned/signed)
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

.export add24

; Zero page locations assumed
.include "nlib.inc"

.proc add24
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
