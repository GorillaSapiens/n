#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

   if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
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

static int make_le_helper(const char *p, int bpc,
                          unsigned char *target, int size) {
   int len = strlen(p);
   int n = 0;
   int o = 0;
   int a = 0;
   int i;

   p += len - 1;
   for (i = 0; i < len; i++) {
      a |= c2n(*p) << o;
      o += bpc;
      if (o >= 8) {
         target[n++] = a;
         o -= 8;
         a >>= 8;
      }
      p--;
   }
   if (a) {
      target[n++] = a;
   }
   return n;
}

static int make_le_binary(const char *p, unsigned char *target, int size) {
   return make_le_helper(p, 1, target, size);
}

static int make_le_hex(const char *p, unsigned char *target, int size) {
   return make_le_helper(p, 4, target, size);
}

static int make_le_octal(const char *p, unsigned char *target, int size) {
   return make_le_helper(p, 3, target, size);
}

static int make_le_decimal(const char *p, unsigned char *target, int size) {
   int i;
   int max = 1;
   int carry;

   while (*p) {
      carry = 0;
      for (i = 0; i < max; i++) {
         carry = target[i] * 10 + carry;
         target[i] = carry;
         carry >>= 8;
      }
      if (carry) {
         target[max++] = carry;
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
      ret = make_le_binary(p + 2, target, size);
   }
   else if (!strncasecmp(p, "0x", 2)) {
      ret = make_le_hex(p + 2, target, size);
   }
   else if (p[0] == '0') {
      ret = make_le_octal(p + 1, target, size);
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
