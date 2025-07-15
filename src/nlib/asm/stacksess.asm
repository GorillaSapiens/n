; stacksess.asm - Stack access functions
;
; these all transfer fp to ptrN, and add arg0

.include "nlib.inc"

.proc _stacksess0
    clc
    lda fp
    adc arg0
    sta ptr0
    lda fp+1
    adc #0
    sta ptr0+1
    rts
.endproc

.proc _stacksess1
    clc
    lda fp
    adc arg0
    sta ptr1
    lda fp+1
    adc #0
    sta ptr1+1
    rts
.endproc

.proc _stacksess2
    clc
    lda fp
    adc arg0
    sta ptr2
    lda fp+1
    adc #0
    sta ptr2+1
    rts
.endproc

.proc _stacksess3
    clc
    lda fp
    adc arg0
    sta ptr3
    lda fp+1
    adc #0
    sta ptr3+1
    rts
.endproc
