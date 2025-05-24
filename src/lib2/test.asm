.segment "CODE"

start:
    cld ; clear decimal mode

    ; setup ptr1, ptr2, ptr3 in zero page
    lda #<input1
    sta $00
    lda #>input1
    sta $01

    lda #<input2
    sta $02
    lda #>input2
    sta $03

    lda #<result_q
    sta $04
    lda #>result_q
    sta $05

    lda #<result_r
    sta $06
    lda #>result_r
    sta $07

    ldx #2
    jsr div_unsigned

hang:
    jmp hang

input1:  .byte $FE, $FF     ; 0x1234
input2:  .byte $10, $27     ; 0x0007
result_q:  .byte $ea,$ea
result_r:  .byte $ea,$ea

