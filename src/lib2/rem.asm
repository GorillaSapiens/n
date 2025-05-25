
; rem.asm - Remainder-only division
;
; Like div_unsigned, but only produces remainder in ptr4.
.include "nlib.inc"

.proc rem_unsigned
    jsr div_unsigned
    rts
.endproc

; rem_signed is very similar to div_signed, with one twist:
; Both do:
; 
;     Detect signs of dividend and divisor
; 
;     Make both positive (2's complement if negative)
; 
;     Call div_unsigned
; 
;     Fix sign of result
; 
; The twist:
; 
;     div_signed fixes the sign of the quotient based on the signs of A and B
; 
;     rem_signed must fix the sign of the remainder to match the original dividend only
