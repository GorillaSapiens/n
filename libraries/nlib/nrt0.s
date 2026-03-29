; nrt0.s

.global __reset

.export __reset

.import _main

.import __data_load_start
.import __data_run_start
.import __data_size
.import __bss_start
.import __bss_end
.import __bss_size
.import __init_table

.include "../nlib.inc"

.segment "CODE"

__reset:
   sei
   cld

   ; hardware stack at $01FF downward
   ldx #$ff
   txs

   ; _nl_ptr0 = __data_load_start
   lda #<__data_load_start
   sta _nl_ptr0
   lda #>__data_load_start
   sta _nl_ptr0+1

   ; _nl_ptr1 = __data_run_start
   lda #<__data_run_start
   sta _nl_ptr1
   lda #>__data_run_start
   sta _nl_ptr1+1

   ; _nl_ptr2 = __data_size
   lda #<__data_size
   sta _nl_ptr2
   lda #>__data_size
   sta _nl_ptr2+1

_copy_data:
   lda _nl_ptr2
   ora _nl_ptr2+1
   beq _clear_bss_setup

   ldy #0
   lda (_nl_ptr0),y
   sta (_nl_ptr1),y

   inc _nl_ptr0
   bne _copy_data_dst
   inc _nl_ptr0+1

_copy_data_dst:
   inc _nl_ptr1
   bne _copy_data_count
   inc _nl_ptr1+1

_copy_data_count:
   lda _nl_ptr2
   bne _copy_data_dec_lo
   dec _nl_ptr2+1
_copy_data_dec_lo:
   dec _nl_ptr2
   jmp _copy_data

_clear_bss_setup:
   ; _nl_ptr1 = __bss_start
   lda #<__bss_start
   sta _nl_ptr1
   lda #>__bss_start
   sta _nl_ptr1+1

   ; _nl_ptr2 = __bss_size
   lda #<__bss_size
   sta _nl_ptr2
   lda #>__bss_size
   sta _nl_ptr2+1

_clear_bss:
   lda _nl_ptr2
   ora _nl_ptr2+1
   beq _start_init

   ldy #0
   lda #0
   sta (_nl_ptr1),y

   inc _nl_ptr1
   bne _clear_bss_count
   inc _nl_ptr1+1

_clear_bss_count:
   lda _nl_ptr2
   bne _clear_bss_dec_lo
   dec _nl_ptr2+1
_clear_bss_dec_lo:
   dec _nl_ptr2
   jmp _clear_bss

_start_init:
   ; argument stack grows upward, so start it immediately after BSS
   lda #<__bss_end
   sta _nl_sp
   sta _nl_fp
   lda #>__bss_end
   sta _nl_sp+1
   sta _nl_fp+1

   lda #<__init_table
   sta _nl_ptr0
   lda #>__init_table
   sta _nl_ptr0+1

_call_init_loop:
   ldy #0
   lda (_nl_ptr0),y
   sta _nl_ptr1
   iny
   lda (_nl_ptr0),y
   sta _nl_ptr1+1
   lda _nl_ptr1
   ora _nl_ptr1+1
   beq _start_main
   lda _nl_ptr0
   pha
   lda _nl_ptr0+1
   pha
   jsr _call_init_trampoline
   pla
   sta _nl_ptr0+1
   pla
   sta _nl_ptr0
   clc
   lda _nl_ptr0
   adc #2
   sta _nl_ptr0
   bcc _call_init_loop
   inc _nl_ptr0+1
   jmp _call_init_loop

_call_init_trampoline:
   jmp (_nl_ptr1)

_start_main:
   jsr _main

_forever:
   jmp _forever

