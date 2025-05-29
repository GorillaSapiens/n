#include <stdio.h>

#include "nlib.h"

void test2(void(*fn)(void)) {
   int val1 = 0x1234;
   int val2 = 0x5678;
   int val3 = 0x9ABC;
   int val4 = 0xDEF0;

   nl_size = 2;
   nl_shift = 0;
   nl_ptr1 = &val1;
   nl_ptr2 = &val2;
   nl_ptr3 = &val3;
   nl_ptr4 = &val4;

   printf("before: size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n", nl_size, nl_shift,
      val1, val2, val3, val4);
   fn();
   printf("after : size=%02x shift=%02x v1:%04x v2:%04x v3:%04x v4:%04x\n", nl_size, nl_shift,
      val1, val2, val3, val4);
}

int main(void) {
   printf("hello, world\n");

   test2(addN);

   return 0;
}
