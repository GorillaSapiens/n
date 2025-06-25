#ifndef _INCLUDE_INTEGER_H_
#define _INCLUDE_INTEGER_H_

int make_le_int(const char *p, unsigned char *target, int size);
void negate_le_int(unsigned char *target, int size);

int make_be_int(const char *p, unsigned char *target, int size);
void negate_be_int(unsigned char *target, int size);

#endif
