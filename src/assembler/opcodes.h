#ifndef OPCODES_H
#define OPCODES_H

struct opcode_entry {
    const char* mnemonic;
    const char* mode;
    unsigned char code;
};

// Returns the opcode value for a given mnemonic and addressing mode
// Falls back to 0xEA (NOP) if the combination is unknown
unsigned char get_opcode(const char* mnemonic, const char* mode);
const struct opcode_entry* get_opcode_info(unsigned char code);

#endif
