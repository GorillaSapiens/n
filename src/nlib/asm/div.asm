; div.asm - Arbitrary-length unsigned division
;
; Divides ptr1 (dividend) by ptr2 (divisor), X bytes each.
; Stores quotient in ptr3, remainder in ptr4.
;
; Inputs:
;   ptr1 - dividend (X bytes), must be writable!
;   ptr2 - divisor (X bytes)
;   ptr3 - quotient (X bytes), must be writable!
;   ptr4 - remainder (X bytes), must be writable!
;   size - byte count
; Clobbers: A, X, Y, and zero page temps

.include "nlib.inc"
tmpX  = _nl_tmp1 ;$0A
carry = _nl_tmp2 ;$0B

.proc _divN
    ; Clear quotient and remainder
    ldy #0
@clear_loop:
    lda #0
    sta (ptr3), y
    sta (ptr4), y
    iny
    cpy size
    bne @clear_loop

    ; Perform size * 8 division steps
    ldx #0
    lda size
    asl
    asl
    asl
    sta tmpX         ; tmpX = bit count = size * 8

@bit_loop:
    ; Shift dividend left by 1
    clc
    ldx size
    dex
    ldy #0
@shift_div:
    lda (ptr1), y
    rol a
    sta (ptr1), y
    iny
    dex
    bpl @shift_div

    ; save that carry bit so we can restore div
    bcs @onward
    ldx #0
@onward:
    stx carry

    ; Shift next bit from dividend into remainder
    ldx size
    dex
    ldy #0
@shift_rem:
    lda (ptr4), y
    rol a
    sta (ptr4), y
    iny
    dex
    bpl @shift_rem

    ; use carry to set the low bit of dividend
    ldy #0
    lda carry
    and #1
    ora (ptr1),y
    sta (ptr1),y

    ; Compare remainder with divisor
    jsr @cmp_rev_div
    bcc @skip_subtract

    jsr @sub_div_from_rem ; remainder -= divisor
    sec                  ; Set quotient bit
   
@skip_subtract:
    ldx size
    dex
    ldy #0
@store_qbit:
    lda (ptr3), y
    rol a
    sta (ptr3), y
    iny
    dex
    bpl @store_qbit

    dec tmpX
    lda tmpX
    bne @bit_loop

    rts

; Compare remainder with divisor
@cmp_rev_div:
    ldy size
    dey
@cmp_loop:
    lda (ptr4), y
    cmp (ptr2), y
    bne @finish_cmp
    dey
    bpl @cmp_loop
    sec
    rts

@finish_cmp:
    bcc @lt
    sec
    rts
@lt:
    clc
    rts

; Subtract divisor from remainder: ptr4 -= ptr2
@sub_div_from_rem:
    ldx size
    dex
    ldy #0
    sec
@sub_loop:
    lda (ptr4), y
    sbc (ptr2), y
    sta (ptr4), y
    iny
    dex
    bpl @sub_loop
    rts

.endproc
