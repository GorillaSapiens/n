#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "messages.h"
#include "xray.h"

// TODO FIX we'll need to revisit this later, for now just ensure
// our doubles are big enough.
static_assert(sizeof(double) == 8);
static_assert(sizeof(unsigned long long) == 8);

// TODO FIX later we can add variable length mantissa and exponent,
// variable offset, and variable ordering.  for right now, you get
// IEEE 754 based on size.

typedef struct Bytes2EBits {
   int bytes;
   int ebits;
} Bytes2EBits;

// table containing default number of exponent bits for N byte float
// TODO FIX: in the future, allow setting the ebits with a $flag
static Bytes2EBits exponent_bits[] = {
   {  1,  4 }, // proposed binary8
   {  2,  5 }, // half / binary16
   {  3,  7 }, // hypothetical binary24
   {  4,  8 }, // single / float / binary32
   {  5, 10 }, // matches some legacy x86 hardware
   {  6, 12 }, // hypothetical binary48
   {  7, 13 }, // hypothetical binary56
   {  8, 11 }, // double / binary64

   // TODO FIX more here!
   { 16, 15 }, // quad / binary128
   { 32, 19 }  // binary256
};

static int ebits(int size) {
   for (int i = 0;
        i < sizeof(exponent_bits) /
            sizeof(exponent_bits[0]);
        i++) {
      if (exponent_bits[i].bytes == size) {
         return exponent_bits[i].ebits;
      }
   }
   return -1;
}

double parse_float(const char *p) {
   double ret;

   if (strstr(p, "0x") || strstr(p, "0X")) {
      if (sscanf(p, "%la", &ret) != 1) {
         error("[%d:%s] could not sscanf '%s'", __FILE__, __LINE__, p);
      }
   }
   else {
      if (sscanf(p, "%lf", &ret) != 1) {
         error("[%d:%s] could not sscanf '%s'", __FILE__, __LINE__, p);
      }
   }

   return ret;
}

int make_le_float(const char *p, unsigned char *target, int size) {

   int expbits = ebits(size);
   if (expbits == -1) {
      error("[%s:%d] size %d floats not supported (yet)", __FILE__, __LINE__);
   }

   memset(target, 0, size);

   // TODO FIX for now, we cheat, by using sscanf and some
   // bit twiddling voodoo that only works for size <= sizeof(double)

   double value = parse_float(p);

   unsigned long long ivalue;

   // TODO FIX this may depend on host byte ordering

   ivalue = *((unsigned long long *) &value);

   if (size != 8) {
      unsigned long long mantissa = ivalue & ((1LL << 52) - 1);
      unsigned long long exponent = (ivalue >> 52) & ((1LL << 11) - 1);
      unsigned long long sign = ivalue >> 63;

      int bias  = (1 << (expbits - 1)) - 1;
      int mbits = (8 * size) - 1 - expbits;

      mantissa >>= 52 - mbits;
      exponent = exponent - 1023 + bias;

      ivalue = (sign << (8 * size - 1)) | (exponent << mbits) | mantissa;
   }

   for (int i = 0; i < size; i++) {
      target[i] = ivalue;
      ivalue >>= 8;
   }

   return 0;
}

void negate_le_float(unsigned char *target, int size) {
   target[size-1] |= 0x80;
}

int make_be_float(const char *p, unsigned char *target, int size) {
   int ret = make_le_float(p, target, size);
   int tmp;
   for (int i = 0; i < size/2; i++) {
      tmp = target[i];
      target[i] = target[size - 1 - i];
      target[size - 1 - i] = tmp;
   }
   return ret;
}

void negate_be_float(unsigned char *target, int size) {
   target[0] |= 0x80;
}

