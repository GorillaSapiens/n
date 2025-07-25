#ifndef _INCLUDE_INTEGER_H_
#define _INCLUDE_INTEGER_H_

// NOTE:
// 'p' may be binary (0b*), octal(0*), hex (0x*), or decimal
// returns the value of the integer
// expect silly return values when 'size' > sizeof(int)

// convert the string 'p' to a 'size' little endian int at 'target'
// see NOTE above
int make_le_int(const char *p, unsigned char *target, int size);

// take the 2s complement of the 'size' little endian int at 'target'
void negate_le_int(unsigned char *target, int size);

// convert the string 'p' to a 'size' big endian int at 'target'
// see NOTE above
int make_be_int(const char *p, unsigned char *target, int size);

// take the 2s complement of the 'size' big endian int at 'target'
void negate_be_int(unsigned char *target, int size);

#endif
