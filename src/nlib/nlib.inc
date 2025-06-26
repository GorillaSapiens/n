; nlib.inc

.globalzp _nl_sp, _nl_fp
.globalzp _nl_arg0, _nl_arg1
.globalzp _nl_ptr0, _nl_ptr1, _nl_ptr2, _nl_ptr3
.globalzp _nl_tmp0, _nl_tmp1, _nl_tmp2, _nl_tmp3, _nl_tmp4, _nl_tmp5

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

