// this C header is used to compile the tests.c file

extern void* nl_sp;
#pragma zpsym ("nl_sp");
extern void* nl_fp;
#pragma zpsym ("nl_fp");
extern unsigned char nl_arg0;
#pragma zpsym ("nl_arg0");
extern unsigned char nl_arg1;
#pragma zpsym ("nl_arg1");
extern void* nl_ptr0;
#pragma zpsym ("nl_ptr0");
extern void* nl_ptr1;
#pragma zpsym ("nl_ptr1");
extern void* nl_ptr2;
#pragma zpsym ("nl_ptr2");
extern void* nl_ptr3;
#pragma zpsym ("nl_ptr3");

extern void* nl_sbrk;
#pragma zpsym ("nl_sbrk");

extern unsigned char nl_tmp0;
#pragma zpsym ("nl_tmp0");
extern unsigned char nl_tmp1;
#pragma zpsym ("nl_tmp1");
extern unsigned char nl_tmp2;
#pragma zpsym ("nl_tmp2");
extern unsigned char nl_tmp3;
#pragma zpsym ("nl_tmp3");

void add16(void);
void add24(void);
void add32(void);
void add8(void);
void addN(void);
void sub8(void);
void sub16(void);
void sub24(void);
void sub32(void);
void subN(void);
void mulN(void);
void divN(void);
void remN(void);
void inc8(void);
void inc16(void);
void inc24(void);
void inc32(void);
void incN(void);
void dec8(void);
void dec16(void);
void dec24(void);
void dec32(void);
void decN(void);
void bit_andN(void);
void bit_notN(void);
void bit_orN(void);
void bit_xorN(void);
void eqN(void);
void leNs(void);
void leNu(void);
void ltNs(void);
void ltNu(void);

void asrN(void);
void lslN(void);
void lsrN(void);

void asr1(void);
void asr8(void);
void lsl1(void);
void lsl8(void);
void lsr1(void);
void lsr8(void);
void shiftN(void);

void pushN(void);
void popN(void);
void cpyN(void);
void comp2N(void);
void swapN(void);

void fp2ptr0p(void);
void fp2ptr1p(void);
void fp2ptr2p(void);
void fp2ptr3p(void);

void fp2ptr0m(void);
void fp2ptr1m(void);
void fp2ptr2m(void);
void fp2ptr3m(void);
