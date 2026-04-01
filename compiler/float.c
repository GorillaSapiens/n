#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "messages.h"
#include "xray.h"

// TODO FIX we'll need to revisit this later, for now just ensure
// our doubles are big enough.
static_assert(sizeof(double) == 8);
static_assert(sizeof(unsigned long long) == 8);

#define HOST_DOUBLE_EXPBITS 11
#define HOST_DOUBLE_MBITS   52
#define HOST_DOUBLE_BIAS    1023

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

static uint64_t double_bits(double value) {
   uint64_t bits = 0;
   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

static int highest_set_bit_u64(uint64_t value) {
   int bit = -1;
   while (value) {
      bit++;
      value >>= 1;
   }
   return bit;
}

static void set_le_bit(unsigned char *target, int bit_index, int bit_value) {
   int byte_index;
   int bit_in_byte;

   if (!target || bit_index < 0) {
      return;
   }

   byte_index = bit_index / 8;
   bit_in_byte = bit_index % 8;
   if (bit_value) {
      target[byte_index] |= (unsigned char) (1u << bit_in_byte);
   }
   else {
      target[byte_index] &= (unsigned char) ~(1u << bit_in_byte);
   }
}

static void set_le_bits_u64(unsigned char *target, int start_bit, int bit_count, uint64_t value) {
   int i;

   if (!target || bit_count <= 0) {
      return;
   }

   for (i = 0; i < bit_count; i++) {
      set_le_bit(target, start_bit + i, (int) ((value >> i) & 1ULL));
   }
}

static void set_le_bits_all_ones(unsigned char *target, int start_bit, int bit_count) {
   int i;

   if (!target || bit_count <= 0) {
      return;
   }

   for (i = 0; i < bit_count; i++) {
      set_le_bit(target, start_bit + i, 1);
   }
}

static void set_le_field_from_shifted_u64(unsigned char *target, int start_bit, int field_bits, uint64_t source, int source_bits, int shift) {
   int i;

   if (!target || field_bits <= 0 || source_bits <= 0) {
      return;
   }

   for (i = 0; i < source_bits; i++) {
      int target_bit = i + shift;
      if (target_bit < 0 || target_bit >= field_bits) {
         continue;
      }
      if ((source >> i) & 1ULL) {
         set_le_bit(target, start_bit + target_bit, 1);
      }
   }
}


static int le_field_has_any_bits(const unsigned char *target, int start_bit, int bit_count) {
   int i;

   if (!target || bit_count <= 0) {
      return 0;
   }

   for (i = 0; i < bit_count; i++) {
      int bit_index = start_bit + i;
      int byte_index = bit_index / 8;
      int bit_in_byte = bit_index % 8;
      if ((target[byte_index] >> bit_in_byte) & 1) {
         return 1;
      }
   }

   return 0;
}
static void increment_le_field(unsigned char *target, int start_bit, int bit_count) {
   int i;
   for (i = 0; i < bit_count; i++) {
      int bit_index = start_bit + i;
      int byte_index = bit_index / 8;
      int bit_in_byte = bit_index % 8;
      int current = (target[byte_index] >> bit_in_byte) & 1;
      if (!current) {
         target[byte_index] |= (unsigned char) (1u << bit_in_byte);
         return;
      }
      target[byte_index] &= (unsigned char) ~(1u << bit_in_byte);
   }
}

static void decrement_le_field(unsigned char *target, int start_bit, int bit_count) {
   int i;
   for (i = 0; i < bit_count; i++) {
      int bit_index = start_bit + i;
      int byte_index = bit_index / 8;
      int bit_in_byte = bit_index % 8;
      int current = (target[byte_index] >> bit_in_byte) & 1;
      if (current) {
         target[byte_index] &= (unsigned char) ~(1u << bit_in_byte);
         return;
      }
      target[byte_index] |= (unsigned char) (1u << bit_in_byte);
   }
}

static void set_le_exponent_bias_plus_e(unsigned char *target, int start_bit, int expbits, int e) {
   int i;

   if (!target || expbits <= 0) {
      return;
   }

   for (i = 0; i < expbits - 1; i++) {
      set_le_bit(target, start_bit + i, 1);
   }
   set_le_bit(target, start_bit + expbits - 1, 0);

   if (e >= 0) {
      for (i = 0; i < e; i++) {
         increment_le_field(target, start_bit, expbits);
      }
   }
   else {
      for (i = 0; i < -e; i++) {
         decrement_le_field(target, start_bit, expbits);
      }
   }
}

static void reverse_bytes(unsigned char *target, int size) {
   int i;

   if (!target || size <= 1) {
      return;
   }

   for (i = 0; i < size / 2; i++) {
      unsigned char tmp = target[i];
      target[i] = target[size - 1 - i];
      target[size - 1 - i] = tmp;
   }
}

int make_le_float_layout(const char *p, unsigned char *target, int size, int expbits) {
   int total_bits;
   int mbits;
   int warned_best_effort = 0;
   double value;
   uint64_t host;
   uint64_t host_mantissa;
   uint64_t host_exponent;
   uint64_t host_sign;

   if (!target) {
      return -1;
   }

   total_bits = size * 8;
   if (size <= 0 || expbits <= 0 || 1 + expbits >= total_bits) {
      error_unreachable("[%s:%d] invalid float layout size=%d expbits=%d", __FILE__, __LINE__, size, expbits);
   }

   mbits = total_bits - 1 - expbits;
   if (size > (int) sizeof(double) || mbits > HOST_DOUBLE_MBITS) {
      warning("[%s:%d] float layout SE%dM%d in %d bytes exceeds host double precision; using best-effort packing",
            __FILE__, __LINE__, expbits, mbits, size);
      warned_best_effort = 1;
   }

   memset(target, 0, size);

   value = parse_float(p);
   host = double_bits(value);
   host_mantissa = host & ((1ULL << HOST_DOUBLE_MBITS) - 1ULL);
   host_exponent = (host >> HOST_DOUBLE_MBITS) & ((1ULL << HOST_DOUBLE_EXPBITS) - 1ULL);
   host_sign = host >> 63;

   if (host_sign) {
      set_le_bit(target, total_bits - 1, 1);
   }

   if (host_exponent == 0 && host_mantissa == 0) {
      return 0;
   }

   if (host_exponent == ((1ULL << HOST_DOUBLE_EXPBITS) - 1ULL)) {
      set_le_bits_all_ones(target, mbits, expbits);
      if (host_mantissa != 0 && mbits > 0) {
         set_le_field_from_shifted_u64(target, 0, mbits, host_mantissa, HOST_DOUBLE_MBITS, mbits - HOST_DOUBLE_MBITS);
         if (!warned_best_effort && mbits > HOST_DOUBLE_MBITS) {
            warning("[%s:%d] float layout SE%dM%d in %d bytes exceeds host double mantissa precision; using best-effort packing",
                  __FILE__, __LINE__, expbits, mbits, size);
         }
         if (!le_field_has_any_bits(target, 0, mbits)) {
            set_le_bit(target, 0, 1);
         }
      }
      return 0;
   }

   {
      uint64_t sig53;
      int e;

      if (host_exponent != 0) {
         sig53 = (1ULL << HOST_DOUBLE_MBITS) | host_mantissa;
         e = (int) host_exponent - HOST_DOUBLE_BIAS;
      }
      else {
         int lead = highest_set_bit_u64(host_mantissa);
         sig53 = host_mantissa << (HOST_DOUBLE_MBITS - lead);
         e = lead - 1074;
      }

      if (expbits <= 63) {
         uint64_t bias = (1ULL << (expbits - 1)) - 1ULL;
         uint64_t max_exp = (1ULL << expbits) - 1ULL;
         long long raw_exp = (long long) bias + (long long) e;

         if (raw_exp >= (long long) max_exp) {
            set_le_bits_all_ones(target, mbits, expbits);
            return 0;
         }

         if (raw_exp > 0) {
            uint64_t frac52 = sig53 - (1ULL << HOST_DOUBLE_MBITS);
            set_le_bits_u64(target, mbits, expbits, (uint64_t) raw_exp);
            set_le_field_from_shifted_u64(target, 0, mbits, frac52, HOST_DOUBLE_MBITS, mbits - HOST_DOUBLE_MBITS);
            return 0;
         }

         {
            int shift = e + (int) bias + mbits - 53;
            set_le_field_from_shifted_u64(target, 0, mbits, sig53, HOST_DOUBLE_MBITS + 1, shift);
            return 0;
         }
      }

      /* expbits > 63 only happens for formats larger than host double. Keep a
         best-effort normal encoding with an explicit bias pattern. */
      set_le_exponent_bias_plus_e(target, mbits, expbits, e);
      set_le_field_from_shifted_u64(target, 0, mbits, sig53 - (1ULL << HOST_DOUBLE_MBITS), HOST_DOUBLE_MBITS, mbits - HOST_DOUBLE_MBITS);
      return 0;
   }
}

int make_le_float(const char *p, unsigned char *target, int size) {
   int expbits = default_float_expbits_for_size(size);
   if (expbits < 0) {
      error_user("[%s:%d] layout must be specified for non IEEE 754 floats", __FILE__, __LINE__);
   }
   return make_le_float_layout(p, target, size, expbits);
}

void negate_le_float(unsigned char *target, int size) {
   target[size-1] |= 0x80;
}

int make_be_float_layout(const char *p, unsigned char *target, int size, int expbits) {
   int ret = make_le_float_layout(p, target, size, expbits);
   reverse_bytes(target, size);
   return ret;
}

int make_be_float(const char *p, unsigned char *target, int size) {
   int expbits = default_float_expbits_for_size(size);
   if (expbits < 0) {
      error_user("[%s:%d] layout must be specified for non IEEE 754 floats", __FILE__, __LINE__);
   }
   return make_be_float_layout(p, target, size, expbits);
}

void negate_be_float(unsigned char *target, int size) {
   (void) size; // unused parameter
   target[0] |= 0x80;
}
