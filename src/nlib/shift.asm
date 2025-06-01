
; shift.asm - Bit shifting routines
;
; Implements:
; - lsl1: logical shift left by 1
; - lsr1: logical shift right by 1
; - asr1: arithmetic shift right by 1 (signed)
;
; - lsl8: logical shift left by 8
; - lsr8: logical shift right by 8
; - asr8: arithmetic shift right by 8 (signed)
;
; - lslN: logical shift left by N
; - lsrN: logical shift right by N
; - asrN: arithmetic shift right by N (signed)
;
; Inputs:
;   ptr1  - source, modified in place for *1 and *8
;   ptr2  - destination for *N
;   size  - byte count
;   shift - bits to shift for N
; Clobbers: A, X, Y

.include "nlib.inc"
; lslN lsrN asrN also use ptr3 and ptr4
n_byte    = _nl_tmp1 ;$0A
n_bit     = _nl_tmp2 ;$0B
tmp       = _nl_tmp3 ;$0C
swaptmp   = _nl_tmp4 ;$0D

.proc _lsl1
    ldx size
    ldy #0
    clc
@loop:
    lda (ptr1), y
    rol
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _lsr1
    ldy size
    dey
    clc
@loop:
    lda (ptr1), y
    ror
    sta (ptr1), y
    dey
    bpl @loop
    rts
.endproc

.proc _asr1
    ldy size
    dey
    lda (ptr1), y
    asl           ; places the high bit in Carry
@loop:
    lda (ptr1), y
    ror
    sta (ptr1), y
    dey
    bpl @loop
    rts
.endproc

; Logical shift left by 8 bits (1 byte)
.proc _lsl8
    ldy size
    dey
    dey
    bmi @fini
@loop:
    lda (ptr1), y
    iny
    sta (ptr1), y
    dey
    dey
    bpl @loop
@fini:
    iny
    lda #0
    sta (ptr1), y
    rts
.endproc

; Logical shift right by 8 bits (1 byte)
.proc _lsr8
    ldy #0
    ldx size
    dex
    dex
    bmi @fini
@loop:
    iny
    lda (ptr1), y
    dey
    sta (ptr1), y
    iny
    dex
    bpl @loop
@fini:
    lda #0
    sta (ptr1), y
    rts
.endproc

; Arithmetic shift right by 8 bits (1 byte)

.proc _asr8
    lda #0
    sta tmp
    ldy size
    dey
    lda (ptr1), y
    bpl @skip
    lda #$FF
    sta tmp
@skip:
    ldy #0
    ldx size
    dex
    dex
    bmi @fini
@loop:
    iny
    lda (ptr1), y
    dey
    sta (ptr1), y
    iny
    dex
    bpl @loop
@fini:
    lda tmp
    sta (ptr1), y
    rts
.endproc

; Logical shift left by N bits (N in shift)
; Uses lsl8 and lsl1
.proc _shiftN
    jmp @start
@trampoline1:
    jmp (ptr3)
@trampoline8:
    jmp (ptr4)
@start:
    ldy size
    dey
@copy:
    lda (ptr1), y
    sta (ptr2), y
    dey
    bpl @copy

    lda shift
    and #7
    sta n_bit
    lda shift
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
    ldy size
    dey
@swap:
    lda (ptr1), y
    sta swaptmp
    lda (ptr2), y
    sta (ptr1), y
    lda swaptmp
    sta (ptr2), y
    dey
    bpl @swap
    rts
.endproc

.proc _lslN
    lda #<_lsl1
    sta ptr3
    lda #>_lsl1
    sta ptr3+1

    lda #<_lsl8
    sta ptr4
    lda #>_lsl8
    sta ptr4+1

    jmp _shiftN
.endproc

.proc _lsrN
    lda #<_lsr1
    sta ptr3
    lda #>_lsr1
    sta ptr3+1

    lda #<_lsr8
    sta ptr4
    lda #>_lsr8
    sta ptr4+1

    jmp _shiftN
.endproc

.proc _asrN
    lda #<_asr1
    sta ptr3
    lda #>_asr1
    sta ptr3+1

    lda #<_asr8
    sta ptr4
    lda #>_asr8
    sta ptr4+1

    jmp _shiftN
.endproc
