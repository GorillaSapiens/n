
; arg1.asm - Bit arg1ing routines
;
; Implements:
; - lsl1: logical arg1 left by 1
; - lsr1: logical arg1 right by 1
; - asr1: arithmetic arg1 right by 1 (signed)
;
; - lsl8: logical arg1 left by 8
; - lsr8: logical arg1 right by 8
; - asr8: arithmetic arg1 right by 8 (signed)
;
; - lslN: logical arg1 left by N
; - lsrN: logical arg1 right by N
; - asrN: arithmetic arg1 right by N (signed)
;
; Inputs:
;   ptr0  - source, modified in place for *1 and *8
;   ptr1  - destination for *N
;   arg0  - byte count
;   arg1 - bits to arg1 for N
; Clobbers: A, X, Y

.include "nlib.inc"
; lslN lsrN asrN also use ptr2 and ptr3
.def n_byte  _nl_tmp0
.def n_bit   _nl_tmp1
.def tmp     _nl_tmp2
.def swaptmp _nl_tmp3

.proc _lsl1
    ldx arg0
    ldy #0
    clc
@loop:
    lda (ptr0), y
    rol
    sta (ptr0), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _lsr1
    ldy arg0
    dey
    clc
@loop:
    lda (ptr0), y
    ror
    sta (ptr0), y
    dey
    bpl @loop
    rts
.endproc

.proc _asr1
    ldy arg0
    dey
    lda (ptr0), y
    asl           ; places the high bit in Carry
@loop:
    lda (ptr0), y
    ror
    sta (ptr0), y
    dey
    bpl @loop
    rts
.endproc

; Logical arg1 left by 8 bits (1 byte)
.proc _lsl8
    ldy arg0
    dey
    dey
    bmi @fini
@loop:
    lda (ptr0), y
    iny
    sta (ptr0), y
    dey
    dey
    bpl @loop
@fini:
    iny
    lda #0
    sta (ptr0), y
    rts
.endproc

; Logical arg1 right by 8 bits (1 byte)
.proc _lsr8
    ldy #0
    ldx arg0
    dex
    dex
    bmi @fini
@loop:
    iny
    lda (ptr0), y
    dey
    sta (ptr0), y
    iny
    dex
    bpl @loop
@fini:
    lda #0
    sta (ptr0), y
    rts
.endproc

; Arithmetic arg1 right by 8 bits (1 byte)

.proc _asr8
    lda #0
    sta tmp
    ldy arg0
    dey
    lda (ptr0), y
    bpl @skip
    lda #$FF
    sta tmp
@skip:
    ldy #0
    ldx arg0
    dex
    dex
    bmi @fini
@loop:
    iny
    lda (ptr0), y
    dey
    sta (ptr0), y
    iny
    dex
    bpl @loop
@fini:
    lda tmp
    sta (ptr0), y
    rts
.endproc

; Logical arg1 left by N bits (N in arg1)
; Uses lsl8 and lsl1
.proc _arg1N
    jmp @start
@trampoline1:
    jmp (ptr2)
@trampoline8:
    jmp (ptr3)
@start:
    ldy arg0
    dey
@copy:
    lda (ptr0), y
    sta (ptr1), y
    dey
    bpl @copy

    lda arg1
    and #7
    sta n_bit
    lda arg1
    lsr
    lsr
    lsr
    sta n_byte
    lda n_bit
    beq @fini1
@loop1:
    jsr @trampoline1
    dec n_bit
    bne @loop1
@fini1:
    lda n_byte
    beq @fini2
@loop2:
    jsr @trampoline8
    dec n_byte
    bne @loop2
@fini2:
    ldy arg0
    dey
@swap:
    lda (ptr0), y
    sta swaptmp
    lda (ptr1), y
    sta (ptr0), y
    lda swaptmp
    sta (ptr1), y
    dey
    bpl @swap
    rts
.endproc

.proc _lslN
    lda #<_lsl1
    sta ptr2
    lda #>_lsl1
    sta ptr2+1

    lda #<_lsl8
    sta ptr3
    lda #>_lsl8
    sta ptr3+1

    jmp _arg1N
.endproc

.proc _lsrN
    lda #<_lsr1
    sta ptr2
    lda #>_lsr1
    sta ptr2+1

    lda #<_lsr8
    sta ptr3
    lda #>_lsr8
    sta ptr3+1

    jmp _arg1N
.endproc

.proc _asrN
    lda #<_asr1
    sta ptr2
    lda #>_asr1
    sta ptr2+1

    lda #<_asr8
    sta ptr3
    lda #>_asr8
    sta ptr3+1

    jmp _arg1N
.endproc
