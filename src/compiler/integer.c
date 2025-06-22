#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int make_le_binary(const char *p, unsigned char *target, int size) {
   // TODO FIX
   return -1;
}

static int make_le_hex(const char *p, unsigned char *target, int size) {
   int n = strlen(p);
   int i;

   for (i = 0; i < n; i++) {
      switch(p[i]) {
         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
            target[n - 1 - i] = p[i] - '0';
            break;
         case 'a':
         case 'b':
         case 'c':
         case 'd':
         case 'e':
         case 'f':
            target[n - 1 - i] = p[i] - 'a' + 10;
            break;
         case 'A':
         case 'B':
         case 'C':
         case 'D':
         case 'E':
         case 'F':
            target[n - 1 - i] = p[i] - 'A' + 10;
            break;
            break;
         default:
            return -1;
      }
   }
   return n;
}

static int make_le_octal(const char *p, unsigned char *target, int size) {
   // TODO FIX
   return -1;
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

#ifdef UNIT_TEST
void test(const char *p) {
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

   desire = strtoul(buf, NULL, 10);

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
}
#endif
