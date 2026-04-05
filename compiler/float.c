#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#include "messages.h"
#include "xray.h"
#include "float.h"

static_assert(sizeof(double) == 8);

#define WORD_BITS 32

typedef struct BigNat {
   uint32_t *words;
   int len;
   int cap;
} BigNat;

typedef enum FloatSpecialKind {
   FLOAT_SPECIAL_FINITE,
   FLOAT_SPECIAL_INF,
   FLOAT_SPECIAL_NAN,
} FloatSpecialKind;

typedef struct ParsedFloatValue {
   FloatSpecialKind special;
   bool negative;
   BigNat sig;
   long long exp2;
   long long exp5;
} ParsedFloatValue;

static void bn_init(BigNat *bn) {
   bn->words = NULL;
   bn->len = 0;
   bn->cap = 0;
}

static void bn_free(BigNat *bn) {
   if (!bn) {
      return;
   }
   free(bn->words);
   bn->words = NULL;
   bn->len = 0;
   bn->cap = 0;
}

static void bn_normalize(BigNat *bn) {
   while (bn->len > 0 && bn->words[bn->len - 1] == 0) {
      bn->len--;
   }
}

static void bn_reserve(BigNat *bn, int need) {
   int new_cap;
   uint32_t *tmp;

   if (need <= bn->cap) {
      return;
   }

   new_cap = bn->cap ? bn->cap : 1;
   while (new_cap < need) {
      if (new_cap > INT_MAX / 2) {
         error_user("[%s:%d] float literal is too large", __FILE__, __LINE__);
      }
      new_cap *= 2;
   }

   tmp = (uint32_t *) realloc(bn->words, sizeof(uint32_t) * (size_t) new_cap);
   if (!tmp) {
      error_user("[%s:%d] out of memory", __FILE__, __LINE__);
   }

   if (new_cap > bn->cap) {
      memset(tmp + bn->cap, 0, sizeof(uint32_t) * (size_t) (new_cap - bn->cap));
   }

   bn->words = tmp;
   bn->cap = new_cap;
}

static void bn_set_zero(BigNat *bn) {
   if (!bn) {
      return;
   }
   bn->len = 0;
}

static void bn_from_u32(BigNat *bn, uint32_t value) {
   bn_set_zero(bn);
   if (value != 0) {
      bn_reserve(bn, 1);
      bn->words[0] = value;
      bn->len = 1;
   }
}

static bool bn_is_zero(const BigNat *bn) {
   return !bn || bn->len == 0;
}

static void bn_copy(BigNat *dst, const BigNat *src) {
   if (!dst || !src) {
      return;
   }
   bn_reserve(dst, src->len);
   if (src->len > 0) {
      memcpy(dst->words, src->words, sizeof(uint32_t) * (size_t) src->len);
   }
   if (dst->cap > src->len) {
      memset(dst->words + src->len, 0, sizeof(uint32_t) * (size_t) (dst->cap - src->len));
   }
   dst->len = src->len;
}

static void bn_mul_small(BigNat *bn, uint32_t mul) {
   int i;
   uint64_t carry = 0;

   if (!bn || mul == 1 || bn_is_zero(bn)) {
      return;
   }
   if (mul == 0) {
      bn_set_zero(bn);
      return;
   }

   bn_reserve(bn, bn->len + 1);
   for (i = 0; i < bn->len; i++) {
      uint64_t cur = (uint64_t) bn->words[i] * mul + carry;
      bn->words[i] = (uint32_t) cur;
      carry = cur >> WORD_BITS;
   }
   if (carry) {
      bn->words[bn->len++] = (uint32_t) carry;
   }
}

static void bn_add_small(BigNat *bn, uint32_t add) {
   int i;
   uint64_t carry;

   if (!bn || add == 0) {
      return;
   }

   if (bn_is_zero(bn)) {
      bn_from_u32(bn, add);
      return;
   }

   bn_reserve(bn, bn->len + 1);
   carry = add;
   for (i = 0; i < bn->len && carry; i++) {
      uint64_t cur = (uint64_t) bn->words[i] + carry;
      bn->words[i] = (uint32_t) cur;
      carry = cur >> WORD_BITS;
   }
   if (carry) {
      bn->words[bn->len++] = (uint32_t) carry;
   }
}

