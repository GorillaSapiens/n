
_nrt0_start:

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

