.segmentdef "ZEROPAGE", $0000, $0100
.segmentdef "CODE",     $8000, $2000
.segmentdef "RODATA",   $A000, $1000

.segment "CODE"
start:
   LDA #1
   RTS

.segment "RODATA"
msg:
   .asciiz "hello"