static uint32_t bn_mod_small(const BigNat *bn, uint32_t div) {
   int i;
   uint64_t rem = 0;

   if (!bn || div == 0) {
      error_unreachable("[%s:%d] invalid small modulus", __FILE__, __LINE__);
   }

   for (i = bn->len - 1; i >= 0; i--) {
      rem = ((rem << WORD_BITS) | bn->words[i]) % div;
   }
   return (uint32_t) rem;
}

static uint32_t bn_div_small(BigNat *bn, uint32_t div) {
   int i;
   uint64_t rem = 0;

   if (!bn || div == 0) {
      error_unreachable("[%s:%d] invalid small division", __FILE__, __LINE__);
   }

   for (i = bn->len - 1; i >= 0; i--) {
      uint64_t cur = (rem << WORD_BITS) | bn->words[i];
      bn->words[i] = (uint32_t) (cur / div);
      rem = cur % div;
   }
   bn_normalize(bn);
   return (uint32_t) rem;
}

static void bn_mul_pow5(BigNat *bn, long long exp) {
   long long i;
   for (i = 0; i < exp; i++) {
      bn_mul_small(bn, 5);
   }
}

static void bn_shift_left_bits(BigNat *bn, long long shift) {
   int word_shift;
   int bit_shift;
   int old_len;
   int i;
   uint32_t carry = 0;

   if (!bn || shift <= 0 || bn_is_zero(bn)) {
      return;
   }
   if (shift > INT_MAX / 2) {
      error_user("[%s:%d] float literal shift is too large", __FILE__, __LINE__);
   }

   word_shift = (int) (shift / WORD_BITS);
   bit_shift = (int) (shift % WORD_BITS);
   old_len = bn->len;

   bn_reserve(bn, old_len + word_shift + 2);

   if (word_shift > 0) {
      memmove(bn->words + word_shift, bn->words, sizeof(uint32_t) * (size_t) old_len);
      memset(bn->words, 0, sizeof(uint32_t) * (size_t) word_shift);
      bn->len += word_shift;
   }

   if (bit_shift > 0) {
      carry = 0;
      for (i = word_shift; i < bn->len; i++) {
         uint64_t cur = ((uint64_t) bn->words[i] << bit_shift) | carry;
         bn->words[i] = (uint32_t) cur;
         carry = (uint32_t) (cur >> WORD_BITS);
      }
      if (carry) {
         bn->words[bn->len++] = carry;
      }
   }
}

static void bn_shift_right_one(BigNat *bn) {
   int i;
   uint32_t carry = 0;

   if (!bn || bn_is_zero(bn)) {
      return;
   }

   for (i = bn->len - 1; i >= 0; i--) {
      uint32_t new_carry = (uint32_t) ((bn->words[i] & 1u) << (WORD_BITS - 1));
      bn->words[i] = (bn->words[i] >> 1) | carry;
      carry = new_carry;
   }
   bn_normalize(bn);
}

static int bn_bit_length(const BigNat *bn) {
   uint32_t top;
   int bits = 0;

   if (!bn || bn_is_zero(bn)) {
      return 0;
   }

   top = bn->words[bn->len - 1];
   while (top) {
      bits++;
      top >>= 1;
   }
   return (bn->len - 1) * WORD_BITS + bits;
}

static int bn_cmp(const BigNat *a, const BigNat *b) {
   int i;

   if (a->len != b->len) {
      return a->len < b->len ? -1 : 1;
   }
   for (i = a->len - 1; i >= 0; i--) {
      if (a->words[i] != b->words[i]) {
         return a->words[i] < b->words[i] ? -1 : 1;
      }
   }
   return 0;
}

static int bn_cmp_shifted(const BigNat *a, const BigNat *b, int shift_bits) {
   BigNat tmp;
   int cmp;

   bn_init(&tmp);
   bn_copy(&tmp, b);
   bn_shift_left_bits(&tmp, shift_bits);
   cmp = bn_cmp(a, &tmp);
   bn_free(&tmp);
   return cmp;
}

