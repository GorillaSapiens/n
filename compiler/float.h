#ifndef _INCLUDE_FLOAT_H_
#define _INCLUDE_FLOAT_H_

// convert a string to float
double parse_float(const char *p);

// default IEEE-style exponent-bit count for the supported built-in float sizes
// returns -1 if the size has no default layout
int default_float_expbits_for_size(int size);

// NOTE:
// 'p' may be hex (0x*), or decimal
// returns 0 on success

// convert the string 'p' to a 'size' little endian float at 'target'
// using an IEEE-style SEM layout with 'expbits' exponent bits.
int make_le_float_layout(const char *p, unsigned char *target, int size, int expbits);

// convert the string 'p' to a 'size' little endian float at 'target'
// using the default IEEE-style layout for that size.
int make_le_float(const char *p, unsigned char *target, int size);

// negate the 'size' little endian float at 'target'
void negate_le_float(unsigned char *target, int size);

// convert the string 'p' to a 'size' big endian float at 'target'
// using an IEEE-style SEM layout with 'expbits' exponent bits.
int make_be_float_layout(const char *p, unsigned char *target, int size, int expbits);

// convert the string 'p' to a 'size' big endian float at 'target'
// using the default IEEE-style layout for that size.
int make_be_float(const char *p, unsigned char *target, int size);

// negate the 'size' big endian float at 'target'
void negate_be_float(unsigned char *target, int size);

#endif
