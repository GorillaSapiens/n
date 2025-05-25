
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
loop_lsl1:
    lda (ptr1), y
    rol
    sta (ptr2), y
    iny
    dex
    bne loop_lsl1
    rts
.endproc

.proc lsr1
    ldy size
    dey
    sec         ; clear carry using SEC followed by ROR
loop_lsr1:
    lda (ptr1), y
    ror
    sta (ptr2), y
    dey
    cpy #$FF
    bne loop_lsr1
    rts
.endproc

.proc asr1
    ldy size
    dey
    sec
loop_asr1:
    lda (ptr1), y
    ror
    ; inject sign bit into top byte
    cpy #0
    bne store_asr1
    ; top byte: preserve sign
    bmi store_asr1
    and #%01111111
store_asr1:
    sta (ptr2), y
    dey
    cpy #$FF
    bne loop_asr1
    rts
.endproc

; Logical shift left by 8 bits (1 byte)
.proc lsl8
    ldy #0
    lda #0
    sta (ptr2), y
loop_lsl8:
    lda (ptr1), y
    iny
    cpy size
    beq fini_lsl8
    sta (ptr2), y
    jmp loop_lsl8
fini_lsl8:
    rts
.endproc

; Logical shift right by 8 bits (1 byte)
.proc lsr8
    ldy size
    dey
    lda #0
    sta (ptr2), y
loop_lsr8:
    lda (ptr1), y
    dey
    bmi fini_lsr8
    sta (ptr2), y
    jmp loop_lsr8
fini_lsr8:
    rts
.endproc

; Arithmetic shift right by 8 bits (1 byte)

.proc asr8
    ldy size
    dey
    lda (ptr1), y
    rol
    bcs neg_asr8
    lda #0
    sta (ptr2), y
    jmp loop_asr8
neg_asr8:
    lda #$FF
    sta (ptr2), y
loop_asr8:
    lda (ptr1), y
    dey
    bmi fini_asr8
    sta (ptr2), y
    jmp loop_asr8
fini_asr8:
    rts
.endproc

; Logical shift left by N bits (N in shift)
; Uses lsl8 and lsl1
.proc shiftN
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
    beq fini1_shiftN   ; label
loop1_shiftN:          ; label
    ldx size
    jsr trampoline1    ; here
    dec n_bit
    bne loop1_shiftN   ; label
fini1_shiftN:          ; label
    lda n_byte
    beq fini2_shiftN   ; label
loop2_shiftN:          ; label
    ldx size
    jsr trampoline2
    dec n_byte
    bne loop2_shiftN   ; label
fini2_shiftN:          ; label
    rts
trampoline1:
    jmp (ptr3)
trampoline8:
    jmp (ptr4)
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
