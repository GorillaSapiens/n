extern unsigned char nl_size;
#pragma zpsym ("nl_size");
extern unsigned char nl_shift;
#pragma zpsym ("nl_shift");
extern void* nl_ptr1;
#pragma zpsym ("nl_ptr1");
extern void* nl_ptr2;
#pragma zpsym ("nl_ptr2");
extern void* nl_ptr3;
#pragma zpsym ("nl_ptr3");
extern void* nl_ptr4;
#pragma zpsym ("nl_ptr4");

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

void asr1(void);
void asr8(void);
void asrN(void);
void bit_andN(void);
void bit_notN(void);
void bit_orN(void);
void bit_xorN(void);
void eq(void);
void le_signed(void);
void le_unsigned(void);
void lsl1(void);
void lsl8(void);
void lslN(void);
void lsr1(void);
void lsr8(void);
void lsrN(void);
void lt_signed(void);
void lt_unsigned(void);
void shiftN(void);

