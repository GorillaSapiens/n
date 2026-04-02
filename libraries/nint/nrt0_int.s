; nrt0_int.s

.global __nmi
.global __irqbrk

.export __nmi
.export __irqbrk

.import handle_irq
.import handle_nmi

.segment "CODE"

__nmi:
   php
   pha
   txa
   pha
   tya
   pha

   jsr handle_nmi

   jmp __nmi_irqbrk_common

__irqbrk:
   php
   pha
   txa
   pha
   tya
   pha

   jsr handle_irq

__nmi_irqbrk_common:
   pla
   tay
   pla
   tax
   pla
   plp
   rti
