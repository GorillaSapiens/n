; stack.asm - Arbitrary-length stack operations

.include "nlib.inc"

.proc _pushN
    ; stack grows up!
    clc
    lda sp
    adc arg0
    sta sp
    lda sp+1
    adc #0
    sta sp+1
    rts
.endproc

.proc _popN
    sec
    lda sp
    sbc arg0
    sta sp
    lda sp+1
    sbc #0
    sta sp+1
    rts
.endproc

.proc _cpyN
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _comp2N
    ldx arg0
    ldy #0
    sec
@loop:
    lda (ptr0), y
    eor #$FF
    adc #$0
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _swapN
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    sta tmp0
    lda (ptr1), y
    sta (ptr0), y
    lda tmp0
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc
