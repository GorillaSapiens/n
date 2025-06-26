; mul.asm - Arbitrary-length unsigned multiplication using proper 8x8 arg1-and-add
;
; Multiply *ptr0 * *ptr1 and store into *ptr2.
; X = byte count of inputs (result is up to 2X bytes)
;
; Inputs:
;   ptr0 - pointer to multiplicand buffer
;   ptr1 - pointer to multiplier buffer
;   ptr2 - pointer to result buffer (2X bytes, must be zero-initialized beforehand)
;   arg0 - byte count
; Clobbers: A, X, Y, zero page temp vars

.include "nlib.inc"
product_lo = _nl_ptr3   ;$0C
product_hi = _nl_ptr3+1 ;$0D
byte_b     = _nl_tmp0   ;$08
tmp_b      = _nl_tmp1   ;$09
a_lo       = _nl_tmp2   ;$0A
a_hi       = _nl_tmp3   ;$0B
outer      = _nl_tmp4   ;$0E
inner      = _nl_tmp5   ;$0F


.proc _mulN
    lda #0               ; zero out inner and outer loop counters
    sta outer
    sta inner

    ldy #0               ; clear the result
    ldx #0
@clear_ptr2:
    sta (ptr2), y
    iny
    sta (ptr2), y
    iny
    inx
    cpx arg0
    bne @clear_ptr2

@outer_loop:
    ldy outer
    cpy arg0
    beq @outer_fini

    lda (ptr1), y          ; initialize b
    sta byte_b

@inner_loop:
    ldy inner
    cpy arg0
    beq @inner_fini

    lda (ptr0), y          ; initialize a
    sta a_lo
    lda #0
    sta a_hi

    sta product_lo         ; initialize product
    sta product_hi

    ldx #8                 ; 8x8 multiply begins
    lda byte_b
    sta tmp_b
@mult_loop:
    lsr tmp_b
    bcc @no_add

    clc
    lda product_lo
    adc a_lo
    sta product_lo
    lda product_hi
    adc a_hi
    sta product_hi

@no_add:
    asl a_lo
    rol a_hi

    dex
    bne @mult_loop

    ; add product to ptr2
    clc
    lda inner
    adc outer
    tay
    clc                ; just to be safe
    lda (ptr2), y
    adc product_lo
    sta (ptr2), y
    iny
    lda (ptr2), y
    adc product_hi
    sta (ptr2), y
    iny
    lda (ptr2), y
    adc #0
    sta (ptr2), y

    inc inner
    jmp @inner_loop

@inner_fini:
    lda #0
    sta inner

    inc outer
    jmp @outer_loop

@outer_fini:
    rts
.endproc
