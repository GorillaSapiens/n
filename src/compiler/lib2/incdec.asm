
; incdec.asm - Increment and decrement
;
; Operates in-place on ptr1
;
; Inputs:
;   ptr1 - buffer to modify
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

.proc inc
    ; Increment multi-byte value
    rts
.endproc

.proc dec
    ; Decrement multi-byte value
    rts
.endproc
