; floating-point add/sub core for IEEE-style binary32
;
; Inputs:
;   ptr0 - lhs
;   ptr1 - rhs
;   ptr2 - destination
;   arg0 - total size in bytes
;   arg1 - exponent bit count
;
; Notes:
;   - currently implements binary32 only (size=4, expbits=8)
;   - operands are treated as little-endian in memory
;   - tmp5 is a rhs sign xor mask on entry: 0 for add, $80 for subtract
;
.include "nlib.inc"
.export __faddsubN_core

.proc _faddN
    lda #0
    sta tmp5
    jmp __faddsubN_core
.endproc

.proc __faddsubN_core
    lda arg0
    cmp #4
    bne @copy_lhs_to_dst
    lda arg1
    cmp #8
    bne @copy_lhs_to_dst

    jsr @load_lhs
    jsr @load_rhs

    ; zero fast paths after operands have been converted to internal mantissas
    lda tmp0
    bne @lhs_nonzero
    ldy #0
    lda (ptr0), y
    iny
    ora (ptr0), y
    iny
    ora (ptr0), y
    bne @lhs_nonzero
    lda tmp1
    bne @swap_and_store_ptr0
    ldy #0
    lda (ptr1), y
    iny
    ora (ptr1), y
    iny
    ora (ptr1), y
    beq @store_zero
@swap_and_store_ptr0:
    jsr @swap_operands
    jmp @store_ptr0
@lhs_nonzero:
    lda tmp1
    bne @have_nonzero_operands
    ldy #0
    lda (ptr1), y
    iny
    ora (ptr1), y
    iny
    ora (ptr1), y
    beq @store_ptr0

@have_nonzero_operands:
    lda tmp2
    cmp tmp3
    bne @different_signs

    ; same-sign addition
    lda tmp0
    cmp tmp1
    bcs @same_sign_base_ready
    jsr @swap_operands
@same_sign_base_ready:
    lda tmp0
    sec
    sbc tmp1
    sta tmp4
    beq @same_sign_add
    cmp #24
    bcs @zero_rhs_for_add
@same_sign_shift_loop:
    ldy #2
    lda (ptr1), y
    lsr
    sta (ptr1), y
    dey
    lda (ptr1), y
    ror
    sta (ptr1), y
    dey
    lda (ptr1), y
    ror
    sta (ptr1), y
    dec tmp4
    bne @same_sign_shift_loop
    beq @same_sign_add
@zero_rhs_for_add:
    ldy #0
    lda #0
    sta (ptr1), y
    iny
    sta (ptr1), y
    iny
    sta (ptr1), y
@same_sign_add:
    clc
    ldy #0
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr0), y
    iny
    lda (ptr0), y
    adc (ptr1), y
    sta (ptr0), y
    bcc @store_ptr0

    ; shift the carry+mantissa back down to 24 bits
    ldy #2
    lda (ptr0), y
    ror
    sta (ptr0), y
    dey
    lda (ptr0), y
    ror
    sta (ptr0), y
    dey
    lda (ptr0), y
    ror
    sta (ptr0), y
    inc tmp0
    lda tmp0
    cmp #$ff
    bne @store_ptr0
    ldy #0
    lda #0
    sta (ptr0), y
    iny
    sta (ptr0), y
    iny
    sta (ptr0), y
    jmp @store_ptr0

@different_signs:
    jsr @compare_magnitude
    beq @store_zero
    bcs @lhs_mag_ge_rhs
    jsr @swap_operands
@lhs_mag_ge_rhs:
    lda tmp0
    sec
    sbc tmp1
    sta tmp4
    beq @different_sign_sub
    cmp #24
    bcs @zero_rhs_for_sub
@different_sign_shift_loop:
    ldy #2
    lda (ptr1), y
    lsr
    sta (ptr1), y
    dey
    lda (ptr1), y
    ror
    sta (ptr1), y
    dey
    lda (ptr1), y
    ror
    sta (ptr1), y
    dec tmp4
    bne @different_sign_shift_loop
    beq @different_sign_sub
@zero_rhs_for_sub:
    ldy #0
    lda #0
    sta (ptr1), y
    iny
    sta (ptr1), y
    iny
    sta (ptr1), y
@different_sign_sub:
    sec
    ldy #0
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr0), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr0), y

    ldy #0
    lda (ptr0), y
    iny
    ora (ptr0), y
    iny
    ora (ptr0), y
    beq @store_zero

