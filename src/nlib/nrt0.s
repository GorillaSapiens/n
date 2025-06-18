; nrt0.s

.global _nrt0_reset
.export _nrt0_reset
.export _startup_segment_address
.import main
.import handle_irq
.import handle_nmi

.include "nlib.inc"

; sneakly trick; we need an argstack, but it can be empty!
.segment "ARGSTACK"
_argstack_segment_address:
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

    ; argument stack starts
    ldx #<_argstack_segment_address
    stx sp
    ldx #>_argstack_segment_address
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