static void bn_sub_inplace(BigNat *a, const BigNat *b) {
   int i;
   uint64_t borrow = 0;

   if (!a || !b || bn_cmp(a, b) < 0) {
      error_unreachable("[%s:%d] invalid bigint subtraction", __FILE__, __LINE__);
   }

   for (i = 0; i < a->len; i++) {
      uint64_t av = a->words[i];
      uint64_t bv = (i < b->len) ? b->words[i] : 0;
      uint64_t sub = bv + borrow;
      if (av < sub) {
         a->words[i] = (uint32_t) (((uint64_t) 1 << WORD_BITS) + av - sub);
         borrow = 1;
      }
      else {
         a->words[i] = (uint32_t) (av - sub);
         borrow = 0;
      }
   }

   if (borrow) {
      error_unreachable("[%s:%d] bigint subtraction underflow", __FILE__, __LINE__);
   }

   bn_normalize(a);
}

static void bn_set_bit(BigNat *bn, long long bit_index) {
   int word_index;
   int bit_in_word;

   if (!bn || bit_index < 0) {
      return;
   }
   if (bit_index > INT_MAX / 2) {
      error_user("[%s:%d] float literal is too wide", __FILE__, __LINE__);
   }

   word_index = (int) (bit_index / WORD_BITS);
   bit_in_word = (int) (bit_index % WORD_BITS);
   bn_reserve(bn, word_index + 1);
   if (word_index >= bn->len) {
      memset(bn->words + bn->len, 0, sizeof(uint32_t) * (size_t) (word_index + 1 - bn->len));
      bn->len = word_index + 1;
   }
   bn->words[word_index] |= (uint32_t) (1u << bit_in_word);
}

static int bn_test_bit(const BigNat *bn, int bit_index) {
   int word_index;
   int bit_in_word;

   if (!bn || bit_index < 0) {
      return 0;
   }

   word_index = bit_index / WORD_BITS;
   bit_in_word = bit_index % WORD_BITS;
   if (word_index >= bn->len) {
      return 0;
   }
   return (int) ((bn->words[word_index] >> bit_in_word) & 1u);
}

static void bn_divmod(const BigNat *numerator, const BigNat *denominator, BigNat *quotient, BigNat *remainder) {
   BigNat shifted;
   int shift;
   int i;

   if (!numerator || !denominator || !quotient || !remainder || bn_is_zero(denominator)) {
      error_unreachable("[%s:%d] invalid bigint division", __FILE__, __LINE__);
   }

   bn_set_zero(quotient);
   bn_copy(remainder, numerator);
   if (bn_cmp(remainder, denominator) < 0) {
      return;
   }

   shift = bn_bit_length(remainder) - bn_bit_length(denominator);
   bn_init(&shifted);
   bn_copy(&shifted, denominator);
   bn_shift_left_bits(&shifted, shift);

   for (i = shift; i >= 0; i--) {
      if (bn_cmp(remainder, &shifted) >= 0) {
         bn_sub_inplace(remainder, &shifted);
         bn_set_bit(quotient, i);
      }
      bn_shift_right_one(&shifted);
   }

   bn_free(&shifted);
}

static bool bn_is_odd(const BigNat *bn) {
   return bn && bn->len > 0 && (bn->words[0] & 1u);
}

static void bn_round_ratio_shift(BigNat *out, const BigNat *numerator, const BigNat *denominator, long long shift) {
   BigNat num;
   BigNat den;
   BigNat rem;
   BigNat twice;
   int cmp;

   bn_init(&num);
   bn_init(&den);
   bn_init(&rem);
   bn_init(&twice);

   bn_copy(&num, numerator);
   bn_copy(&den, denominator);
   if (shift > 0) {
      bn_shift_left_bits(&num, shift);
   }
   else if (shift < 0) {
      bn_shift_left_bits(&den, -shift);
   }

   bn_divmod(&num, &den, out, &rem);

   bn_copy(&twice, &rem);
   bn_shift_left_bits(&twice, 1);
   cmp = bn_cmp(&twice, &den);
   if (cmp > 0 || (cmp == 0 && bn_is_odd(out))) {
      bn_add_small(out, 1);
   }

   bn_free(&num);
   bn_free(&den);
   bn_free(&rem);
   bn_free(&twice);
}

static void parsed_float_init(ParsedFloatValue *value) {
   value->special = FLOAT_SPECIAL_FINITE;
   value->negative = false;
   bn_init(&value->sig);
   value->exp2 = 0;
   value->exp5 = 0;
}

