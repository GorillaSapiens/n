; stack.asm - Arbitrary-length stack operations

.include "nlib.inc"

.proc _pushN
    ; increment sp by arg0
    ; stack grows up!
    clc
    lda sp
    adc arg0
    sta sp
    lda sp+1
    adc #0
    sta sp+1
    rts
.endproc

.proc _popN
    ; decrement sp by arg0
    sec
    lda sp
    sbc arg0
    sta sp
    lda sp+1
    sbc #0
    sta sp+1
    rts
.endproc

.proc _cpyN
    ; copy arg0 bytes from ptr0 to ptr1
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _comp2N
    ; arg0 bytes ptr1 = 2's complement of ptr0
    ldx arg0
    ldy #0
    sec
@loop:
    lda (ptr0), y
    eor #$FF
    adc #$0
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _swapN
    ; swap arg0 bytes between ptr0 and ptr1
    ldx arg0
    ldy #0
@loop:
    lda (ptr0), y
    sta tmp0
    lda (ptr1), y
    sta (ptr0), y
    lda tmp0
    sta (ptr1), y
    iny
    dex
    bne @loop
    rts
.endproc

.proc _callptr0
    ; indirect call through ptr0
    ; pushes (target - 1) so RTS transfers control to ptr0
    ;
    ; Indirect call trampoline for the 6502. The CPU has no JSR (addr) instruction,
    ; so this routine fakes one by taking the target address in ptr0, subtracting 1,
    ; pushing that adjusted address onto the hardware stack in the order RTS expects,
    ; and then executing RTS. Since RTS pulls the stacked address, adds 1, and jumps
    ; to it, control transfers to ptr0. This preserves normal call/return behavior
    ; only because _callptr0 itself was entered by JSR, so the caller's real return
    ; address is already sitting underneath the fake one on the stack; when the
    ; target later executes RTS, it returns to the original caller as usual.
    ;
    sec
    lda ptr0
    sbc #1
    tax
    lda ptr0+1
    sbc #0
    pha
    txa
    pha
    rts
.endproc
