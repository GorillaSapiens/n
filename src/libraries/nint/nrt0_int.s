; nrt0.s

.global __nmi
.global __irqbrk

.export __nmi
.export __irqbrk

.import _handle_irq
.import _handle_nmi

.segment "CODE"

__nmi:
   php
   pha
   txa
   pha
   tya
   pha

   jsr _handle_nmi

   jmp __nmi_irqbrk_common

__irqbrk:
   php
   pha
   txa
   pha
   tya
   pha

   jsr _handle_irq

__nmi_irqbrk_common:
   pla
   tay
   pla
   tax
   pla
   plp
   rti
