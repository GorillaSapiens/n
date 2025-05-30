#include <stdio.h>
#include <stdlib.h>

#include "nlib.h"

#define LOOPS 10000

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
      printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, bshift, bval1, bval2, bval3, bval4);
      printf("expect: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
         nl_size, ashift, aval1, aval2, aval3, aval4);
      printf("realit: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n",
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

void addN_tests(void) {
   int i;
   
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
      test4("addN", addN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1+v2, v4);
   }
   printf("addN, n=4 PASS\n");
}

void subN_tests(void) {
   int i;
   
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
      test4("subN", subN,
         0, v1, v2, v3, v4,
         0, v1, v2, v1-v2, v4);
   }
   printf("subN, n=4 PASS\n");
}

void mulN_tests(void) {
   int i;
   
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
   int i;
   
   for (i = 0; i < LOOPS; i++) {
      char v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test1("divN", divN,
         0, v1, v2, v3, v4,
         0,  0, v2, v1/v2, v1%v2);
   }
   printf("divN, n=1 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      int v1 = rand(), v2 = rand(), v3 = 0, v4 = 0;
      test2("divN", divN,
         0, v1, v2, v3, v4,
         0,  0, v2, v1/v2, v1%v2);
   }
   printf("divN, n=2 PASS\n");

   for (i = 0; i < LOOPS; i++) {
      long v1 = lrand(), v2 = lrand(), v3 = 0, v4 = 0;
      test4("divN", divN,
         0, v1, v2, v3, v4,
         0,  0, v2, v1/v2, v1%v2);
   }
   printf("divN, n=4 PASS\n");
}

int main(void) {
   printf("hello, world\n");

   addN_tests();
   subN_tests();
   mulN_tests();
   divN_tests();

   return 0;
}
