#ifndef SYMBOLS_H
#define SYMBOLS_H

void init_symbols();
void define_label(const char* name, int address);
int get_label_address(const char* name);
void emit_byte(unsigned char byte);
void emit_word(unsigned short word);
void emit_string(const char* str);
void dump_output();
extern int current_address;

#endif
