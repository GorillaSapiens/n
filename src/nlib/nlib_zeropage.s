; nlib_zeropage.s

.include "nlib.inc"

.segment "ZEROPAGE"

; 36 bytes total

_nl_sp:    .res 2 ; the argument stack pointer
_nl_fp:    .res 2 ; the frame pointer

_nl_arg0:  .res 1 ; byte argument 0 ; size
_nl_arg1:  .res 1 ; byte argument 1 ; shift / result
_nl_arg2:  .res 1 ; byte argument 2
_nl_arg3:  .res 1 ; byte argument 3
_nl_arg4:  .res 1 ; byte argument 4
_nl_arg5:  .res 1 ; byte argument 5
_nl_arg6:  .res 1 ; byte argument 6
_nl_arg7:  .res 1 ; byte argument 7

_nl_ptr0:  .res 2 ; pointer to argument 0
_nl_ptr1:  .res 2 ; pointer to argument 1
_nl_ptr2:  .res 2 ; pointer to argument 2
_nl_ptr3:  .res 2 ; pointer to argument 3
_nl_ptr4:  .res 2 ; pointer to argument 4
_nl_ptr5:  .res 2 ; pointer to argument 5
_nl_ptr6:  .res 2 ; pointer to argument 6
_nl_ptr7:  .res 2 ; pointer to argument 7

_nl_tmp0:  .res 1 ; temporary scratch register 0
_nl_tmp1:  .res 1 ; temporary scratch register 1
_nl_tmp2:  .res 1 ; temporary scratch register 2
_nl_tmp3:  .res 1 ; temporary scratch register 3
_nl_tmp4:  .res 1 ; temporary scratch register 4
_nl_tmp5:  .res 1 ; temporary scratch register 5
_nl_tmp6:  .res 1 ; temporary scratch register 6
_nl_tmp7:  .res 1 ; temporary scratch register 7

