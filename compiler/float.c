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

int default_float_expbits_for_size(int size) {
   switch (size) {
      case 1: return 4;  // FP8-style default (custom, not IEEE 754 interchange)
      case 2: return 5;  // IEEE 754 binary16
      case 4: return 8;  // IEEE 754 binary32
      case 8: return 11; // IEEE 754 binary64
      default:
         return -1;
   }
}

double parse_float(const char *p) {
   double ret;

   if (strstr(p, "0x") || strstr(p, "0X")) {
      if (sscanf(p, "%la", &ret) != 1) {
         error_unreachable("[%s:%d] could not sscanf '%s'", __FILE__, __LINE__, p);
      }
   }
   else {
      if (sscanf(p, "%lf", &ret) != 1) {
         error_unreachable("[%s:%d] could not sscanf '%s'", __FILE__, __LINE__, p);
      }
   }

   return ret;
}

int make_le_float_layout(const char *p, unsigned char *target, int size, int expbits) {
   int total_bits;
   int mbits;
   int bias;

   if (!target) {
      return -1;
   }

   total_bits = size * 8;
   if (size <= 0 || expbits <= 0 || 1 + expbits >= total_bits) {
      error_unreachable("[%s:%d] invalid float layout size=%d expbits=%d", __FILE__, __LINE__, size, expbits);
   }

   memset(target, 0, size);

   // TODO FIX for now, we cheat, by using sscanf and some
   // bit twiddling voodoo that only works for size <= sizeof(double)

   double value = parse_float(p);
   unsigned long long ivalue;

   // TODO FIX this may depend on host byte ordering
   ivalue = *((unsigned long long *) &value);

   mbits = total_bits - 1 - expbits;
   bias = (1 << (expbits - 1)) - 1;

   if (size != 8 || expbits != 11) {
      unsigned long long mantissa = ivalue & ((1ULL << 52) - 1ULL);
      unsigned long long exponent = (ivalue >> 52) & ((1ULL << 11) - 1ULL);
      unsigned long long sign = ivalue >> 63;

      if (mbits < 52) {
         mantissa >>= (52 - mbits);
      }
      else if (mbits > 52) {
         mantissa <<= (mbits - 52);
      }
      exponent = exponent - 1023 + bias;

      if (total_bits < 64) {
         unsigned long long mask = (1ULL << total_bits) - 1ULL;
         ivalue = ((sign << (total_bits - 1)) | (exponent << mbits) | mantissa) & mask;
      }
      else {
         ivalue = (sign << (total_bits - 1)) | (exponent << mbits) | mantissa;
      }
   }

   for (int i = 0; i < size; i++) {
      target[i] = (unsigned char) ivalue;
      ivalue >>= 8;
   }

   return 0;
}

int make_le_float(const char *p, unsigned char *target, int size) {
   int expbits = default_float_expbits_for_size(size);
   if (expbits < 0) {
      error_unimplemented("[%s:%d] size %d floats not supported (yet)", __FILE__, __LINE__, size);
   }
   return make_le_float_layout(p, target, size, expbits);
}

void negate_le_float(unsigned char *target, int size) {
   target[size-1] |= 0x80;
}

int make_be_float_layout(const char *p, unsigned char *target, int size, int expbits) {
   int ret = make_le_float_layout(p, target, size, expbits);
   int tmp;
   for (int i = 0; i < size/2; i++) {
      tmp = target[i];
      target[i] = target[size - 1 - i];
      target[size - 1 - i] = tmp;
   }
   return ret;
}

int make_be_float(const char *p, unsigned char *target, int size) {
   int expbits = default_float_expbits_for_size(size);
   if (expbits < 0) {
      error_unimplemented("[%s:%d] size %d floats not supported (yet)", __FILE__, __LINE__, size);
   }
   return make_be_float_layout(p, target, size, expbits);
}

void negate_be_float(unsigned char *target, int size) {
   (void) size; // unused parameter
   target[0] |= 0x80;
}
