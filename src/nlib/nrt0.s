; nrt0.s

.global __reset
.global __nmi
.global __irqbrk

.export __reset
.export __nmi
.export __irqbrk

.import _main
.import _handle_irq
.import _handle_nmi

.import __data_load_start
.import __data_run_start
.import __data_size
.import __bss_start
.import __bss_size

.include "nlib.inc"

.segment "CODE"

__reset:
   sei
   cld

   ; hardware stack at $01FF downward
   ldx #$ff
   txs

   ; ptr0 = __data_load_start
   lda #<__data_load_start
   sta ptr0
   lda #>__data_load_start
   sta ptr0+1

   ; ptr1 = __data_run_start
   lda #<__data_run_start
   sta ptr1
   lda #>__data_run_start
   sta ptr1+1

   ; ptr2 = __data_size
   lda #<__data_size
   sta ptr2
   lda #>__data_size
   sta ptr2+1

_copy_data:
   lda ptr2
   ora ptr2+1
   beq _clear_bss_setup

   ldy #0
   lda (ptr0),y
   sta (ptr1),y

   inc ptr0
   bne _copy_data_dst
   inc ptr0+1

_copy_data_dst:
   inc ptr1
   bne _copy_data_count
   inc ptr1+1

_copy_data_count:
   lda ptr2
   bne _copy_data_dec_lo
   dec ptr2+1
_copy_data_dec_lo:
   dec ptr2
   jmp _copy_data

_clear_bss_setup:
   ; ptr1 = __bss_start
   lda #<__bss_start
   sta ptr1
   lda #>__bss_start
   sta ptr1+1

   ; ptr2 = __bss_size
   lda #<__bss_size
   sta ptr2
   lda #>__bss_size
   sta ptr2+1

_clear_bss:
   lda ptr2
   ora ptr2+1
   beq _start_main

   ldy #0
   lda #0
   sta (ptr1),y

   inc ptr1
   bne _clear_bss_count
   inc ptr1+1

_clear_bss_count:
   lda ptr2
   bne _clear_bss_dec_lo
   dec ptr2+1
_clear_bss_dec_lo:
   dec ptr2
   jmp _clear_bss

_start_main:
   ; argument stack pointer
   ldx #$00
   stx sp
   ldx #$10
   stx sp+1

   jsr _main

_forever:
   jmp _forever


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
