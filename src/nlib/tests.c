#include <stdio.h>
#include <stdlib.h>

#include "nlib.h"

#define LOOPS 100000

long extend3(long arg) {
   if (arg & 0x800000)
      return arg | 0xFF000000;
   return arg & 0xffffff;
}

long lrand(void) {
   long ret = rand();
   ret <<= 16;
   ret |= rand();
   return ret;
}

void test1(const char *name, void(*fn)(void),
   unsigned char bshift, char bval1, char bval2, char bval3, char bval4,
   unsigned char ashift, char aval1, char aval2, char aval3, char aval4) {

   unsigned char shift = bshift;
   char val1 = bval1;
   char val2 = bval2;
   char val3 = bval3;
   char val4 = bval4;

   nl_size = 1;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         val1 != aval1 || val2 != aval2 ||
         val3 != aval3 || val4 != aval4) {

      printf("test1 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void test2(const char *name, void(*fn)(void),
   unsigned char bshift, int bval1, int bval2, int bval3, int bval4,
   unsigned char ashift, int aval1, int aval2, int aval3, int aval4) {

   unsigned char shift = bshift;
   int val1 = bval1;
   int val2 = bval2;
   int val3 = bval3;
   int val4 = bval4;

   nl_size = 2;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         val1 != aval1 || val2 != aval2 ||
         val3 != aval3 || val4 != aval4) {

      printf("test2 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void test3(const char *name, void(*fn)(void),
   unsigned char bshift, long bval1, long bval2, long bval3, long bval4,
   unsigned char ashift, long aval1, long aval2, long aval3, long aval4) {

   unsigned char shift = bshift;
   long val1, val2, val3, val4;

   bval1 &= 0xFFFFFF;
   bval2 &= 0xFFFFFF;
   bval3 &= 0xFFFFFF;
   bval4 &= 0xFFFFFF;
   aval1 &= 0xFFFFFF;
   aval2 &= 0xFFFFFF;
   aval3 &= 0xFFFFFF;
   aval4 &= 0xFFFFFF;

   val1 = bval1;
   val2 = bval2;
   val3 = bval3;
   val4 = bval4;

   nl_size = 3;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         (val1 & 0xFFFFFF) != aval1 || (val2 & 0xFFFFFF) != aval2 ||
         (val3 & 0xFFFFFF) != aval3 || (val4 & 0xFFFFFF) != aval4) {

      printf("test3 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void test4(const char *name, void(*fn)(void),
   unsigned char bshift, long bval1, long bval2, long bval3, long bval4,
   unsigned char ashift, long aval1, long aval2, long aval3, long aval4) {

   unsigned char shift = bshift;
   long val1 = bval1;
   long val2 = bval2;
   long val3 = bval3;
   long val4 = bval4;

   nl_size = 4;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         val1 != aval1 || val2 != aval2 ||
         val3 != aval3 || val4 != aval4) {

      printf("test4 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%08lx v2:%08lx v3:%08lx v4:%08lx\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void test1x2(const char *name, void(*fn)(void),
   unsigned char bshift, char bval1, char bval2, int bval3, char bval4,
   unsigned char ashift, char aval1, char aval2, int aval3, char aval4) {

   unsigned char shift = bshift;
   char val1 = bval1;
   char val2 = bval2;
   int val3 = bval3;
   char val4 = bval4;

   nl_size = 1;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         val1 != aval1 || val2 != aval2 ||
         val3 != aval3 || val4 != aval4) {

      printf("test1 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void test2x4(const char *name, void(*fn)(void),
   unsigned char bshift, int bval1, int bval2, long bval3, int bval4,
   unsigned char ashift, int aval1, int aval2, long aval3, int aval4) {

   unsigned char shift = bshift;
   int val1 = bval1;
   int val2 = bval2;
   long val3 = bval3;
   int val4 = bval4;

   nl_size = 2;
   nl_shift = bshift;

   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   fn();

   if (nl_shift != ashift ||
         val1 != aval1 || val2 != aval2 ||
         val3 != aval3 || val4 != aval4) {

      printf("test2 : %s ERROR\n", name);
      printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%08lx v4:%04x\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%04x v2:%04x v3:%08lx v4:%04x\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%04x v2:%04x v3:%08lx v4:%04x\n",
         nl_size, nl_shift, val1, val2, val3, val4);
      exit(0);
   }
}

void add8_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("add8", add8,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("add8, n=1 PASS\n");
}

void add16_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("add16", add16,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("add16, n=2 PASS\n");
}

void add24_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("add24", add24,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("add24, n=3 PASS\n");
}

void add32_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("add32", add32,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("add32, n=4 PASS\n");
}

void addN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("addN", addN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("addN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("addN", addN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("addN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("addN", addN,
         0, v1, v2, v3, v4,
         0, v1, v2, (v1+v2), v4);
   }
   printf("addN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("addN", addN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("addN, n=4 PASS\n");
}

void inc8_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test1("inc8", inc8,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("inc8, n=1 PASS\n");
}

void inc16_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test2("inc16", inc16,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("inc16, n=2 PASS\n");
}

void inc24_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("inc24", inc24,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("inc24, n=3 PASS\n");
}

void inc32_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("inc32", inc32,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("inc32, n=4 PASS\n");
}

void incN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test1("incN", incN,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("incN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test2("incN", incN,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("incN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("incN", incN,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("incN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test4("incN", incN,
         0, v1, v2, v3, v4,
         0, v1+1, v2, v3, v4);
   }
   printf("incN, n=4 PASS\n");
}

void sub8_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("sub8", sub8,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("sub8, n=1 PASS\n");
}

void sub16_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("sub16", sub16,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("sub16, n=2 PASS\n");
}

void sub24_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("sub24", sub24,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("sub24, n=3 PASS\n");
}

void sub32_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("sub32", sub32,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("sub32, n=4 PASS\n");
}

void subN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("subN", subN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("subN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("subN", subN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("subN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("subN", subN,
         0, v1, v2, v3, v4,
         0, v1, v2, (v1-v2), v4);
   }
   printf("subN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("subN", subN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("subN, n=4 PASS\n");
}

void dec8_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test1("dec8", dec8,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("dec8, n=1 PASS\n");
}

void dec16_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test2("dec16", dec16,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("dec16, n=2 PASS\n");
}

void dec24_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("dec24", dec24,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("dec24, n=3 PASS\n");
}

void dec32_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("dec32", dec32,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("dec32, n=4 PASS\n");
}

void decN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test1("decN", decN,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("decN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = 0, v3 = 0, v4 = 0;
      test2("decN", decN,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("decN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test3("decN", decN,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("decN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = 0, v3 = 0, v4 = 0;
      test4("decN", decN,
         0, v1, v2, v3, v4,
         0, v1-1, v2, v3, v4);
   }
   printf("decN, n=4 PASS\n");
}

void mulN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1x2("mulN", mulN,
         0, v1, v2, v3, v4,
         0, v1, v2, (int)v1*(int)v2, v4);
   }
   printf("mulN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2x4("mulN", mulN,
         0, v1, v2, v3, v4,
         0, v1, v2, (long)v1*(long)v2, v4);
   }
   printf("mulN, n=2 PASS\n");

#if 0
   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4x8("mulN", mulN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("subN, n=4 PASS\n");
#endif
}

void divN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test1("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test1("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("divN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test2("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test2("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("divN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test4("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test4("divN", divN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("divN, n=4 PASS\n");
}

void remN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test1("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test1("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("remN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test2("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test2("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("remN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      if (v2 != 0) {
         test4("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, v1/v2, v1%v2);
      }
      else {
         test4("remN", remN,
               0, v1, v2, v3, v4,
               0, v1, v2, ~0, v1);
      }
   }
   printf("remN, n=4 PASS\n");
}

void bit_andN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("bit_andN", bit_andN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1&v2, v4);
   }
   printf("bit_andN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("bit_andN", bit_andN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1&v2, v4);
   }
   printf("bit_andN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("bit_andN", bit_andN,
         0, v1, v2, v3, v4,
         0, v1, v2, (v1&v2), v4);
   }
   printf("bit_andN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("bit_andN", bit_andN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1&v2, v4);
   }
   printf("bit_andN, n=4 PASS\n");
}

void bit_orN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("bit_orN", bit_orN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1|v2, v4);
   }
   printf("bit_orN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("bit_orN", bit_orN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1|v2, v4);
   }
   printf("bit_orN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("bit_orN", bit_orN,
         0, v1, v2, v3, v4,
         0, v1, v2, (v1|v2), v4);
   }
   printf("bit_orN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("bit_orN", bit_orN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1|v2, v4);
   }
   printf("bit_orN, n=4 PASS\n");
}

void bit_xorN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("bit_xorN", bit_xorN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1^v2, v4);
   }
   printf("bit_xorN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("bit_xorN", bit_xorN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1^v2, v4);
   }
   printf("bit_xorN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("bit_xorN", bit_xorN,
         0, v1, v2, v3, v4,
         0, v1, v2, (v1^v2), v4);
   }
   printf("bit_xorN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("bit_xorN", bit_xorN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1^v2, v4);
   }
   printf("bit_xorN, n=4 PASS\n");
}

void bit_notN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("bit_notN", bit_notN,
         0, v1, v2, v3, v4,
         0, v1, v2, ~v1, v4);
   }
   printf("bit_notN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("bit_notN", bit_notN,
         0, v1, v2, v3, v4,
         0, v1, v2, ~v1, v4);
   }
   printf("bit_notN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("bit_notN", bit_notN,
         0, v1, v2, v3, v4,
         0, v1, v2, ~v1, v4);
   }
   printf("bit_notN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("bit_notN", bit_notN,
         0, v1, v2, v3, v4,
         0, v1, v2, ~v1, v4);
   }
   printf("bit_notN, n=4 PASS\n");
}

