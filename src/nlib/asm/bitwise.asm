; bitwise.s - AND, OR, NOT, XOR
;
; Arbitrary-width bitwise logic on buffers
; Inputs:
;   ptr1, ptr2 - input buffers (ptr2 not needed for NOT)
;   ptr3 - destination
;   size - byte count
; Clobbers: A, Y

.include "nlib.inc"

.proc _bit_andN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    and (ptr2), y
    sta (ptr3), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _bit_notN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    eor #$FF
    sta (ptr3), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _bit_orN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    ora (ptr2), y
    sta (ptr3), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _bit_xorN
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
