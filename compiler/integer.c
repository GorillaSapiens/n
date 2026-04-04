#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "messages.h"
#include "xray.h"

static long long parse_binary(const char *p) {
   long long ret = 0;
   p += 2;
   while (*p) {
      ret <<= 1;
      ret |= (*p - '0');
      p++;
   }
   return ret;
}

long long parse_int(const char *p) {
   long long ret;
   bool negate = false;

   if (*p == '-') {
      negate = true;
      p++;
   }

   if (!strcmp(p, "true")) {
      ret = 1;
   }
   else if (!strcmp(p, "false")) {
      ret = 0;
   }
   else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
      ret = parse_binary(p);
   }
   else {
      ret = atoll(p);
   }

   if (negate) {
      ret = -ret;
   }

   return ret;
}

static int c2n(char c) {
   if (c >= '0' && c <= '9') {
      return c - '0';
   }
   else if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
   }
   else if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
   }
   return -1;
}

static int make_le_helper(const char *digits, const char *op, int bpc,
                          unsigned char *target, int size) {
   int len = strlen(digits);
   int n = 0;
   int used = 0;
   int o = 0;
   int a = 0;
   int i;
   bool overflow = false;
   const char *p = digits + len - 1;

   for (i = 0; i < len; i++) {
      unsigned char byte;

      a |= c2n(*p) << o;
      o += bpc;
      if (o >= 8) {
         byte = a;
         if (n < size) {
            target[n] = byte;
         }
         else if (byte) {
            overflow = true;
         }
         if (byte) {
            used = n + 1;
         }
         n++;
         o -= 8;
         a >>= 8;
      }
      p--;
   }

   if (o > 0 || n == 0) {
      unsigned char byte = a;
      if (n < size) {
         target[n] = byte;
      }
      else if (byte) {
         overflow = true;
      }
      if (byte) {
         used = n + 1;
      }
      n++;
   }

   if (overflow) {
      warning("integer '%s' is too big for %d bytes!", op, size);
      // TODO FIX should this be an error?
      return size;
   }

   return used ? used : 1;
}

static int make_le_binary(const char *p, const char *op,
                          unsigned char *target, int size) {
   return make_le_helper(p, op, 1, target, size);
}

static int make_le_hex(const char *p, const char *op,
                       unsigned char *target, int size) {
   return make_le_helper(p, op, 4, target, size);
}

static int make_le_octal(const char *p, const char *op,
                         unsigned char *target, int size) {
   return make_le_helper(p, op, 3, target, size);
}

static int make_le_decimal(const char *p, unsigned char *target, int size) {
   int i;
   int max = 1;
   int carry;
   const char *op = p;

   while (*p) {
      carry = 0;
      for (i = 0; i < max; i++) {
         carry = target[i] * 10 + carry;
         target[i] = carry;
         carry >>= 8;
      }
      if (carry) {
         target[max++] = carry;
         if (max > size) {
            warning("integer '%s' is too big for %d bytes!", op, size);
            // TODO FIX should this be an error?
            return size;
         }
      }

      carry = target[0] + *p - '0';
      target[0] = carry;
      carry >>= 8;
      i = 1;
      while (i < max && carry) {
         carry = target[i] + carry;
         target[i] = carry;
         carry >>= 8;
         i++;
      }
      if (carry) {
         target[max++] = carry;
         if (max > size) {
            warning("integer '%s' is too big for %d bytes!", op, size);
            // TODO FIX should this be an error?
            return size;
         }
      }
      p++;
   }

   return max;
}

int make_le_int(const char *p, unsigned char *target, int size) {

   memset(target, 0, size);

   int ret;

   if (!strcmp(p, "0")) {
      for (int i = 0; i < size; i++) {
         target[i] = 0;
      }
      ret = 1;
   }
   else if (!strncasecmp(p, "0b", 2)) {
      ret = make_le_binary(p + 2, p, target, size);
   }
   else if (!strncasecmp(p, "0x", 2)) {
      ret = make_le_hex(p + 2, p, target, size);
   }
   else if (p[0] == '0') {
      ret = make_le_octal(p + 1, p, target, size);
   }
   else {
      ret = make_le_decimal(p, target, size);
   }

   return ret;
}

void negate_le_int(unsigned char *target, int size) {
   int carry = 1;
   for (int i = 0; i < size; i++) {
      carry = (target[i] ^  0xFF) + carry;
      target[i] = carry;
      carry >>= 8;
   }
}

int make_be_int(const char *p, unsigned char *target, int size) {
   int ret = make_le_int(p, target, size);
   int tmp;
   for (int i = 0; i < size/2; i++) {
      tmp = target[i];
      target[i] = target[size - 1 - i];
      target[size - 1 - i] = tmp;
   }
   return ret;
}

void negate_be_int(unsigned char *target, int size) {
   int carry = 1;
   for (int i = size - 1; i >= 0; i--) {
      carry = (target[i] ^  0xFF) + carry;
      target[i] = carry;
      carry >>= 8;
   }
}
