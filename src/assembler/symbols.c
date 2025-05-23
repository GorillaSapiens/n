#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbols.h"

#define MAX_SYMBOLS 1024
#define OUTPUT_SIZE 65536

static struct { char* name; int address; } symbols[MAX_SYMBOLS];
static int symbol_count = 0;
static unsigned char output[OUTPUT_SIZE];
int current_address = 0;

void init_symbols() {
    symbol_count = 0;
    current_address = 0;
    memset(output, 0xFF, sizeof(output));
}

void define_label(const char* name, int address) {
    symbols[symbol_count].name = strdup(name);
    symbols[symbol_count].address = address;
    symbol_count++;
}

int get_label_address(const char* name) {
    for (int i = 0; i < symbol_count; i++) {
        if (strcmp(symbols[i].name, name) == 0)
            return symbols[i].address;
    }
    fprintf(stderr, "Undefined label: %s\n", name);
    exit(1);
}

void emit_byte(unsigned char byte) {
    output[current_address++] = byte;
}

void emit_word(unsigned short word) {
    emit_byte(word & 0xFF);
    emit_byte((word >> 8) & 0xFF);
}

void emit_string(const char* str) {
    while (*str) emit_byte(*str++);
}

void dump_output() {
    FILE* f = fopen("output.bin", "wb");
    if (!f) { perror("fopen"); exit(1); }
    fwrite(output, 1, current_address, f);
    fclose(f);
    for (int i = 0; i < current_address; i++) {
        printf("%02X ", output[i]);
        if ((i+1) % 16 == 0) printf("\n");
    }
    if (current_address % 16) printf("\n");
}
