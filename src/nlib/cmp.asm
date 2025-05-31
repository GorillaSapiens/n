
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

.include "nlib.inc"

.proc _eqN
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

.proc _ltNs
    ldy size
    dey
    lda (ptr1), y
    eor (ptr2), y
    bmi @differ

    lda (ptr1), y
    bmi @both_neg
@both_pos:
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    jmp @false
@both_neg:
    lda (ptr2), y
    cmp (ptr1), y
    bcc @false
    bne @true
    dey
    bpl @both_neg
    jmp @false
@differ:
    lda (ptr1), y
    bpl @false
    ; fallthrough
@true:
    lda #1
    sta shift
    rts
@false:
    lda #0
    sta shift
    rts
.endproc

.proc _leNs
    ldy size
    dey
    lda (ptr1), y
    eor (ptr2), y
    bmi @differ

    lda (ptr1), y
    bmi @both_neg
@both_pos:
    lda (ptr1), y
    cmp (ptr2), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    jmp @true
@both_neg:
    lda (ptr2), y
    cmp (ptr1), y
    bcc @false
    bne @true
    dey
    bpl @both_neg
    jmp @true
@differ:
    lda (ptr1), y
    bpl @false
    ; fallthrough
@true:
    lda #1
    sta shift
    rts
@false:
    lda #0
    sta shift
    rts
.endproc

.proc _ltNu
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

.proc _leNu
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
