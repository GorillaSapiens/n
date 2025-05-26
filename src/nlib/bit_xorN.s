; bit_xorN.s - AND, OR, NOT, XOR
;
; Arbitrary-width bitwise logic on buffers
; Inputs:
;   ptr1, ptr2 - input buffers (ptr2 not needed for NOT)
;   ptr3 - destination
;   size - byte count
; Clobbers: A, Y

.export bit_xorN

.include "nlib.inc"

.proc bit_xorN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    eor (ptr2), y
    sta (ptr3), y
    iny
    dex
    bne @loop
    rts
.endproc
