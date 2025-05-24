
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

.include "zp.inc"

shift_count = $08
byte_count  = $09
bit_count   = $0A


; Zero page addresses
ptr1 = $00
ptr2 = $02

.proc lsl1
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
    txa
    tay
    dey
    sec         ; clear carry using SEC followed by ROR
@loop:
    lda (ptr1), y
    ror
    sta (ptr2), y
    dey
    cpy #$FF
    bne @loop
    rts
.endproc

.proc asr1
    txa
    tay
    dey
    sec
@loop:
    lda (ptr1), y
    ror
    ; inject sign bit into top byte
    cpy #0
    bne @store
    ; top byte: preserve sign
    bmi @store
    and #%01111111
@store:
    sta (ptr2), y
    dey
    cpy #$FF
    bne @loop
    rts
.endproc

; Logical shift left by 8 bits (1 byte)
.proc lsl8
    ldy #1
@loop:
    cpy X
    beq @fill
    lda (ptr1), y
    dey
    sta (ptr2), y
    iny
    bne @loop
@fill:
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
@loop:
    cpy #0
    beq @fill
    dey
    lda (ptr1), y
    iny
    sta (ptr2), y
    dey
    bne @loop
@fill:
    lda #0
    ldy #0
    sta (ptr2), y
    rts
.endproc

; Arithmetic shift right by 8 bits (1 byte)
.proc asr8
    txa
    tay
    dey
    lda (ptr1), y
    bmi @neg
    lda #0
    sta (ptr2), y
    jmp @copy
@neg:
    lda #$FF
    sta (ptr2), y
@copy:
    dey
@loop:
    cpy #$FF
    beq @done
    lda (ptr1), y
    iny
    sta (ptr2), y
    dey
    jmp @loop
@done:
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
@byte_loop:
    lda byte_count
    beq @bit_shifts
    jsr lsl8
    dec byte_count
    jmp @byte_loop

@bit_shifts:
    lda bit_count
    beq @done
@bit_loop:
    jsr lsl1
    dec bit_count
    bne @bit_loop

@done:
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

@byte_loop:
    lda byte_count
    beq @bit_shifts
    jsr lsr8
    dec byte_count
    jmp @byte_loop

@bit_shifts:
    lda bit_count
    beq @done
@bit_loop:
    jsr lsr1
    dec bit_count
    bne @bit_loop

@done:
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

@byte_loop:
    lda byte_count
    beq @bit_shifts
    jsr asr8
    dec byte_count
    jmp @byte_loop

@bit_shifts:
    lda bit_count
    beq @done
@bit_loop:
    jsr asr1
    dec bit_count
    bne @bit_loop

@done:
    rts
.endproc
