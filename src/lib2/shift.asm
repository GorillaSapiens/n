
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
n_shift   = $06
n_byte    = $07
n_bit     = $08
bytecount = $09

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

; Logical shift left by N bits (N in A)
; Uses lsl8 and lsl1
.proc lslN
    sta n_shift
    and #7
    sta n_bit
    lda n_shift
    lsr
    lsr
    lsr
    sta n_byte
    lda n_bit
    beq fini1_lslN   ; label
loop1_lslN:          ; label
    ldx size
    jsr lsl1         ; here
    dec n_bit
    bne loop1_lslN   ; label
fini1_lslN:          ; label
    lda n_byte
    beq fini2_lslN   ; label
loop2_lslN:          ; label
    ldx size
    jsr lsl8
    dec n_byte
    bne loop2_lslN   ; label
fini2_lslN:          ; label
    rts
.endproc

; Logical shift right by N bits (N in A)
; Uses lsr8 and lsr1
.proc lsrN
    sta n_shift
    lda n_shift
    lsr
    lsr
    lsr
    sta n_byte
    lda n_shift
    and #7
    sta n_bit

byte_loop_lsrN:
    lda n_byte
    beq bit_shifts_lsrN
    jsr lsr8
    dec n_byte
    jmp byte_loop_lsrN

bit_shifts_lsrN:
    lda n_bit
    beq done_lsrN
bit_loop_lsrN:
    jsr lsr1
    dec n_bit
    bne bit_loop_lsrN

done_lsrN:
    rts
.endproc

; Arithmetic shift right by N bits (N in A)
; Uses asr8 and asr1
.proc asrN
    sta n_shift
    lda n_shift
    lsr
    lsr
    lsr
    sta n_byte
    lda n_shift
    and #7
    sta n_bit

byte_loop_asrN:
    lda n_byte
    beq bit_shifts_asrN
    jsr asr8
    dec n_byte
    jmp byte_loop_asrN

bit_shifts_asrN:
    lda n_bit
    beq done_asrN
bit_loop_asrN:
    jsr asr1
    dec n_bit
    bne bit_loop_asrN

done_asrN:
    rts
.endproc
