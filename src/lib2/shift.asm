
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
;   X    - byte count
; Clobbers: A, Y

; Zero page addresses
ptr1 = $00
ptr2 = $02
size = $04
shift_count = $05
byte_count  = $06
bit_count   = $07
bytecount   = $08

.proc lsl1
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
    txa
    tay
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
    txa
    tay
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
    stx bytecount
loop_lsl8:
    cpy bytecount
    beq fill_lsl8
    lda (ptr1), y
    dey
    sta (ptr2), y
    iny
    bne loop_lsl8
fill_lsl8:
    lda #0
    dey
    sta (ptr2), y
    rts
.endproc

; Logical shift right by 8 bits (1 byte)
.proc lsr8
    txa
    tay
    dey
loop_lsr8:
    cpy #0
    beq fill_lsr8
    dey
    lda (ptr1), y
    iny
    sta (ptr2), y
    dey
    bne loop_lsr8
fill_lsr8:
    lda #0
    ldy #0
    sta (ptr2), y
    rts
.endproc

; Arithmetic shift right by 8 bits (1 byte)

.proc asr8
    sty size
    ldy #1
loop_asr8:
    lda (ptr1), y
    dey
    sta (ptr2), y
    iny
    iny
    cpy size
    bne loop_asr8
    dey
    ldx #$FF
    lda (ptr1), y
    bmi neg_asr8
    ldx #0
neg_asr8:
    txa
    sta (ptr2), y
    rts
.endproc

; Logical shift left by N bits (N in A)
; Uses lsl8 and lsl1
.proc lslN
    sta shift_count
    lda shift_count
    lsr             ; divide by 2 until quotient = N / 8
    lsr
    lsr
    sta byte_count
    lda shift_count
    and #7
    sta bit_count

    ; Byte shifts
byte_loop_lslN:
    lda byte_count
    beq bit_shifts_lslN
    jsr lsl8
    dec byte_count
    jmp byte_loop_lslN

bit_shifts_lslN:
    lda bit_count
    beq done_lslN
bit_loop_lslN:
    jsr lsl1
    dec bit_count
    bne bit_loop_lslN

done_lslN:
    rts
.endproc

; Logical shift right by N bits (N in A)
; Uses lsr8 and lsr1
.proc lsrN
    sta shift_count
    lda shift_count
    lsr
    lsr
    lsr
    sta byte_count
    lda shift_count
    and #7
    sta bit_count

byte_loop_lsrN:
    lda byte_count
    beq bit_shifts_lsrN
    jsr lsr8
    dec byte_count
    jmp byte_loop_lsrN

bit_shifts_lsrN:
    lda bit_count
    beq done_lsrN
bit_loop_lsrN:
    jsr lsr1
    dec bit_count
    bne bit_loop_lsrN

done_lsrN:
    rts
.endproc

; Arithmetic shift right by N bits (N in A)
; Uses asr8 and asr1
.proc asrN
    sta shift_count
    lda shift_count
    lsr
    lsr
    lsr
    sta byte_count
    lda shift_count
    and #7
    sta bit_count

byte_loop_asrN:
    lda byte_count
    beq bit_shifts_asrN
    jsr asr8
    dec byte_count
    jmp byte_loop_asrN

bit_shifts_asrN:
    lda bit_count
    beq done_asrN
bit_loop_asrN:
    jsr asr1
    dec bit_count
    bne bit_loop_asrN

done_asrN:
    rts
.endproc
