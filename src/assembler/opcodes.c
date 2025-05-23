#include <string.h>
#include "opcodes.h"

static struct opcode_entry table[] = {
#include "opcode_data_by_value.h"
};

unsigned char get_opcode(const char* mnemonic, const char* mode) {
    for (int i = 0; i < 256; i++) {
        if (table[i].mnemonic &&
            strcmp(table[i].mnemonic, mnemonic) == 0 &&
            strcmp(table[i].mode, mode) == 0) {
            return table[i].code;
        }
    }
    return 0xEA; // NOP as fallback
}

const struct opcode_entry* get_opcode_info(unsigned char code) {
    for (int i = 0; i < 256; i++) {
        if (table[i].code == code) {
            return &table[i];
        }
    }
    return NULL;
}
