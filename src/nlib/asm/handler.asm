; handler.s - default IRQ and NMI handlers

.include "nlib.inc"

.proc handle_nmi
    rts
.endproc

.proc handle_irq
    rts
.endproc
