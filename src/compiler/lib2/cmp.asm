
; cmp.asm - Comparison routines
;
; Implements:
; - eq: equal
; - lt: less than
; - le: less or equal
;
; Returns result in A: 1 if true, 0 if false
;
; Inputs:
;   ptr1 - input buffer A
;   ptr2 - input buffer B
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

; Zero page locations assumed
ptr1     = $00
ptr2     = $02

.proc eq
    ldy #0
@loop:
    lda (ptr1), y
    cmp (ptr2), y
    bne @false
    iny
    dex
    bne @loop
    lda #1      ; equal
    rts
@false:
    lda #0      ; not equal
    rts
.endproc





.proc lt_signed
    ; Signed less-than
    ; Compare MSB for sign difference first
    txa
    tay
    dey                 ; last byte
    lda (ptr1), y
    cmp (ptr2), y
    beq @compare_unsigned
    bpl @check_if_b_negative
    ; A is negative, B is positive → A < B
    lda #1
    rts
@check_if_b_negative:
    ; A is positive, B is negative → A > B
    lda #0
    rts
@compare_unsigned:
    jsr lt_unsigned
    rts
.endproc

.proc le_signed
    ; Signed less-than-or-equal
    txa
    tay
    dey                 ; last byte
    lda (ptr1), y
    cmp (ptr2), y
    beq @compare_unsigned
    bpl @check_if_b_negative
    ; A is negative, B is positive → A < B
    lda #1
    rts
@check_if_b_negative:
    ; A is positive, B is negative → A > B
    lda #0
    rts
@compare_unsigned:
    jsr le_unsigned
    rts
.endproc

.proc lt_unsigned
    ; Unsigned less-than comparison
    ; Compare from most significant byte to least
@loop:
    dex
    txa
    tay
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true   ; A < B
    bne @false  ; A > B
    cpx #0
    bne @loop
@false:
    lda #0
    rts
@true:
    lda #1
    rts
.endproc

.proc le_unsigned
    ; Unsigned less-than-or-equal comparison
    ; Compare from most significant byte to least
@loop:
    dex
    txa
    tay
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true   ; A < B
    bne @false  ; A > B
    cpx #0
    bne @loop
@true:
    lda #1
    rts
@false:
    lda #0
    rts
.endproc
