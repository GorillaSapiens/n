; nrt0.s

.global nrt0_reset
.export nrt0_reset
.export startup_segment_address
.import _main
.import _handle_irq
.import _handle_nmi

.import __DATA_LOAD__, __DATA_RUN__, __DATA_SIZE__
.import __BSS_RUN__, __BSS_SIZE__
.import __ARGSTACK_RUN__

.include "nlib.inc"

; sneaky trick; we need an argstack, but it can be empty!
.segment "ARGSTACK"
.res 0

.segment "DATA"
.res 0

.segment "BSS"
.res 0

.segment "STARTUP"
startup_segment_address: ; do not move, must come first !!!
nrt0_reset:
    ; set interrupt disable flag (disable IRQs)
    sei


    ; clear decimal mode (ensure binary mode math)
    cld


    ; hardware stack starts at $01FF and grows down
    ldx #$FF
    txs


    ; copy the DATA segment to RAM
    lda #<__DATA_SIZE__
    bne _copy_data_hi
    lda #>__DATA_SIZE__
    beq _copy_data_fini

_copy_data_hi:
    lda #<__DATA_LOAD__
    sta ptr0
    lda #>__DATA_LOAD__
    sta ptr0+1

    lda #<__DATA_RUN__
    sta ptr1
    lda #>__DATA_RUN__
    sta ptr1+1

    ldy #0
    ldx #>__DATA_SIZE__
    beq _copy_data_lo

_copy_data_hi_loop:
    lda (ptr0),y
    sta (ptr1),y
    iny
    bne _copy_data_hi_loop
    inc ptr0+1
    inc ptr1+1
    dex
    bne _copy_data_hi_loop

_copy_data_lo:
    ldy #0
    ldx #<__DATA_SIZE__
    beq _copy_data_fini

_copy_data_lo_loop:
    lda (ptr0),y
    sta (ptr1),y
    iny
    dex
    bne _copy_data_lo_loop

_copy_data_fini:


    ; clear the BSS segment
    lda #<__BSS_SIZE__
    bne _clear_bss_hi
    lda #>__BSS_SIZE__
    beq _clear_bss_fini

_clear_bss_hi:
    lda #<__BSS_RUN__
    sta ptr1
    lda #>__BSS_RUN__
    sta ptr1+1

    lda #0
    ldy #0
    ldx #>__BSS_SIZE__
    beq _clear_bss_lo

_clear_bss_hi_loop:
    sta (ptr1),y
    iny
    bne _clear_bss_hi_loop
    inc ptr1+1
    dex
    bne _clear_bss_hi_loop

_clear_bss_lo:
    ldy #0
    ldx #<__BSS_SIZE__
    beq _clear_bss_fini

_clear_bss_lo_loop:
    sta (ptr1),y
    iny
    dex
    bne _clear_bss_lo_loop

_clear_bss_fini:


    ; set up argument stack pointer
    ldx #<__ARGSTACK_RUN__
    stx sp
    ldx #>__ARGSTACK_RUN__
    stx sp+1


    ; init nlib dynamic memory
    lda #0
    sta startup_segment_address-2
    sta startup_segment_address-1
    lda #<(startup_segment_address-4)
    sta startup_segment_address-4
    lda #>(startup_segment_address-4)
    sta startup_segment_address-3


    ; jump to main program
    jsr _main

loop:
    jmp loop

nrt0_nmi:
    ; push PAXY
    php
    pha
    txa
    pha
    tya
    pha

    ; call the handler
    jsr _handle_nmi

    ; pop YXAP
    pla
    tay
    pla
    tax
    pla
    plp
    rti

nrt0_irq:
    ; push PAXY
    php
    pha
    txa
    pha
    tya
    pha

    ; call the handler
    jsr _handle_irq

    ; pop YXAP
    pla
    tay
    pla
    tax
    pla
    plp
    rti

.segment "VECTORS"

.word nrt0_nmi   ; @ $fffa - $fffb
.word nrt0_reset ; @ $fffc - $fffd
.word nrt0_irq   ; @ $fffe - $ffff
