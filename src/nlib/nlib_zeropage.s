; nlib_zeropage.s

.include "nlib.inc"

.segment "ZEROPAGE"

; 18 bytes total

_nl_sp:    .res 2 ; the argument stack pointer
_nl_size:  .res 1 ; size for the current operation
_nl_shift: .res 1 ; 1 byte arg AND/OR result
_nl_ptr1:  .res 2 ; pointer to argument 1
_nl_ptr2:  .res 2 ; pointer to argument 2
_nl_ptr3:  .res 2 ; pointer to argument 3
_nl_ptr4:  .res 2 ; pointer to argument 4
_nl_tmp1:  .res 1 ; temporary scratch register 1
_nl_tmp2:  .res 1 ; temporary scratch register 2
_nl_tmp3:  .res 1 ; temporary scratch register 3
_nl_tmp4:  .res 1 ; temporary scratch register 4
_nl_tmp5:  .res 1 ; temporary scratch register 5
_nl_tmp6:  .res 1 ; temporary scratch register 6

