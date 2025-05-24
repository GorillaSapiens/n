
; bitwise.asm - AND, OR, NOT
;
; Arbitrary-width bitwise logic on buffers
; Inputs:
;   ptr1, ptr2 - input buffers (ptr2 not needed for NOT)
;   ptr3 - destination
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

; Zero page locations assumed
ptr1     = $00
ptr2     = $02
ptr3     = $04

.proc and_bytes
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

.proc or_bytes
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

.proc not_bytes
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
