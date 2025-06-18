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
nrt0_copy_data:
    lda #<__DATA_LOAD__
    sta ptr1
    lda #>__DATA_LOAD__
    sta ptr1+1

    lda #<__DATA_RUN__
    sta ptr2
    lda #>__DATA_RUN__
    sta ptr2+1

    ldy #0
    ldx #<__DATA_SIZE__
    cpx #0
    bne nrt0_copy_data_loop
    ldx #1       ; fallback to enter loop if low byte is 0
nrt0_copy_data_loop:
    lda (ptr1), y
    sta (ptr2), y
    iny
    bne nrt0_copy_data_loop_continue

    inc ptr1+1
    inc ptr2+1

nrt0_copy_data_loop_continue:
    dex
    bne nrt0_copy_data_loop

    ; clear the BSS segment
nrt0_clear_bss:
    lda #<__BSS_RUN__
    sta ptr1
    lda #>__BSS_RUN__
    sta ptr1+1

    ldy #0
    ldx #<__BSS_SIZE__
    cpx #0
    bne nrt0_clear_bss_loop
    ldx #1
nrt0_clear_bss_loop:
    lda #0
    sta (ptr1), y
    iny
    bne nrt0_clear_bss_loop_continue

    inc ptr1+1

nrt0_clear_bss_loop_continue:
    dex
    bne nrt0_clear_bss_loop

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
    jmp main

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
