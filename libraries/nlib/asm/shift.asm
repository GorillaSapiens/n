; arg1.asm - Bit arg1ing routines
;
; Implements:
; - lsl1: logical arg1 left by 1
; - lsr1: logical arg1 right by 1
; - asr1: arithmetic arg1 right by 1 (signed)
;
; - lslN: logical arg1 left by N
; - lsrN: logical arg1 right by N
; - asrN: arithmetic arg1 right by N (signed)
;
; Inputs:
;   ptr0  - source, modified in place for *1, read-only for *N
;   ptr1  - destination for *N
;   arg0  - byte count
;   arg1  - bits to shift for N
; Clobbers: A, X, Y

.include "nlib.inc"
; lslN lsrN asrN also use ptr2 and ptr3
.def n_byte  _nl_tmp0
.def n_bit   _nl_tmp1
.def tmp     _nl_tmp2

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

; Apply the remaining 1-bit shifts to ptr1 in place.
.proc _arg1N
    jmp @start
@trampoline1:
    jmp (ptr3)
@start:
    lda ptr0
    pha
    lda ptr0+1
    pha

    lda ptr1
    sta ptr0
    lda ptr1+1
    sta ptr0+1

    lda arg1
    and #7
    sta n_bit
    beq @done
@loop:
    jsr @trampoline1
    dec n_bit
    bne @loop
@done:
    pla
    sta ptr0+1
    pla
    sta ptr0
    rts
.endproc

.proc _lslN
    lda #<_lsl1
    sta ptr3
    lda #>_lsl1
    sta ptr3+1

    lda arg1
    lsr
    lsr
    lsr
    sta n_byte
    cmp arg0
    bcc @copy_bytes

    ldy arg0
    dey
    lda #0
@fill_all:
    sta (ptr1), y
    dey
    bpl @fill_all
    rts

@copy_bytes:
    lda ptr0
    sec
    sbc n_byte
    sta ptr2
    lda ptr0+1
    sbc #0
    sta ptr2+1

    ldy arg0
    dey
@copy:
    cpy n_byte
    bcc @fill_low
    lda (ptr2), y
    sta (ptr1), y
    dey
    bpl @copy
    jsr _arg1N
    rts

@fill_low:
    lda #0
@fill_low_loop:
    sta (ptr1), y
    dey
    bpl @fill_low_loop
    jsr _arg1N
    rts
.endproc

.proc _lsrN
    lda #<_lsr1
    sta ptr3
    lda #>_lsr1
    sta ptr3+1

    lda arg1
    lsr
    lsr
    lsr
    sta n_byte
    cmp arg0
    bcc @copy_bytes

    ldy #0
    lda #0
@fill_all:
    sta (ptr1), y
    iny
    cpy arg0
    bcc @fill_all
    rts

@copy_bytes:
    lda ptr0
    clc
    adc n_byte
    sta ptr2
    lda ptr0+1
    adc #0
    sta ptr2+1

    lda arg0
    sec
    sbc n_byte
    tax
    ldy #0
@copy:
    lda (ptr2), y
    sta (ptr1), y
    iny
    dex
    bne @copy

    lda #0
@fill_high:
    cpy arg0
    bcs @post_bits
    sta (ptr1), y
    iny
    bne @fill_high
@post_bits:
    jsr _arg1N
    rts
.endproc

.proc _asrN
    lda #<_asr1
    sta ptr3
    lda #>_asr1
    sta ptr3+1

    ldy arg0
    dey
    lda (ptr0), y
    bmi @negative
    lda #0
    jmp @got_fill
@negative:
    lda #$ff
@got_fill:
    sta tmp

    lda arg1
    lsr
    lsr
    lsr
    sta n_byte
    cmp arg0
    bcc @copy_bytes

    ldy #0
    lda tmp
@fill_all:
    sta (ptr1), y
    iny
    cpy arg0
    bcc @fill_all
    rts

@copy_bytes:
    lda ptr0
    clc
    adc n_byte
    sta ptr2
    lda ptr0+1
    adc #0
    sta ptr2+1

    lda arg0
    sec
    sbc n_byte
    tax
    ldy #0
@copy:
    lda (ptr2), y
    sta (ptr1), y
    iny
    dex
    bne @copy

    lda tmp
@fill_high:
    cpy arg0
    bcs @post_bits
    sta (ptr1), y
    iny
    bne @fill_high
@post_bits:
    jsr _arg1N
    rts
.endproc
