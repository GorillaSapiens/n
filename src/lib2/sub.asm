; sub.asm - Arbitrary-length and fixed-width subtraction routines (unsigned/signed)
;
; Subtracts ptr2 from ptr1 and stores in ptr3, X bytes.
; Result = ptr1 - ptr2 (little-endian)
;
; Inputs:
;   ptr1 - minuend
;   ptr2 - subtrahend
;   ptr3 - destination
;   X    - byte count (in register X)
; Assumes:
;   ptr1, ptr2, ptr3 are 2-byte pointers in zero page
; Clobbers: A, Y, status flags

; Zero page locations assumed
ptr1     = $00
ptr2     = $02
ptr3     = $04

.proc sub_unsigned
    ldy #0            ; Start at byte 0
    sec               ; Set carry before SBC
@loop:
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    dex
    bne @loop
    rts
.endproc

; Fixed-width versions

.proc sub8
    ldy #0
    sec
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc sub16
    ldy #0
    sec
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    rts
.endproc

.proc sub32
    ldy #0
    sec
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    iny
    lda (ptr1), y
    sbc (ptr2), y
    sta (ptr3), y
    rts
.endproc
