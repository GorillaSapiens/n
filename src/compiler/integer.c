#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

   // make a copy, strip out underscores;
   char *copy = strdup(p);
   char *q = copy;
   while (*p) {
      if (*p != '_') {
         *q++ = *p++;
      }
      else {
         p++;
      }
   }
   *q = 0;

   if (!strcmp(copy, "0")) {
      target[0] = 0;
      return 1;
   }
   else if (!strncasecmp(copy, "0b", 2)) {
      return make_le_binary(copy + 2, target, size);
   }
   else if (!strncasecmp(copy, "0x", 2)) {
      return make_le_hex(copy + 2, target, size);
   }
   else if (copy[0] == '0') {
      return make_le_octal(copy + 1, target, size);
   }
   else {
      return make_le_decimal(copy, target, size);
   }
}

void negate_le_int(unsigned char *target, int size) {
   int carry = 1;
   for (int i = 0; i < size; i++) {
      carry = (target[i] ^  0xFF) + carry;
      target[i] = carry;
      carry >>= 8;
   }
}

#ifdef UNIT_TEST
static unsigned long parse_number(const char *str) {
   if (str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) {
      // handles 0b (binary)
      return strtoul(str + 2, NULL, 2);
   }
   // handles 0x (hex), 0 (oct), and default decimal
   return strtoul(str, NULL, 0);
}

static void test(const char *p) {
   unsigned char buf[16];
   char *b = buf;
   const char *q = p;
   int i, n;
   unsigned long desire;

   while (*q) {
      if (*q != '_') {
         *b++ = *q++;
      }
      else {
         q++;
      }
   }
   *b = 0;

   desire = parse_number(buf);

   n = make_le_int(p, buf, sizeof(buf));
   printf("%ld= (%d) ", desire, n);

   for (i = 0; i < n; i++) {
      printf("%02x:%02lx ", buf[i], (desire >> (8 * i)) & 0xFF);
   }
   printf("\n");
}

void main(void) {
   test("0");
   test("1");
   test("255");
   test("256");
   test("1234");
   test("12345");
   test("4_294_967_295");
   test("4_294_967_296");
   test("0x1");
   test("0x12");
   test("0x123");
   test("0x1234");
   test("0x123456789ABCDEF");
   test("0b1100001110100101");
   test("076543210");
}
#endif
