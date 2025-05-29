; mul.asm - Arbitrary-length unsigned multiplication using proper 8x8 shift-and-add
;
; Multiply *ptr1 * *ptr2 and store into *ptr3.
; X = byte count of inputs (result is up to 2X bytes)
;
; Inputs:
;   ptr1 - pointer to multiplicand buffer
;   ptr2 - pointer to multiplier buffer
;   ptr3 - pointer to result buffer (2X bytes, must be zero-initialized beforehand)
;   size - byte count
; Clobbers: A, X, Y, zero page temp vars

.include "nlib.inc"
product_lo = _nl_ptr4   ;$0C
product_hi = _nl_ptr4+1 ;$0D
byte_b     = _nl_tmp1   ;$08
tmp_b      = _nl_tmp2   ;$09
a_lo       = _nl_tmp3   ;$0A
a_hi       = _nl_tmp4   ;$0B
outer      = _nl_tmp5   ;$0E
inner      = _nl_tmp6   ;$0F


.proc _mul_unsigned
    lda #0               ; zero out inner and outer loop counters
    sta outer
    sta inner

    ldy #0               ; clear the result
    ldx #0
@clear_ptr3:
    sta (ptr3), y
    iny
    sta (ptr3), y
    iny
    inx
    cpx size
    bne @clear_ptr3

@outer_loop:
    ldy outer
    cpy size
    beq @outer_fini

    lda (ptr2), y          ; initialize b
    sta byte_b

@inner_loop:
    ldy inner
    cpy size
    beq @inner_fini

    lda (ptr1), y          ; initialize a
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

    ; add product to ptr3
    clc
    lda inner
    adc outer
    tay
    clc                ; just to be safe
    lda (ptr3), y
    adc product_lo
    sta (ptr3), y
    iny
    lda (ptr3), y
    adc product_hi
    sta (ptr3), y
    iny
    lda (ptr3), y
    adc #0
    sta (ptr3), y

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
