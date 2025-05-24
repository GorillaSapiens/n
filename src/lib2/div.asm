; div.asm - Arbitrary-length unsigned division
;
; Divides ptr1 (dividend) by ptr2 (divisor), X bytes each.
; Stores quotient in ptr3, remainder in ptr4.
;
; Inputs:
;   ptr1 - dividend (X bytes, will be clobbered)
;   ptr2 - divisor (X bytes)
;   ptr3 - quotient (X bytes)
;   ptr4 - remainder (X bytes)
;   X    - byte count
; Clobbers: A, X, Y, and zero page temps

.include "zp.inc"

ptr1  = $00
ptr2  = $02
ptr3  = $04
ptr4  = $06
tmpX  = $08
tmpY  = $09
carry = $0A
zero  = $0B
size  = $0C

.proc div_unsigned
    stx size

    ; Clear quotient and remainder
    ldy #0
clear_loop:
    lda #0
    sta (ptr3), y
    sta (ptr4), y
    iny
    cpy size
    bne clear_loop

    ; Perform size * 8 division steps
    ldx #0
    lda size
    asl
    asl
    asl
    sta tmpX         ; tmpX = bit count = size * 8

bitloop:
    ; Shift remainder left by 1
    clc
    ldy size
    dey
shift_div:
    lda (ptr1), y
    rol a
    sta (ptr1), y
    dey
    bpl shift_div

    ; Shift next bit from dividend into remainder
    ldy size
    dey
shift_rem:
    lda (ptr4), y
    rol a
    sta (ptr4), y
    dey
    bpl shift_rem

    ; Compare remainder with divisor
    jsr cmp_rem_div
    bcc skip_subtract

    ; remainder -= divisor
    jsr sub_div_from_rem

    ; Set quotient bit
    ldy size
    dey
store_qbit:
    lda (ptr3), y
    rol a
    sta (ptr3), y
    dey
    bpl store_qbit

skip_subtract:
    dec tmpX
    lda tmpX
    bne bitloop

    rts
.endproc

; Compare remainder with divisor
cmp_rem_div:
    ldy size
    dey
cmp_loop:
    lda (ptr4), y
    cmp (ptr2), y
    bne finish_cmp
    dey
    bpl cmp_loop
    sec
    rts

finish_cmp:
    bcc lt
    sec
    rts
lt:
    clc
    rts

; Subtract divisor from remainder: ptr4 -= ptr2
sub_div_from_rem:
    ldy size
    dey
    sec
sub_loop:
    lda (ptr4), y
    sbc (ptr2), y
    sta (ptr4), y
    dey
    bpl sub_loop
    rts
