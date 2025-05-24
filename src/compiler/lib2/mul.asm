
; mul.asm - Arbitrary-length unsigned multiplication
;
; Multiply ptr1 * ptr2 and store into ptr3.
; X = byte count of inputs (result may be up to 2X bytes).
;
; Inputs:
;   ptr1 - multiplicand
;   ptr2 - multiplier
;   ptr3 - result (2X bytes)
;   X    - byte count
; Clobbers: A, Y, temp

.include "zp.inc"

.proc mul_unsigned
    ; Not yet optimized; uses repeated addition
    ; Outer loop: for each byte in ptr2
    ; Inner loop: add ptr1 * ptr2[i] shifted i positions
    rts
.endproc
