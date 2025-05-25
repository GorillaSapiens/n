
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

.proc incN
    ldx size
    ldy #0
inc_loop:
    lda (ptr1), y
    clc
    adc #1
    sta (ptr1), y
    bne inc_done      ; No carry → done
    iny
    dex
    bne inc_loop
inc_done:
    rts
.endproc

.proc decN
    ldx size
    ldy #0
dec_loop:
    lda (ptr1), y
    sec
    sbc #1
    sta (ptr1), y
    bne dec_done      ; No borrow → done
    iny
    dex
    bne dec_loop
dec_done:
    rts
.endproc

; Fixed width versions

.proc inc8
    ldy #0
    clc
    lda (ptr1), y
    adc #1
    sta (ptr1), y
    rts
.endproc

.proc inc16
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

.proc inc32
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

.proc dec8
    ldy #0
    sec
    lda (ptr1), y
    sbc #1
    sta (ptr1), y
    rts
.endproc

.proc dec16
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

.proc dec32
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
