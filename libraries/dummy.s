.include "nrt0.s"

.segment "CODE"

.global _main
.export _main

_main:
RTS

.global _handle_nmi
.export _handle_nmi

_handle_nmi:
RTS

.global _handle_irq
.export _handle_irq

_handle_irq:
RTS

