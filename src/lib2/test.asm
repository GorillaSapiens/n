.import mul_unsigned
.segment "CODE"
    .org $8000

start:
    ; setup ptr1, ptr2, ptr3 in zero page
    lda #<input1
    sta $00
    lda #>input1
    sta $01

    lda #<input2
    sta $02
    lda #>input2
    sta $03

    lda #<result
    sta $04
    lda #>result
    sta $05

    ldx #2
    jsr mul_unsigned

hang:
    jmp hang

.segment "RODATA"
input1:  .byte $34, $12     ; 0x1234
input2:  .byte $78, $56     ; 0x5678

.segment "BSS"
result:  .res 4
