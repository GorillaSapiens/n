.global main
.export main

.segment "BSS"
.res 5

.segment "DATA"
.word $1234

.segment "RODATA"
.asciiz "fnord"

.segment "CODE"
main:
    rts
