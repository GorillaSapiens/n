
; incdec.asm - Increment and decrement
;
; Operates in-place on ptr1
;
; Inputs:
;   ptr1 - buffer to modify
;   X    - byte count
; Clobbers: A, Y

; Zero page location
.include "nlib.inc"

.proc _incN
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    clc
    adc #1
    sta (ptr1), y
    bne @done      ; No carry → done
    iny
    dex
    bne @loop
@done:
    rts
.endproc

.proc _decN
    ldx size
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    iny
    dex
    beq @done
@loop:
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    ;bne @done      ; No borrow → done
    iny
    dex
    bne @loop
@done:
    rts
.endproc
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    rts

; Fixed width versions

.proc _inc8
    ldy #0
    clc
    lda (ptr1), y
    adc #1
    sta (ptr1), y
    rts
.endproc

.proc _inc16
    ldy #0
    clc
    lda (ptr1), y
    adc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    rts
.endproc

.proc _inc24
    ldy #0
    clc
    lda (ptr1), y
    adc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    rts
.endproc

.proc _inc32
    ldy #0
    clc
    lda (ptr1), y
    adc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    adc #0
    sta (ptr1), y
    rts
.endproc

.proc _dec8
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    rts
.endproc

.proc _dec16
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    rts
.endproc

.proc _dec24
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    rts
.endproc

.proc _dec32
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    iny
    lda (ptr1), y
    sbc #0
    sta (ptr1), y
    rts
.endproc
