
; shift.asm - Bit shifting routines
;
; Implements:
; - lsl1: logical shift left by 1
; - lsr1: logical shift right by 1
; - asr1: arithmetic shift right by 1 (signed)
;
; Inputs:
;   ptr1 - source
;   ptr2 - dest
;   X    - byte count
; Clobbers: A, Y

.include "zp.inc"

.proc lsl1
    ; Shift left 1 bit
    rts
.endproc

.proc lsr1
    ; Logical shift right
    rts
.endproc

.proc asr1
    ; Arithmetic shift right
    rts
.endproc
