; div.asm - Arbitrary-length unsigned division
;
; Divides ptr0 (dividend) by ptr1 (divisor), X bytes each.
; Stores quotient in ptr2, remainder in ptr3.
;
; Inputs:
;   ptr0 - dividend (X bytes)
;   ptr1 - divisor (X bytes)
;   ptr2 - quotient (X bytes), must be writable!
;   ptr3 - remainder (X bytes), must be writable!
;   arg0 - byte count
; Clobbers: A, X, Y, and zero page temps
; NB: ALSO WRITES TO THE STACK!!!

.include "nlib.inc"
.def tmpX  _nl_tmp0
.def carry _nl_tmp1

.proc _divN
    ; copy dividend to the stack
    ldx arg0
    ldy #0
@cpy_loop:
    lda (ptr0), y
    sta (sp), y
    iny
    dex
    bne @cpy_loop

    ; Clear quotient and remainder
    ldy #0
@clear_loop:
    lda #0
    sta (ptr2), y
    sta (ptr3), y
    iny
    cpy arg0
    bne @clear_loop

    ; Perform arg0 * 8 division steps
    ldx #0
    lda arg0
    asl
    asl
    asl
    sta tmpX         ; tmpX = bit count = arg0 * 8

@bit_loop:
    ; Shift dividend left by 1
    clc
    ldx arg0
    dex
    ldy #0
@arg1_div:
    lda (sp), y
    rol a
    sta (sp), y
    iny
    dex
    bpl @arg1_div

    ; save that carry bit so we can restore div
    bcs @onward
    ldx #0
@onward:
    stx carry

    ; Shift next bit from dividend into remainder
    ldx arg0
    dex
    ldy #0
@arg1_rem:
    lda (ptr3), y
    rol a
    sta (ptr3), y
    iny
    dex
    bpl @arg1_rem

    ; use carry to set the low bit of dividend
    ldy #0
    lda carry
    and #1
    ora (sp),y
    sta (sp),y

    ; Compare remainder with divisor
    jsr @cmp_rev_div
    bcc @skip_subtract

    jsr @sub_div_from_rem ; remainder -= divisor
    sec                  ; Set quotient bit
   
@skip_subtract:
    ldx arg0
    dex
    ldy #0
@store_qbit:
    lda (ptr2), y
    rol a
    sta (ptr2), y
    iny
    dex
    bpl @store_qbit

    dec tmpX
    lda tmpX
    bne @bit_loop

    rts

; Compare remainder with divisor
@cmp_rev_div:
    ldy arg0
    dey
@cmp_loop:
    lda (ptr3), y
    cmp (ptr1), y
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

; Subtract divisor from remainder: ptr3 -= ptr1
@sub_div_from_rem:
    ldx arg0
    dex
    ldy #0
    sec
@sub_loop:
    lda (ptr3), y
    sbc (ptr1), y
    sta (ptr3), y
    iny
    dex
    bpl @sub_loop
    rts

.endproc
