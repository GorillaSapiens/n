; floating-point helper stub
;
; Inputs:
;   ptr0 - lhs
;   ptr1 - rhs
;   ptr2 - destination
;   arg0 - total size in bytes
;   arg1 - exponent bit count
; Layout is always SEM from the most-significant bit down.
;
; NOTE:
;   This is currently a bytewise XOR debug stub so compiler plumbing can be
;   validated independently from real floating-point arithmetic.
;
.include "nlib.inc"

.proc _faddN
    ldx arg0
    beq @done
    ldy #0
@loop:
    lda (ptr0), y
    eor (ptr1), y
    sta (ptr2), y
    iny
    dex
    bne @loop
@done:
    rts
.endproc
