
; cmp.asm - Comparison routines
;
; Implements:
; - eq: equal
; - lt: less than
; - le: less or equal
;
; Returns result in A and arg1: 1 if true, 0 if false
;
; Inputs:
;   ptr0 - input buffer A
;   ptr1 - input buffer B
;   arg0 - byte count
; Clobbers: A, X, Y

.include "nlib.inc"

.proc _eqN
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    cmp (ptr1), y
    bne @false
    iny
    dex
    bne @loop
    lda #1      ; equal
    sta arg1
    rts
@false:
    lda #0      ; not equal
    sta arg1
    rts
.endproc

.proc _ltNs
    ldy arg0
    dey
    lda (ptr0), y
    eor (ptr1), y
    bmi @differ

    lda (ptr0), y
    bmi @both_neg
@both_pos:
    lda (ptr0), y
    cmp (ptr1), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    jmp @false
@both_neg:
    lda (ptr1), y
    cmp (ptr0), y
    bcc @false
    bne @true
    dey
    bpl @both_neg
    jmp @false
@differ:
    lda (ptr0), y
    bpl @false
    ; fallthrough
@true:
    lda #1
    sta arg1
    rts
@false:
    lda #0
    sta arg1
    rts
.endproc

.proc _leNs
    ldy arg0
    dey
    lda (ptr0), y
    eor (ptr1), y
    bmi @differ

    lda (ptr0), y
    bmi @both_neg
@both_pos:
    lda (ptr0), y
    cmp (ptr1), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    jmp @true
@both_neg:
    lda (ptr1), y
    cmp (ptr0), y
    bcc @false
    bne @true
    dey
    bpl @both_neg
    jmp @true
@differ:
    lda (ptr0), y
    bpl @false
    ; fallthrough
@true:
    lda #1
    sta arg1
    rts
@false:
    lda #0
    sta arg1
    rts
.endproc

.proc _ltNu
    ldy arg0
    dey
@both_pos:
    lda (ptr0), y
    cmp (ptr1), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    ; fallthrough jmp @false
@false:
    lda #0
    sta arg1
    rts
@true:
    lda #1
    sta arg1
    rts
.endproc

.proc _leNu
    ldy arg0
    dey
@both_pos:
    lda (ptr0), y
    cmp (ptr1), y
    bcc @true
    bne @false
    dey
    bpl @both_pos
    ; fallthrough jmp @true
@true:
    lda #1
    sta arg1
    rts
@false:
    lda #0
    sta arg1
    rts
.endproc
