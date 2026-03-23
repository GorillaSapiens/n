#ifndef ADDR_MODE_H
#define ADDR_MODE_H

typedef enum addr_mode {
   AM_NONE = 0,
   AM_IMPLIED,
   AM_ACCUMULATOR,
   AM_IMMEDIATE,
   AM_ZP_OR_ABS,
   AM_ZPX_OR_ABSX,
   AM_ZPY_OR_ABSY,
   AM_INDIRECT,
   AM_INDEXED_INDIRECT,
   AM_INDIRECT_INDEXED,
   AM_RELATIVE
} addr_mode_t;

#endif
