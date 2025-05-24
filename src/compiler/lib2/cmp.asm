
; cmp.asm - Comparison routines
;
; Implements:
; - eq: equal
; - lt: less than
; - le: less or equal
;
; Returns result in A: 1 if true, 0 if false

.include "zp.inc"

.proc eq
    ; Check equality byte by byte
    rts
.endproc

.proc lt_unsigned
    ; Unsigned less-than
    rts
.endproc

.proc le_unsigned
    ; Unsigned less-or-equal
    rts
.endproc
