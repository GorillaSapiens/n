; nrt0.s

.global _nrt0_reset
.export _nrt0_reset
.export _startup_segment_address
.import main
.import handle_irq
.import handle_nmi
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
_startup_segment_address: ; do not move, must come first !!!
_nrt0_reset:
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
    sta ptr1
    lda #>__DATA_LOAD__
    sta ptr1+1

    lda #<__DATA_RUN__
    sta ptr2
    lda #>__DATA_RUN__
    sta ptr2+1

    ldy #0
    ldx #>__DATA_SIZE__
    beq _copy_data_lo

_copy_data_hi_loop:
    lda (ptr1),y
    sta (ptr2),y
    iny
    bne _copy_data_hi_loop
    inc ptr1+1
    inc ptr2+1
    dex
    bne _copy_data_hi_loop

_copy_data_lo:
    ldy #0
    ldx #<__DATA_SIZE__
    beq _copy_data_fini

_copy_data_lo_loop:
    lda (ptr1),y
    sta (ptr2),y
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
    sta ptr2
    lda #>__BSS_RUN__
    sta ptr2+1

    lda #0
    ldy #0
    ldx #>__BSS_SIZE__
    beq _clear_bss_lo

_clear_bss_hi_loop:
    sta (ptr2),y
    iny
    bne _clear_bss_hi_loop
    inc ptr2+1
    dex
    bne _clear_bss_hi_loop

_clear_bss_lo:
    ldy #0
    ldx #<__BSS_SIZE__
    beq _clear_bss_fini

_clear_bss_lo_loop:
    sta (ptr2),y
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
    sta _startup_segment_address-2
    sta _startup_segment_address-1
    lda #<(_startup_segment_address-4)
    sta _startup_segment_address-4
    lda #>(_startup_segment_address-4)
    sta _startup_segment_address-3


    ; jump to main program
    jsr main

loop:
    jmp loop

_nrt0_nmi:
    ; push AXY
    pha
    txa
    pha
    tya
    pha

    ; call the handler
    jsr handle_nmi

    ; pop YXA
    pla
    tay
    pla
    tax
    pla
    rti

_nrt0_irq:
    ; push AXY
    pha
    txa
    pha
    tya
    pha

    ; call the handler
    jsr handle_irq

    ; pop YXA
    pla
    tay
    pla
    tax
    pla
    rti

.segment "VECTORS"

.word _nrt0_nmi   ; @ $fffa - $fffb
.word _nrt0_reset ; @ $fffc - $fffd
.word _nrt0_irq   ; @ $fffe - $ffff
