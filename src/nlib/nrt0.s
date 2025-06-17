.include "nlib.inc"

.segment "STARTUP"

_nrt0_reset:
    ; set interrupt disable flag (disable IRQs)
    sei

    ; clear decimal mode (ensure binary mode math)
    cld

    ; hardware stack starts at $01FF and grows down
    ldx #$FF
    txs

    ; argument stack starts at $0200 and grows up
    ldx #0
    stx sp
    ldx #$02
    stx sp+1

    ; jump to main program
    jmp main

_nrt0_nmi:
    rti

_nrt0_irq:
    rti

.segment "VECTORS"

.word _nrt0_nmi   ; @ $fffa - $fffb
.word _nrt0_reset ; @ $fffc - $fffd
.word _nrt0_irq   ; @ $fffe - $ffff
