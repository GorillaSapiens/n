
; add.asm - Arbitrary-length and fixed-width addition routines (unsigned/signed)
;
; Adds two little-endian integers of length X bytes (in register X).
; Inputs:
;   ptr1 - pointer to operand A (low byte at lower address)
;   ptr2 - pointer to operand B
;   ptr3 - pointer to destination buffer
;   X    - number of bytes (in X register)
; Assumes:
;   ptr1, ptr2, ptr3 are 2-byte pointers in zero page
; Clobbers: A, Y, status flags

; Zero page locations assumed
ptr1     = $00
ptr2     = $02
ptr3     = $04

.proc add_unsigned
    ldy #0            ; Start at offset 0
    clc               ; Clear carry flag

@loop:
    lda (ptr1), y     ; Load byte from operand A
    adc (ptr2), y     ; Add byte from operand B with carry
    sta (ptr3), y     ; Store result
    iny               ; Next byte
    dex               ; Decrement byte counter
    bne @loop         ; Repeat if not zero

    rts
.endproc

; Fixed width (8/16/32-bit) versions that avoid X usage

.proc add8
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc add16
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc add32
    ldy #0
    clc
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    adc (ptr2), y
    sta (ptr3), y
    rts
.endproc
