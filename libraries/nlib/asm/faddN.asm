; floating-point add/sub core for IEEE-style binary32
;
; Inputs:
;   ptr0 - lhs (read-only)
;   ptr1 - rhs (read-only)
;   ptr2 - destination / lhs workspace
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
    bne @swap_and_store_result
    ldy #0
    lda (ptr1), y
    iny
    ora (ptr1), y
    iny
    ora (ptr1), y
    beq @store_zero
@swap_and_store_result:
    jsr @swap_work_operands
    jmp @store_result
@lhs_nonzero:
    lda tmp1
    bne @have_nonzero_operands
    ldy #0
    lda (ptr1), y
    iny
    ora (ptr1), y
    iny
    ora (ptr1), y
    beq @store_result

@have_nonzero_operands:
    lda tmp2
    cmp tmp3
    bne @different_signs

    ; same-sign addition
    lda tmp0
    cmp tmp1
    bcs @same_sign_base_ready
    jsr @swap_work_operands
@same_sign_base_ready:
    lda tmp0
    sec
    sbc tmp1
    sta tmp5
    beq @same_sign_add
    cmp #24
    bcs @zero_rhs_for_add
@same_sign_shift_loop:
    jsr @rhs_shift_right_one
    dec tmp5
    bne @same_sign_shift_loop
    beq @same_sign_add
@zero_rhs_for_add:
    jsr @rhs_zero
@same_sign_add:
    clc
    ldy #0
    lda (ptr2), y
    adc arg0
    sta (ptr2), y
    iny
    lda (ptr2), y
    adc arg1
    sta (ptr2), y
    iny
    lda (ptr2), y
    adc tmp4
    sta (ptr2), y
    bcc @store_result

    ; shift the carry+mantissa back down to 24 bits
    ldy #2
    lda (ptr2), y
    ror
    sta (ptr2), y
    dey
    lda (ptr2), y
    ror
    sta (ptr2), y
    dey
    lda (ptr2), y
    ror
    sta (ptr2), y
    inc tmp0
    lda tmp0
    cmp #$ff
    bne @store_result
    ldy #0
    lda #0
    sta (ptr2), y
    iny
    sta (ptr2), y
    iny
    sta (ptr2), y
    jmp @store_result

@different_signs:
    jsr @compare_magnitude
    beq @store_zero
    bcs @lhs_mag_ge_rhs
    jsr @swap_work_operands
@lhs_mag_ge_rhs:
    lda tmp0
    sec
    sbc tmp1
    sta tmp5
    beq @different_sign_sub
    cmp #24
    bcs @zero_rhs_for_sub
@different_sign_shift_loop:
    jsr @rhs_shift_right_one
    dec tmp5
    bne @different_sign_shift_loop
    beq @different_sign_sub
@zero_rhs_for_sub:
    jsr @rhs_zero
@different_sign_sub:
    sec
    ldy #0
    lda (ptr2), y
    sbc arg0
    sta (ptr2), y
    iny
    lda (ptr2), y
    sbc arg1
    sta (ptr2), y
    iny
    lda (ptr2), y
    sbc tmp4
    sta (ptr2), y

    ldy #0
    lda (ptr2), y
    iny
    ora (ptr2), y
    iny
    ora (ptr2), y
    beq @store_zero

@normalize_left:
    ldy #2
    lda (ptr2), y
    and #$80
    bne @store_result
    lda tmp0
    cmp #1
    beq @make_subnormal
    ldy #0
    lda (ptr2), y
    asl
    sta (ptr2), y
    iny
    lda (ptr2), y
    rol
    sta (ptr2), y
    iny
    lda (ptr2), y
    rol
    sta (ptr2), y
    dec tmp0
    jmp @normalize_left

@make_subnormal:
    lda #0
    sta tmp0
    jmp @store_result

@store_result:
    ; tmp0 = effective exponent, tmp2 = sign, ptr2 mantissa bytes contain result
    ldy #2
    lda (ptr2), y
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
    ldy #2
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
    lda (ptr2), y
    cmp tmp4
    bcc @rhs_greater
    bne @lhs_greater
    dey
    lda (ptr2), y
    cmp arg1
    bcc @rhs_greater
    bne @lhs_greater
    dey
    lda (ptr2), y
    cmp arg0
    rts
@lhs_greater:
    sec
    lda #1
    rts
@rhs_greater:
    clc
    lda #1
    rts

@swap_work_operands:
    ldy #0
    lda (ptr2), y
    sta tmp5
    lda arg0
    sta (ptr2), y
    lda tmp5
    sta arg0

    iny
    lda (ptr2), y
    sta tmp5
    lda arg1
    sta (ptr2), y
    lda tmp5
    sta arg1

    iny
    lda (ptr2), y
    sta tmp5
    lda tmp4
    sta (ptr2), y
    lda tmp5
    sta tmp4

    lda tmp0
    sta tmp5
    lda tmp1
    sta tmp0
    lda tmp5
    sta tmp1

    lda tmp2
    sta tmp5
    lda tmp3
    sta tmp2
    lda tmp5
    sta tmp3
    rts

@rhs_shift_right_one:
    lda tmp4
    lsr
    sta tmp4
    lda arg1
    ror
    sta arg1
    lda arg0
    ror
    sta arg0
    rts

@rhs_zero:
    lda #0
    sta arg0
    sta arg1
    sta tmp4
    rts

@load_lhs:
    ldy #0
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sta (ptr2), y
    sta tmp4
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
    lda (ptr2), y
    ora #$80
    sta (ptr2), y
    rts
@lhs_zero_or_subnormal:
    ldy #0
    lda (ptr2), y
    iny
    ora (ptr2), y
    iny
    ora (ptr2), y
    beq @load_lhs_done
    lda #1
    sta tmp0
@load_lhs_done:
    rts

@load_rhs:
    ldy #0
    lda (ptr1), y
    sta arg0
    iny
    lda (ptr1), y
    sta arg1
    iny
    lda (ptr1), y
    sta tmp4
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
    lda tmp4
    ora #$80
    sta tmp4
    lda #0
    sta tmp5
    rts
@rhs_zero_or_subnormal:
    lda arg0
    ora arg1
    ora tmp4
    beq @rhs_loaded_zero
    lda #1
    sta tmp1
@rhs_loaded_zero:
    lda #0
    sta tmp5
    rts
.endproc
