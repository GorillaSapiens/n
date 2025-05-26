
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
;   size - byte count
; Clobbers: A, X, Y

.include "nlib.inc"
; we use ptr3 and ptr4
n_shift   = $0A
n_byte    = $0B
n_bit     = $0C
bytecount = $0D

.proc lsl1
    ldx size
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
    ldy size
    dey
    clc ; clear carry using SEC followed by ROR
@loop:
    lda (ptr1), y
    ror
    sta (ptr2), y
    dey
    bmi @loop
    rts
.endproc

.proc asr1
    ldy size
    dey
    lda (ptr1), y
    asl           ; places the high bit in Carry
@loop:
    lda (ptr1), y
    ror
    sta (ptr2), y
    dey
    bpl @loop
    rts
.endproc

; Logical shift left by 8 bits (1 byte)
.proc lsl8
    ldy #0
    lda #0
    sta (ptr2), y
@loop:
    lda (ptr1), y
    iny
    cpy size
    beq @fini
    sta (ptr2), y
    jmp @loop
@fini:
    rts
.endproc

; Logical shift right by 8 bits (1 byte)
.proc lsr8
    ldy size
    dey
    lda #0
    sta (ptr2), y
@loop:
    lda (ptr1), y
    dey
    bmi @fini
    sta (ptr2), y
    jmp @loop
@fini:
    rts
.endproc

; Arithmetic shift right by 8 bits (1 byte)

.proc asr8
    ldy size
    dey
    lda (ptr1), y
    rol
    bcs @neg
    lda #0
    sta (ptr2), y
    jmp @loop
@neg:
    lda #$FF
    sta (ptr2), y
@loop:
    lda (ptr1), y
    dey
    bmi @fini
    sta (ptr2), y
    jmp @loop
@fini:
    rts
.endproc

; Logical shift left by N bits (N in shift)
; Uses lsl8 and lsl1
.proc shiftN
    jmp @start
@trampoline1:
    jmp (ptr3)
@trampoline8:
    jmp (ptr4)
@start:
    lda shift
    sta n_shift
    and #7
    sta n_bit
    lda n_shift
    lsr
    lsr
    lsr
    sta n_byte
    lda n_bit
    beq @fini1
@loop1:
    ldx size
    jsr @trampoline1
    dec n_bit
    bne @loop1
@fini1:
    lda n_byte
    beq @fini2
@loop2:
    ldx size
    jsr @trampoline8
    dec n_byte
    bne @loop2
@fini2:
    rts
.endproc

.proc lslN
    lda #<lsl1
    sta ptr3
    lda #>lsl1
    sta ptr3+1

    lda #<lsl8
    sta ptr4
    lda #>lsl8
    sta ptr4+1

    jmp shiftN
.endproc

.proc lsrN
    lda #<lsr1
    sta ptr3
    lda #>lsr1
    sta ptr3+1

    lda #<lsr8
    sta ptr4
    lda #>lsr8
    sta ptr4+1

    jmp shiftN
.endproc

.proc asrN
    lda #<asr1
    sta ptr3
    lda #>asr1
    sta ptr3+1

    lda #<asr8
    sta ptr4
    lda #>asr8
    sta ptr4+1

    jmp shiftN
.endproc
