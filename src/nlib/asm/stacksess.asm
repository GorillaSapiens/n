; stacksess.asm - Stack access functions
;
; these all transfer fp to ptrN, and subtract size

.include "nlib.inc"

.proc _stacksess1
    sec
    lda fp
    sbc size
    sta ptr1
    lda fp+1
    sbc #0
    sta ptr1+1
    rts
.endproc

.proc _stacksess2
    sec
    lda fp
    sbc size
    sta ptr2
    lda fp+1
    sbc #0
    sta ptr2+1
    rts
.endproc

.proc _stacksess3
    sec
    lda fp
    sbc size
    sta ptr3
    lda fp+1
    sbc #0
    sta ptr3+1
    rts
.endproc

.proc _stacksess4
    sec
    lda fp
    sbc size
    sta ptr4
    lda fp+1
    sbc #0
    sta ptr4+1
    rts
.endproc
