#ifndef OPCODE_H
#define OPCODE_H

typedef enum emit_mode {
   EM_IMPLIED = 0,
   EM_ACCUMULATOR,
   EM_IMMEDIATE,
   EM_ZP,
   EM_ZPX,
   EM_ZPY,
   EM_ABS,
   EM_ABSX,
   EM_ABSY,
   EM_IND,
   EM_INDX,
   EM_INDY,
   EM_REL
} emit_mode_t;

int opcode_lookup(const char *mnemonic, emit_mode_t mode, unsigned char *opcode_out);
int emit_mode_size(emit_mode_t mode);

#endif
