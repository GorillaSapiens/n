; stack.asm - Arbitrary-length stack operations

.include "nlib.inc"

.proc _pushN
    ; stack grows up!
    clc
    lda sp
    adc size
    sta sp
    lda sp+1
    adc #0
    sta sp+1
    rts
.endproc

.proc _popN
    sec
    lda sp
    sbc size
    sta sp
    lda sp+1
    sbc #0
    sta sp+1
    rts
.endproc

.proc _cpyN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    sta (ptr2), y
    iny
    dex
    bne @loop
    rts
.endproc
