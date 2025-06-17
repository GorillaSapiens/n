.import TARGET

.include "nlib.inc"

.segment "CODE"

start:
    cld ; clear decimal mode

    ; setup ptr1, ptr2, ptr3 in zero page
    lda #<input1
    sta <ptr1
    lda #>input1
    sta >ptr1

    lda #<input2
    sta <ptr2
    lda #>input2
    sta >ptr2

    lda #<input3
    sta <ptr3
    lda #>input3
    sta >ptr3

    lda #<input4
    sta <ptr4
    lda #>input4
    sta >ptr4

    ldx #2
    stx size
    lda #$0C
    sta shift

    jsr TARGET

    lda size
    sta outsize
    lda shift
    sta outshift
hang:
    jmp hang

outsize:  .byte  $00
outshift: .byte $00
input1:   .dword $0000
input2:   .dword $0000
input3:   .dword $0000
input4:   .dword $0000

