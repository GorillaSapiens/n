
; sub.asm - Arbitrary-length and fixed-width subtraction routines (unsigned/signed)
;
; Subtracts ptr2 from ptr1 and stores in ptr3, X bytes.
; Result = ptr1 - ptr2
;
; Inputs:
;   ptr1 - minuend
;   ptr2 - subtrahend
;   ptr3 - destination
;   X    - byte count
; Clobbers: A, Y, status flags

.include "zp.inc"

.proc sub_unsigned
    ldx byte_count
    clc
sub_loop:
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    dex
    bne sub_loop
    rts
.endproc
