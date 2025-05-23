#ifndef OPCODES_H
#define OPCODES_H

struct opcode_entry {
    const char* mnemonic;
    const char* mode;
    unsigned char code;
};

unsigned char get_opcode(const char* mnemonic, const char* mode);
const struct opcode_entry* get_opcode_info(unsigned char code);
int is_opcode(const char* name);  // Added for lexer use

#endif
