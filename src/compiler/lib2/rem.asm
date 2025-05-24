
; rem.asm - Remainder-only division
;
; Like div_unsigned, but only produces remainder in ptr4.
.proc rem_unsigned
    jsr div_unsigned
    rts
.endproc
