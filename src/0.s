.global main
.export main

.segment "BSS"
.res 5

.segment "DATA"
.word $1234

.segment "CODE"
main:
    rts