static void parsed_float_free(ParsedFloatValue *value) {
   if (!value) {
      return;
   }
   bn_free(&value->sig);
}

static bool string_ieq(const char *a, const char *b) {
   while (*a && *b) {
      if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
         return false;
      }
      a++;
      b++;
   }
   return *a == '\0' && *b == '\0';
}

static int hex_digit_value(char c) {
   if (c >= '0' && c <= '9') {
      return c - '0';
   }
   if (c >= 'a' && c <= 'f') {
      return 10 + c - 'a';
   }
   if (c >= 'A' && c <= 'F') {
      return 10 + c - 'A';
   }
   return -1;
}

static long long parse_signed_decimal_text(const char *text) {
   bool negative = false;
   unsigned long long value = 0;
   bool any_digit = false;
   const char *p = text;

   if (*p == '+' || *p == '-') {
      negative = (*p == '-');
      p++;
   }

   while (*p) {
      unsigned digit;
      if (*p == '_') {
         p++;
         continue;
      }
      if (*p < '0' || *p > '9') {
         error_user("[%s:%d] invalid float exponent '%s'", __FILE__, __LINE__, text);
      }
      digit = (unsigned) (*p - '0');
      any_digit = true;
      if (value > (ULLONG_MAX - digit) / 10ULL) {
         error_user("[%s:%d] float exponent '%s' is too large", __FILE__, __LINE__, text);
      }
      value = value * 10ULL + digit;
      p++;
   }

   if (!any_digit) {
      error_user("[%s:%d] missing float exponent digits", __FILE__, __LINE__);
   }

   if (negative) {
      if (value > (unsigned long long) LLONG_MAX + 1ULL) {
         error_user("[%s:%d] float exponent '%s' is too small", __FILE__, __LINE__, text);
      }
      if (value == (unsigned long long) LLONG_MAX + 1ULL) {
         return LLONG_MIN;
      }
      return -(long long) value;
   }

   if (value > (unsigned long long) LLONG_MAX) {
      error_user("[%s:%d] float exponent '%s' is too large", __FILE__, __LINE__, text);
   }
   return (long long) value;
}

static void normalize_parsed_float(ParsedFloatValue *value) {
   if (!value || value->special != FLOAT_SPECIAL_FINITE || bn_is_zero(&value->sig)) {
      return;
   }

   while (!bn_is_zero(&value->sig) && (value->sig.words[0] & 1u) == 0) {
      bn_div_small(&value->sig, 2);
      value->exp2++;
   }

   while (!bn_is_zero(&value->sig) && bn_mod_small(&value->sig, 5) == 0) {
      bn_div_small(&value->sig, 5);
      value->exp5++;
   }
}

static void parse_decimal_float_literal(const char *text, ParsedFloatValue *value) {
   const char *exp_mark = strchr(text, 'e');
   const char *exp_mark_upper = strchr(text, 'E');
   const char *exp_ptr = exp_mark;
   const char *p = text;
   long long frac_digits = 0;
   long long exp10 = 0;
   bool seen_digit = false;
   bool after_dot = false;

   if (!exp_ptr || (exp_mark_upper && exp_mark_upper < exp_ptr)) {
      exp_ptr = exp_mark_upper;
   }

   while (*p && p != exp_ptr) {
      if (*p == '_') {
         p++;
         continue;
      }
      if (*p == '.') {
         if (after_dot) {
            error_user("[%s:%d] malformed float literal '%s'", __FILE__, __LINE__, text);
         }
         after_dot = true;
         p++;
         continue;
      }
      if (*p < '0' || *p > '9') {
         error_user("[%s:%d] malformed float literal '%s'", __FILE__, __LINE__, text);
      }
      bn_mul_small(&value->sig, 10);
      bn_add_small(&value->sig, (uint32_t) (*p - '0'));
      seen_digit = true;
      if (after_dot) {
         frac_digits++;
      }
      p++;
   }

   if (!seen_digit) {
      error_user("[%s:%d] malformed float literal '%s'", __FILE__, __LINE__, text);
   }

   if (exp_ptr) {
      exp10 = parse_signed_decimal_text(exp_ptr + 1);
   }

   value->exp2 = exp10 - frac_digits;
   value->exp5 = exp10 - frac_digits;
}

