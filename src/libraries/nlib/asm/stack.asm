; stack.asm - Arbitrary-length stack operations

.include "nlib.inc"

.proc _pushN
    ; increment sp by arg0
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
    ; decrement sp by arg0
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
    ; copy arg0 bytes from ptr0 to ptr1
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
    ; arg0 bytes ptr1 = 2's complement of ptr0
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
    ; swap arg0 bytes between ptr0 and ptr1
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

.proc _callptr0
    ; indirect call through ptr0
    ; pushes (target - 1) so RTS transfers control to ptr0
    sec
    lda ptr0
    sbc #1
    tax
    lda ptr0+1
    sbc #0
    pha
    txa
    pha
    rts
.endproc
