.import _startup_segment_address
.include "nlib.h"

.proc _nalloc

    lda #<_startup_segment_address
    sta ptr3
    lda #>_startup_segment_address
    sta ptr3+1

    lda #<_startup_segment_address
    sta ptr4
    lda #>_startup_segment_address
    sta ptr4+1

    rts
.endproc

.proc _nfree
    rts
.endproc