void eqN_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("eqN", eqN,
         0, v1, v2, v3, v4,
         v1 == v2, v1, v2, v3, v4);
   }
   printf("eqN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("eqN", eqN,
         0, v1, v2, v3, v4,
         v1 == v2, v1, v2, v3, v4);
   }
   printf("eqN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("eqN", eqN,
         0, v1, v2, v3, v4,
         v1 == v2, v1, v2, v3, v4);
   }
   printf("eqN, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("eqN", eqN,
         0, v1, v2, v3, v4,
         v1 == v2, v1, v2, v3, v4);
   }
   printf("eqN, n=4 PASS\n");
}

void ltNs_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      signed char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("ltNs", ltNs,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNs, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("ltNs", ltNs,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNs, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = extend3(lrand()), v2 = extend3(lrand()), v3 = 0, v4 = 0;
      test3("ltNs", ltNs,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNs, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("ltNs", ltNs,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNs, n=4 PASS\n");
}

void leNs_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      signed char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("leNs", leNs,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNs, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("leNs", leNs,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNs, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = extend3(lrand()), v2 = extend3(lrand()), v3 = 0, v4 = 0;
      test3("leNs", leNs,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNs, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("leNs", leNs,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNs, n=4 PASS\n");
}

void ltNu_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      unsigned char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("ltNu", ltNu,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNu, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("ltNu", ltNu,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNu, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned long v1 = lrand() & 0xFFFFFF, v2 = lrand() & 0xFFFFFF, v3 = 0, v4 = 0;
      test3("ltNu", ltNu,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNu, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("ltNu", ltNu,
         0, v1, v2, v3, v4,
         v1 < v2, v1, v2, v3, v4);
   }
   printf("ltNu, n=4 PASS\n");
}

void leNu_tests(void) {
   long i;

   for (i = 0; i < LOOPS; i++) {
      unsigned char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("leNu", leNu,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNu, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("leNu", leNu,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNu, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test3("leNu", leNu,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNu, n=3 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      unsigned long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("leNu", leNu,
         0, v1, v2, v3, v4,
         v1 <= v2, v1, v2, v3, v4);
   }
   printf("leNu, n=4 PASS\n");
}

int main(void) {
   printf("tests\n\n");

   add8_tests();
   add16_tests();
   add24_tests();
   add32_tests();
   addN_tests();
   printf("\n");

   sub8_tests();
   sub16_tests();
   sub24_tests();
   sub32_tests();
   subN_tests();
   printf("\n");

   inc8_tests();
   inc16_tests();
   inc24_tests();
   inc32_tests();
   incN_tests();
   printf("\n");

   dec8_tests();
   dec16_tests();
   dec24_tests();
   dec32_tests();
   decN_tests();
   printf("\n");

   mulN_tests();
   printf("\n");

   divN_tests();
   remN_tests();
   printf("\n");

   bit_andN_tests();
   bit_orN_tests();
   bit_notN_tests();
   bit_xorN_tests();
   printf("\n");

   eqN_tests();
   ltNs_tests();
   leNs_tests();
   ltNu_tests();
   leNu_tests();

   return 0;
}
