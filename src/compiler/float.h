#ifndef _INCLUDE_FLOAT_H_
#define _INCLUDE_FLOAT_H_

int make_le_float(const char *p, unsigned char *target, int size);
void negate_le_float(unsigned char *target, int size);

int make_be_float(const char *p, unsigned char *target, int size);
void negate_be_float(unsigned char *target, int size);

#endif
