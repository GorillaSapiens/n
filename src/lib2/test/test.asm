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

    lda #<input3
    sta $04
    lda #>input3
    sta $05

    lda #<input4
    sta $06
    lda #>input4
    sta $07

    ldx #2
    jsr TARGET

hang:
    jmp hang

input1:  .word $FFFE
input2:  .word $2710
input3:  .word $eaea
input4:  .word $eaea

