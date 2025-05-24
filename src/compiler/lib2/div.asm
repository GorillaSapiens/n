
; div.asm - Arbitrary-length unsigned division
;
; Divides ptr1 by ptr2, X bytes each.
; Stores quotient in ptr3, remainder in ptr4.
;
; Inputs:
;   ptr1 - dividend
;   ptr2 - divisor
;   ptr3 - quotient
;   ptr4 - remainder
;   X    - byte count
; Clobbers: A, Y, temp

.include "zp.inc"

.proc div_unsigned
    ; Long division algorithm placeholder
    rts
.endproc
