; floating-point compare helper stub
;
; Inputs:
;   ptr0 - lhs
;   ptr1 - rhs
;   arg0 - total size in bytes
;   arg1 - exponent bit count
; Layout is always SEM from the most-significant bit down.
;
; Result:
;   arg1 = comparison result
;          $ff => lhs < rhs
;          $00 => lhs == rhs
;          $01 => lhs > rhs
;
; NOTE:
;   This file establishes the helper ABI and assembles into nlib.
;   The current implementation always reports equality.
;
.include "nlib.inc"

.proc _fcmp
    lda #0
    sta arg1
    rts
.endproc