static void parse_hex_float_literal(const char *text, ParsedFloatValue *value) {
   const char *p = text;
   const char *exp_ptr;
   long long frac_nibbles = 0;
   long long exp2 = 0;
   bool seen_digit = false;
   bool after_dot = false;

   if (!(p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))) {
      error_user("[%s:%d] malformed hex float literal '%s'", __FILE__, __LINE__, text);
   }
   p += 2;

   exp_ptr = strchr(p, 'p');
   if (!exp_ptr) {
      exp_ptr = strchr(p, 'P');
   }
   if (!exp_ptr) {
      error_user("[%s:%d] malformed hex float literal '%s'", __FILE__, __LINE__, text);
   }

   while (*p && p != exp_ptr) {
      int digit;
      if (*p == '_') {
         p++;
         continue;
      }
      if (*p == '.') {
         if (after_dot) {
            error_user("[%s:%d] malformed hex float literal '%s'", __FILE__, __LINE__, text);
         }
         after_dot = true;
         p++;
         continue;
      }
      digit = hex_digit_value(*p);
      if (digit < 0) {
         error_user("[%s:%d] malformed hex float literal '%s'", __FILE__, __LINE__, text);
      }
      bn_mul_small(&value->sig, 16);
      bn_add_small(&value->sig, (uint32_t) digit);
      seen_digit = true;
      if (after_dot) {
         frac_nibbles++;
      }
      p++;
   }

   if (!seen_digit) {
      error_user("[%s:%d] malformed hex float literal '%s'", __FILE__, __LINE__, text);
   }

   exp2 = parse_signed_decimal_text(exp_ptr + 1);
   value->exp2 = exp2 - 4 * frac_nibbles;
   value->exp5 = 0;
}

static void parse_float_literal(const char *text, ParsedFloatValue *value) {
   const char *p = text;

   parsed_float_init(value);

   if (*p == '+' || *p == '-') {
      value->negative = (*p == '-');
      p++;
   }

   if (string_ieq(p, "inf") || string_ieq(p, "infinity")) {
      value->special = FLOAT_SPECIAL_INF;
      return;
   }
   if (!strncasecmp(p, "nan", 3)) {
      value->special = FLOAT_SPECIAL_NAN;
      return;
   }

   if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      parse_hex_float_literal(p, value);
   }
   else {
      parse_decimal_float_literal(p, value);
   }

   if (bn_is_zero(&value->sig)) {
      value->exp2 = 0;
      value->exp5 = 0;
      return;
   }

   normalize_parsed_float(value);
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

