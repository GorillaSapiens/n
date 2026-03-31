; floating-point subtraction helper
;
; Inputs:
;   ptr0 - lhs
;   ptr1 - rhs
;   ptr2 - destination
;   arg0 - total size in bytes
;   arg1 - exponent bit count
;
; Notes:
;   - currently implements binary32 only (size=4, expbits=8)
;   - subtraction reuses the shared add/sub core with rhs sign xor set
;
.include "nlib.inc"
.import __faddsubN_core

.proc _fsubN
    lda #$80
    sta tmp5
    jmp __faddsubN_core
.endproc
