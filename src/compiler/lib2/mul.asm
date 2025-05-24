
; mul.asm - Arbitrary-length unsigned multiplication
;
; Multiply ptr1 * ptr2 and store into ptr3.
; X = byte count of inputs (result is up to 2X bytes)
;
; Inputs:
;   ptr1 - multiplicand
;   ptr2 - multiplier
;   ptr3 - result (2X bytes, zero-cleared beforehand)
;   X    - byte count
; Clobbers: A, X, Y

.include "zp.inc"

ptr1      = $00
ptr2      = $02
ptr3      = $04
bytecount = $06
multA     = $07
multB     = $08
prod      = $09  ; 2 bytes for result
carry     = $0B

.proc mul_unsigned
    stx bytecount       ; Save byte count
    ldy #0              ; Outer index for multiplier
outer_loop:
    ldx bytecount
    cpx #0
    beq done
    lda (ptr2), y       ; Load multiplier byte
    beq skip_inner      ; If 0, skip
    sta multB

    ldx #0              ; Inner index for multiplicand
inner_loop:
    txa
    tay
    lda (ptr1), y
    sta multA

    ; Multiply A * B via repeated addition (could use table or unrolled later)
    ldy multB
    lda #0
    sta prod
    sta prod+1
    ldy #8
    lda multA
    ldx multB
    sta prod
    lda #0
    sta carry

    ; 8-bit * 8-bit multiply
    ldx #0
    ldy #0
    lda multA
    sta multA
    lda multB
    sta multB
    clc
    lda #0
    sta prod
    sta prod+1
    ldx #8
mul_loop:
    lsr multB
    bcc skip_add
    lda prod
    clc
    adc multA
    sta prod
    bcc skip_add
    inc prod+1
skip_add:
    asl multA
    rol prod+1
    dex
    bne mul_loop

    ; Add result (prod) to ptr3[offset]
    lda bytecount
    sec
    sbc #1
    sec
    sbc #0
    sta carry           ; offset = x + y
    clc
    ldy carry
    lda (ptr3), y
    adc prod
    sta (ptr3), y
    iny
    lda (ptr3), y
    adc prod+1
    sta (ptr3), y

    inx
    cpx bytecount
    bne inner_loop
skip_inner:
    iny
    cpy bytecount
    bne outer_loop
done:
    rts
.endproc