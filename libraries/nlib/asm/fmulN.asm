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
;   This file establishes the helper ABI and assembles into nlib.
;   The current implementation clears the destination buffer.
;
.include "nlib.inc"

.proc _fmulN
    ldx arg0
    ldy #0
    lda #0
@loop:
    sta (ptr2), y
    iny
    dex
    bne @loop
    rts
.endproc