static void set_le_bits_from_bignat(unsigned char *target, int start_bit, int bit_count, const BigNat *value) {
   int i;

   if (!target || bit_count <= 0 || !value) {
      return;
   }

   for (i = 0; i < bit_count; i++) {
      if (bn_test_bit(value, i)) {
         set_le_bit(target, start_bit + i, 1);
      }
   }
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

static void set_le_exponent_bias_plus_e(unsigned char *target, int start_bit, int expbits, long long e) {
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

static long long floor_log2_ratio(const BigNat *numerator, const BigNat *denominator) {
   long long q = (long long) bn_bit_length(numerator) - (long long) bn_bit_length(denominator);

   if (q >= 0) {
      return bn_cmp_shifted(numerator, denominator, (int) q) < 0 ? q - 1 : q;
   }

   return bn_cmp_shifted(denominator, numerator, (int) (-q)) > 0 ? q - 1 : q;
}

static void encode_special_value(unsigned char *target, int mbits, int expbits, bool negative, FloatSpecialKind special) {
   if (negative) {
      set_le_bit(target, mbits + expbits, 1);
   }
   set_le_bits_all_ones(target, mbits, expbits);
   if (special == FLOAT_SPECIAL_NAN && mbits > 0) {
      set_le_bit(target, 0, 1);
   }
}

static int make_le_float_layout_from_parsed(const ParsedFloatValue *value, unsigned char *target, int size, int expbits) {
   int total_bits;
   int mbits;
   BigNat numerator;
   BigNat denominator;
   long long ratio_exp;
   long long e;

   if (!target) {
      return -1;
   }

   total_bits = size * 8;
   if (size <= 0 || expbits <= 0 || 1 + expbits >= total_bits) {
      error_unreachable("[%s:%d] invalid float layout size=%d expbits=%d", __FILE__, __LINE__, size, expbits);
   }

   mbits = total_bits - 1 - expbits;
   memset(target, 0, (size_t) size);

   if (!value) {
      return -1;
   }

   if (value->special != FLOAT_SPECIAL_FINITE) {
      encode_special_value(target, mbits, expbits, value->negative, value->special);
      return 0;
   }

   if (bn_is_zero(&value->sig)) {
      if (value->negative) {
         set_le_bit(target, total_bits - 1, 1);
      }
      return 0;
   }

   bn_init(&numerator);
   bn_init(&denominator);
   bn_copy(&numerator, &value->sig);
   bn_from_u32(&denominator, 1);

   if (value->exp5 > 0) {
      bn_mul_pow5(&numerator, value->exp5);
   }
   else if (value->exp5 < 0) {
      bn_mul_pow5(&denominator, -value->exp5);
   }

   ratio_exp = floor_log2_ratio(&numerator, &denominator);
   e = ratio_exp + value->exp2;

   if (expbits <= 63) {
      uint64_t bias = (1ULL << (expbits - 1)) - 1ULL;
      long long min_normal_e = 1 - (long long) bias;
      long long max_normal_e = (long long) bias;

      if (e > max_normal_e) {
         encode_special_value(target, mbits, expbits, value->negative, FLOAT_SPECIAL_INF);
         bn_free(&numerator);
         bn_free(&denominator);
         return 0;
      }

      if (e >= min_normal_e) {
         BigNat rounded;
         long long shift = (long long) mbits - ratio_exp;
         uint64_t raw_exp = (uint64_t) ((long long) bias + e);

         bn_init(&rounded);
         bn_round_ratio_shift(&rounded, &numerator, &denominator, shift);
         if (bn_bit_length(&rounded) > mbits + 1) {
            bn_shift_right_one(&rounded);
            e++;
            if (e > max_normal_e) {
               bn_free(&rounded);
               encode_special_value(target, mbits, expbits, value->negative, FLOAT_SPECIAL_INF);
               bn_free(&numerator);
               bn_free(&denominator);
               return 0;
            }
            raw_exp = (uint64_t) ((long long) bias + e);
         }

         set_le_bits_u64(target, mbits, expbits, raw_exp);
         set_le_bits_from_bignat(target, 0, mbits, &rounded);
         if (value->negative) {
            set_le_bit(target, total_bits - 1, 1);
         }
         bn_free(&rounded);
         bn_free(&numerator);
         bn_free(&denominator);
         return 0;
      }
      else {
         BigNat rounded;
         long long shift = value->exp2 + mbits + (long long) bias - 1;

         bn_init(&rounded);
         bn_round_ratio_shift(&rounded, &numerator, &denominator, shift);

         if (bn_is_zero(&rounded)) {
            if (value->negative) {
               set_le_bit(target, total_bits - 1, 1);
            }
            bn_free(&rounded);
            bn_free(&numerator);
            bn_free(&denominator);
            return 0;
         }

         if (bn_bit_length(&rounded) > mbits) {
            set_le_bits_u64(target, mbits, expbits, 1);
         }
         else {
            set_le_bits_from_bignat(target, 0, mbits, &rounded);
         }

         if (value->negative) {
            set_le_bit(target, total_bits - 1, 1);
         }

         bn_free(&rounded);
         bn_free(&numerator);
         bn_free(&denominator);
         return 0;
      }
   }
   else {
      BigNat rounded;
      long long shift = (long long) mbits - ratio_exp;

      bn_init(&rounded);
      bn_round_ratio_shift(&rounded, &numerator, &denominator, shift);
      if (bn_bit_length(&rounded) > mbits + 1) {
         bn_shift_right_one(&rounded);
         e++;
      }

      set_le_exponent_bias_plus_e(target, mbits, expbits, e);
      set_le_bits_from_bignat(target, 0, mbits, &rounded);
      if (value->negative) {
         set_le_bit(target, total_bits - 1, 1);
      }

      bn_free(&rounded);
      bn_free(&numerator);
      bn_free(&denominator);
      return 0;
   }
}

double parse_float(const char *p) {
   ParsedFloatValue value;
   unsigned char bytes[8];
   uint64_t bits = 0;
   double ret;
   int i;

   parse_float_literal(p, &value);
   make_le_float_layout_from_parsed(&value, bytes, 8, 11);
   for (i = 0; i < 8; i++) {
      bits |= (uint64_t) bytes[i] << (8 * i);
   }
   memcpy(&ret, &bits, sizeof(ret));
   parsed_float_free(&value);
   return ret;
}

static int ieee754_float_expbits_for_size(int size) {
   switch (size) {
      case 2: return 5;
      case 4: return 8;
      case 8: return 11;
      default:
         return -1;
   }
}

static int simple_float_expbits_for_size(int size) {
   int total_bits;
   int expbits;
   double raw;

   if (size <= 0) {
      return -1;
   }

   total_bits = size * 8;
   raw = 3.0 * (log((double) size) / log(2.0)) + 2.0;
   expbits = (int) floor(raw + 0.5);
   if (expbits < 1) {
      expbits = 1;
   }
   if (1 + expbits >= total_bits) {
      expbits = total_bits - 2;
   }
   return expbits > 0 ? expbits : -1;
}

static int pack_le_float_layout(const char *p, unsigned char *target, int size, int expbits) {
   ParsedFloatValue value;
   int ret;

   parse_float_literal(p, &value);
   ret = make_le_float_layout_from_parsed(&value, target, size, expbits);
   parsed_float_free(&value);
   return ret;
}

static int pack_le_ieee754_float(const char *p, unsigned char *target, int size) {
   int expbits = ieee754_float_expbits_for_size(size);

   if (expbits < 0) {
      error_user("[%s:%d] $float:ieee754 only supports $size:2, $size:4, and $size:8", __FILE__, __LINE__);
   }

   return pack_le_float_layout(p, target, size, expbits);
}

static int pack_le_simple_float(const char *p, unsigned char *target, int size) {
   int expbits = simple_float_expbits_for_size(size);

   if (expbits < 0) {
      error_user("[%s:%d] $float:simple requires a positive $size", __FILE__, __LINE__);
   }

   return pack_le_float_layout(p, target, size, expbits);
}

typedef struct FloatStyleDef {
   const char *name;
   int (*expbits_for_size)(int size);
   int (*pack_le)(const char *p, unsigned char *target, int size);
} FloatStyleDef;

static const FloatStyleDef FLOAT_STYLE_TABLE[] = {
   { "ieee754", ieee754_float_expbits_for_size, pack_le_ieee754_float },
   { "simple",  simple_float_expbits_for_size,  pack_le_simple_float  },
};

static const FloatStyleDef *find_float_style(const char *style) {
   int i;

   if (!style) {
      return NULL;
   }

   for (i = 0; i < (int) (sizeof(FLOAT_STYLE_TABLE) / sizeof(FLOAT_STYLE_TABLE[0])); i++) {
      if (!strcmp(FLOAT_STYLE_TABLE[i].name, style)) {
         return &FLOAT_STYLE_TABLE[i];
      }
   }

   return NULL;
}

bool float_style_is_known(const char *style) {
   return find_float_style(style) != NULL;
}

int float_style_expbits_for_size(const char *style, int size) {
   const FloatStyleDef *def = find_float_style(style);
   if (!def) {
      return -1;
   }
   return def->expbits_for_size(size);
}

int make_le_float_style(const char *p, unsigned char *target, int size, const char *style) {
   const FloatStyleDef *def = find_float_style(style);

   if (!def) {
      error_unreachable("[%s:%d] unknown float style '%s'", __FILE__, __LINE__, style ? style : "(null)");
   }

   return def->pack_le(p, target, size);
}

void negate_le_float(unsigned char *target, int size) {
   target[size-1] |= 0x80;
}

int make_be_float_style(const char *p, unsigned char *target, int size, const char *style) {
   int ret = make_le_float_style(p, target, size, style);
   reverse_bytes(target, size);
   return ret;
}

void negate_be_float(unsigned char *target, int size) {
   (void) size;
   target[0] |= 0x80;
}
