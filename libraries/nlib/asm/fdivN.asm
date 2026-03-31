; floating-point helper debug stub
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
;   This is intentionally a bytewise XOR helper so compiler/runtime
;   plumbing can be verified independently of real float math.
;
.include "nlib.inc"

.proc _fdivN
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    eor (ptr1), y
    sta (ptr2), y
    iny
    dex
    bne @loop
    rts
.endproc
