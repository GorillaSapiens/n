; nrt0.s

.segmentdef "VECTORS", $FFFA, 6
.segmentdef "CODE", $8000, $8000
.segmentdef "DATA_LOAD", $8000 + CODE_SIZE, CODE_CAPACITY - CODE_SIZE
.segmentdef "ZP", $0000, $0100
.segmentdef "STACK", $0100, $0100
.segmentdef "DATA_RUN", $0200, $0D00
.segmentdef "ARGSTACK", $1000, $1000
.segmentdef "BSS", $2000, $1000

.global nrt0_reset
.export nrt0_reset
.export startup_segment_address
.import _main
.import _handle_irq
.import _handle_nmi

;.import DATA_LOAD_BASE, DATA_RUN_BASE, DATA_LOAD_SIZE
;.import BSS_BASE, BSS_SIZE
;.import ARGSTACK_BASE

.include "nlib.inc"

.segment "CODE"
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
    lda #<DATA_LOAD_SIZE
    bne _copy_data_hi
    lda #>DATA_LOAD_SIZE
    beq _copy_data_fini

_copy_data_hi:
    lda #<DATA_LOAD_BASE
    sta ptr0
    lda #>DATA_LOAD_BASE
    sta ptr0+1

    lda #<DATA_RUN_BASE
    sta ptr1
    lda #>DATA_RUN_BASE
    sta ptr1+1

    ldy #0
    ldx #>DATA_LOAD_SIZE
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
    ldx #<DATA_LOAD_SIZE
    beq _copy_data_fini

_copy_data_lo_loop:
    lda (ptr0),y
    sta (ptr1),y
    iny
    dex
    bne _copy_data_lo_loop

_copy_data_fini:


    ; clear the BSS segment
    lda #<BSS_SIZE
    bne _clear_bss_hi
    lda #>BSS_SIZE
    beq _clear_bss_fini

_clear_bss_hi:
    lda #<BSS_BASE
    sta ptr1
    lda #>BSS_BASE
    sta ptr1+1

    lda #0
    ldy #0
    ldx #>BSS_SIZE
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
    ldx #<BSS_SIZE
    beq _clear_bss_fini

_clear_bss_lo_loop:
    sta (ptr1),y
    iny
    dex
    bne _clear_bss_lo_loop

_clear_bss_fini:


    ; set up argument stack pointer
    ldx #<ARGSTACK_BASE
    stx sp
    ldx #>ARGSTACK_BASE
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
