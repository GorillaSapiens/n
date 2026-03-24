; nlib_zp.s

.globalzp _nl_sp
.globalzp _nl_fp
.globalzp _nl_arg0
.globalzp _nl_arg1
.globalzp _nl_ptr0
.globalzp _nl_ptr1
.globalzp _nl_ptr2
.globalzp _nl_ptr3
.globalzp _nl_tmp0
.globalzp _nl_tmp1
.globalzp _nl_tmp2
.globalzp _nl_tmp3
.globalzp _nl_tmp4
.globalzp _nl_tmp5

.segment "ZEROPAGE"
_nl_sp:   .res 2
_nl_fp:   .res 2
_nl_arg0: .res 1
_nl_arg1: .res 1
_nl_ptr0: .res 2
_nl_ptr1: .res 2
_nl_ptr2: .res 2
_nl_ptr3: .res 2
_nl_tmp0: .res 1
_nl_tmp1: .res 1
_nl_tmp2: .res 1
_nl_tmp3: .res 1
_nl_tmp4: .res 1
_nl_tmp5: .res 1
