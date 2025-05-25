
; cmp.asm - Comparison routines
;
; Implements:
; - eq: equal
; - lt: less than
; - le: less or equal
;
; Returns result in A and shift: 1 if true, 0 if false
;
; Inputs:
;   ptr1 - input buffer A
;   ptr2 - input buffer B
;   size - byte count
; Clobbers: A, X, Y

,include "nlib.inc"

.proc eq
    ldx size
    ldy #0
@loop:
    lda (ptr1), y
    cmp (ptr2), y
    bne @false
    iny
    dex
    bne @loop
    lda #1      ; equal
    sta shift
    rts
@false:
    lda #0      ; not equal
    sta shift
    rts
.endproc

.proc lt_signed
    ; Signed less-than
    ; Compare MSB for sign difference first
    ldy size
    dey                 ; last byte
    lda (ptr1), y
    cmp (ptr2), y
    beq @compare_unsigned
    bpl @check_if_b_negative
    ; A is negative, B is positive → A < B
    lda #1
    sta shift
    rts
@check_if_b_negative:
    ; A is positive, B is negative → A > B
    lda #0
    sta shift
    rts
@compare_unsigned:
    jsr lt_unsigned
    rts
.endproc

.proc le_signed
    ; Signed less-than-or-equal
    ldy size
    dey                 ; last byte
    lda (ptr1), y
    cmp (ptr2), y
    beq @compare_unsigned
    bpl @check_if_b_negative
    ; A is negative, B is positive → A < B
    lda #1
    sta shift
    rts
@check_if_b_negative:
    ; A is positive, B is negative → A > B
    lda #0
    sta shift
    rts
@compare_unsigned:
    jsr le_unsigned
    rts
.endproc

.proc lt_unsigned
    ; Unsigned less-than comparison
    ; Compare from most significant byte to least
    ldy size
@loop:
    dey
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true   ; A < B
    bne @false  ; A > B
    cpy #0
    bne @loop
@false:
    lda #0
    sta shift
    rts
@true:
    lda #1
    sta shift
    rts
.endproc

.proc le_unsigned
    ; Unsigned less-than-or-equal comparison
    ; Compare from most significant byte to least
    ldy size
@loop:
    dey
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true   ; A < B
    bne @false  ; A > B
    cpy #0
    bne @loop
@true:
    lda #1
    sta shift
    rts
@false:
    lda #0
    sta shift
    rts
.endproc
