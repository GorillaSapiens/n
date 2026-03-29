
; incdec.asm - Increment and decrement
;
; Operates in-place on ptr0
;
; Inputs:
;   ptr0 - buffer to modify
;   arg0 - byte count
; Clobbers: A, Y

; Zero page location
.include "nlib.inc"

.proc _incN
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    clc
    adc #1
    sta (ptr0), y
    bne @done      ; No carry -> done
    iny
    dex
    bne @loop
@done:
    rts
.endproc

.proc _decN
    ldx arg0
    ldy #0
    sec
    lda (ptr0), y
    sbc #1
    sta (ptr0), y
    iny
    dex
    beq @done
@loop:
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    ;bne @done      ; No borrow -> done
    iny
    dex
    bne @loop
@done:
    rts
.endproc

; Fixed width versions

.proc _inc8
    ldy #0
    clc
    lda (ptr0), y
    adc #1
    sta (ptr0), y
    rts
.endproc

.proc _inc16
    ldy #0
    clc
    lda (ptr0), y
    adc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    rts
.endproc

.proc _inc24
    ldy #0
    clc
    lda (ptr0), y
    adc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    rts
.endproc

.proc _inc32
    ldy #0
    clc
    lda (ptr0), y
    adc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc #0
    sta (ptr0), y
    rts
.endproc

.proc _dec8
    ldy #0
    sec
    lda (ptr0), y
    sbc #1
    sta (ptr0), y
    rts
.endproc

.proc _dec16
    ldy #0
    sec
    lda (ptr0), y
    sbc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    rts
.endproc

.proc _dec24
    ldy #0
    sec
    lda (ptr0), y
    sbc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    rts
.endproc

.proc _dec32
    ldy #0
    sec
    lda (ptr0), y
    sbc #1
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc #0
    sta (ptr0), y
    rts
.endproc