@normalize_left:
    ldy #2
    lda (ptr0), y
    and #$80
    bne @store_ptr0
    lda tmp0
    cmp #1
    beq @make_subnormal
    ldy #0
    lda (ptr0), y
    asl
    sta (ptr0), y
    iny
    lda (ptr0), y
    rol
    sta (ptr0), y
    iny
    lda (ptr0), y
    rol
    sta (ptr0), y
    dec tmp0
    jmp @normalize_left

@make_subnormal:
    lda #0
    sta tmp0
    jmp @store_ptr0

@store_ptr0:
    ; tmp0 = effective exponent, tmp2 = sign, ptr0 mantissa bytes contain result
    ldy #2
    lda (ptr0), y
    sta tmp5
    and #$80
    bne @rawexp_from_tmp0
    lda tmp0
    cmp #1
    bne @rawexp_from_tmp0
    lda #0
    sta tmp4
    beq @rawexp_ready
@rawexp_from_tmp0:
    lda tmp0
    sta tmp4
@rawexp_ready:
    ldy #0
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda tmp5
    and #$7f
    sta tmp5
    lda tmp4
    beq @store_byte2
    and #1
    beq @store_byte2
    lda tmp5
    ora #$80
    sta tmp5
@store_byte2:
    lda tmp5
    sta (ptr2), y
    iny
    lda tmp4
    beq @store_sign_only
    lsr
    ora tmp2
    sta (ptr2), y
    rts
@store_sign_only:
    lda tmp2
    sta (ptr2), y
    rts

@store_zero:
    lda #0
    ldy #0
    sta (ptr2), y
    iny
    sta (ptr2), y
    iny
    sta (ptr2), y
    iny
    sta (ptr2), y
    rts

@copy_lhs_to_dst:
    ldy #0
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    rts

@compare_magnitude:
    lda tmp0
    cmp tmp1
    bcc @rhs_greater
    bne @lhs_greater
    ldy #2
    lda (ptr0), y
    cmp (ptr1), y
    bcc @rhs_greater
    bne @lhs_greater
    dey
    lda (ptr0), y
    cmp (ptr1), y
    bcc @rhs_greater
    bne @lhs_greater
    dey
    lda (ptr0), y
    cmp (ptr1), y
    rts
@lhs_greater:
    sec
    lda #1
    rts
@rhs_greater:
    clc
    lda #1
    rts

@swap_operands:
    lda ptr0
    sta tmp4
    lda ptr1
    sta ptr0
    lda tmp4
    sta ptr1

    lda ptr0+1
    sta tmp4
    lda ptr1+1
    sta ptr0+1
    lda tmp4
    sta ptr1+1

    lda tmp0
    sta tmp4
    lda tmp1
    sta tmp0
    lda tmp4
    sta tmp1

    lda tmp2
    sta tmp4
    lda tmp3
    sta tmp2
    lda tmp4
    sta tmp3
    rts

@load_lhs:
    ldy #2
    lda (ptr0), y
    sta tmp4
    and #$7f
    sta (ptr0), y
    iny
    lda (ptr0), y
    and #$80
    sta tmp2

    lda (ptr0), y
    and #$7f
    asl
    sta tmp0
    lda tmp4
    and #$80
    beq @lhs_exp_ready
    inc tmp0
@lhs_exp_ready:
    lda tmp0
    beq @lhs_zero_or_subnormal
    ldy #2
    lda (ptr0), y
    ora #$80
    sta (ptr0), y
    rts
@lhs_zero_or_subnormal:
    ldy #0
    lda (ptr0), y
    iny
    ora (ptr0), y
    iny
    ora (ptr0), y
    beq @load_lhs_done
    lda #1
    sta tmp0
@load_lhs_done:
    rts

@load_rhs:
    ldy #2
    lda (ptr1), y
    sta tmp4
    and #$7f
    sta (ptr1), y
    iny
    lda (ptr1), y
    and #$80
    eor tmp5
    sta tmp3

    lda (ptr1), y
    and #$7f
    asl
    sta tmp1
    lda tmp4
    and #$80
    beq @rhs_exp_ready
    inc tmp1
@rhs_exp_ready:
    lda tmp1
    beq @rhs_zero_or_subnormal
    ldy #2
    lda (ptr1), y
    ora #$80
    sta (ptr1), y
    lda #0
    sta tmp5
    rts
@rhs_zero_or_subnormal:
    ldy #0
    lda (ptr1), y
    iny
    ora (ptr1), y
    iny
    ora (ptr1), y
    beq @rhs_loaded_zero
    lda #1
    sta tmp1
@rhs_loaded_zero:
    lda #0
    sta tmp5
@load_rhs_done:
    rts
.endproc
