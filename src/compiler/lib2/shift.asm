
; shift.asm - Bit shifting routines
;
; Implements:
; - lsl1: logical shift left by 1
; - lsr1: logical shift right by 1
; - asr1: arithmetic shift right by 1 (signed)
;
; Inputs:
;   ptr1 - source
;   ptr2 - destination
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

; Zero page addresses
ptr1 = $00
ptr2 = $02

.proc lsl1
    ldy #0
    clc
@loop:
    lda (ptr1), y
    rol
    sta (ptr2), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc lsr1
    ldy X
    dey
    sec         ; clear carry using SEC followed by ROR
@loop:
    lda (ptr1), y
    ror
    sta (ptr2), y
    dey
    cpy #$FF
    bne @loop
    rts
.endproc

.proc asr1
    ldy X
    dey
    sec
@loop:
    lda (ptr1), y
    ror
    ; inject sign bit into top byte
    cpy #0
    bne @store
    ; top byte: preserve sign
    bmi @store
    and #%01111111
@store:
    sta (ptr2), y
    dey
    cpy #$FF
    bne @loop
    rts
.endproc
