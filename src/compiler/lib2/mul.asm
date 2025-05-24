; mul.asm - Arbitrary-length unsigned multiplication
;
; Multiply ptr1 * ptr2 and store into ptr3.
; X = byte count of inputs (result is up to 2X bytes)
;
; Inputs:
;   ptr1 - multiplicand
;   ptr2 - multiplier
;   ptr3 - result (2X bytes, must be zero-initialized beforehand)
;   X    - byte count
; Clobbers: A, X, Y, temp zero page

.include "zp.inc"

ptr1      = $00
ptr2      = $02
ptr3      = $04
byte_a    = $06  ; current multiplicand byte
byte_b    = $07  ; current multiplier byte
inner_idx = $08
outer_idx = $09
prod_lo   = $0A  ; product LSB
prod_hi   = $0B  ; product MSB

.proc mul_unsigned
    stx outer_idx       ; store byte count
    ldy #0              ; outer loop: index into ptr2 (multiplier)

outer_loop:
    cpy outer_idx
    beq done

    lda (ptr2), y
    beq skip_outer
    sta byte_b          ; multiplier byte

    ldx #0              ; inner loop: index into ptr1 (multiplicand)
inner_loop:
    cpx outer_idx
    beq end_inner

    txa
    tay
    lda (ptr1), y
    sta byte_a

    ; 8-bit multiply: byte_a * byte_b → prod_hi:prod_lo
    lda #0
    sta prod_lo
    sta prod_hi
    ldy #8
    lda byte_a

multiply_loop:
    lsr byte_b
    bcc skip_add
    clc
    lda prod_lo
    adc byte_a
    sta prod_lo
    bcc skip_add
    inc prod_hi
skip_add:
    asl byte_a
    rol prod_hi
    dey
    bne multiply_loop

    ; Add product to result buffer at ptr3[x+y]
    txa
    clc
    adc outer_idx
    tay

    lda prod_lo
    clc
    adc (ptr3), y
    sta (ptr3), y

    iny
    lda prod_hi
    adc (ptr3), y
    sta (ptr3), y

    inx
    jmp inner_loop

end_inner:
    iny
skip_outer:
    jmp outer_loop

done:
    rts
.endproc
