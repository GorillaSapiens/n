; sub.asm - Arbitrary-length and fixed-width subtraction routines (unsigned/signed)
;
; Subtracts ptr1 from ptr0 and stores in ptr2, X bytes.
; Result = ptr0 - ptr1 (little-endian)
;
; Inputs:
;   ptr0 - minuend
;   ptr1 - subtrahend
;   ptr2 - destination
;   arg0 - byte count (in register X)
; Assumes:
;   ptr0, ptr1, ptr2 are 2-byte pointers in zero page
; Clobbers: A, X, Y, status flags

.include "nlib.inc"

.proc _subN
    ldx arg0
    ldy #0            ; Start at byte 0
    sec               ; Set carry before SBC
@loop:
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    dex
    bne @loop
    rts
.endproc

; Fixed-width versions

.proc _sub8
    ldy #0
    sec
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _sub16
    ldy #0
    sec
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _sub24
    ldy #0
    sec
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    rts
.endproc

.proc _sub32
    ldy #0
    sec
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    iny
    lda (ptr0), y
    sbc (ptr1), y
    sta (ptr2), y
    rts
.endproc
