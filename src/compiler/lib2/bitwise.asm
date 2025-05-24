
; bitwise.asm - AND, OR, NOT
;
; Arbitrary-width bitwise logic on buffers
; Inputs:
;   ptr1, ptr2 - inputs
;   ptr3 - destination
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

.proc and_bytes
    rts
.endproc

.proc or_bytes
    rts
.endproc

.proc not_bytes
    rts
.endproc
