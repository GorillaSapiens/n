; nlib.inc

.segment "ZP"
_nl_sp:
.res 2
_nl_fp:
.res 2
_nl_arg0:
.res 1
_nl_arg1:
.res 1
_nl_ptr0:
.res 2
_nl_ptr1:
.res 2
_nl_ptr2:
.res 2
_nl_ptr3:
.res 2
_nl_tmp0:
.res 1
_nl_tmp1:
.res 1
_nl_tmp2:
.res 1
_nl_tmp3:
.res 1
_nl_tmp4:
.res 1
_nl_tmp5:
.res 1

.global sp
.global fp
.global arg0
.global arg1
.global ptr0
.global ptr1
.global ptr2
.global ptr3
.global tmp0
.global tmp1
.global tmp2
.global tmp3
.global tmp4
.global tmp5

; aliases because we hate typing
sp    = _nl_sp    ; 2 byte
fp    = _nl_fp    ; 2 byte

arg0  = _nl_arg0  ; 1 byte
arg1  = _nl_arg1  ; 1 byte

ptr0  = _nl_ptr0  ; 2 byte
ptr1  = _nl_ptr1  ; 2 byte
ptr2  = _nl_ptr2  ; 2 byte
ptr3  = _nl_ptr3  ; 2 byte

tmp0  = _nl_tmp0  ; 1 byte
tmp1  = _nl_tmp1  ; 1 byte
tmp2  = _nl_tmp2  ; 1 byte
tmp3  = _nl_tmp3  ; 1 byte
tmp4  = _nl_tmp4  ; 1 byte
tmp5  = _nl_tmp5  ; 1 byte

