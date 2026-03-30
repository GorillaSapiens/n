#include <string.h>
#include "opcode.h"

typedef struct opcode_entry {
   const char *mnemonic;
   emit_mode_t mode;
   unsigned char opcode;
} opcode_entry_t;

static int hex_nibble(int ch)
{
   if (ch >= '0' && ch <= '9')
      return ch - '0';
   if (ch >= 'A' && ch <= 'F')
      return 10 + (ch - 'A');
   if (ch >= 'a' && ch <= 'f')
      return 10 + (ch - 'a');
   return -1;
}

int opcode_parse_raw_byte(const char *mnemonic, unsigned char *opcode_out)
{
   int hi;
   int lo;

   if (!mnemonic || strlen(mnemonic) != 4)
      return 0;

   if (mnemonic[0] != 'O' && mnemonic[0] != 'o')
      return 0;
   if (mnemonic[1] != 'P' && mnemonic[1] != 'p')
      return 0;

   hi = hex_nibble((unsigned char)mnemonic[2]);
   lo = hex_nibble((unsigned char)mnemonic[3]);
   if (hi < 0 || lo < 0)
      return 0;

   *opcode_out = (unsigned char)((hi << 4) | lo);
   return 1;
}

int opcode_raw_is_conditional_branch(unsigned char opcode)
{
   return opcode == 0x10 || opcode == 0x30 || opcode == 0x50 || opcode == 0x70 ||
          opcode == 0x90 || opcode == 0xB0 || opcode == 0xD0 || opcode == 0xF0;
}

int opcode_raw_is_accumulator_shorthand(unsigned char opcode)
{
   return opcode == 0x0A || opcode == 0x2A || opcode == 0x4A || opcode == 0x6A;
}

static const opcode_entry_t g_opcodes[] = {
   { "ADC", EM_IMMEDIATE, 0x69 }, { "ADC", EM_ZP, 0x65 }, { "ADC", EM_ZPX, 0x75 }, { "ADC", EM_ABS, 0x6D }, { "ADC", EM_ABSX, 0x7D }, { "ADC", EM_ABSY, 0x79 }, { "ADC", EM_INDX, 0x61 }, { "ADC", EM_INDY, 0x71 },
   { "AND", EM_IMMEDIATE, 0x29 }, { "AND", EM_ZP, 0x25 }, { "AND", EM_ZPX, 0x35 }, { "AND", EM_ABS, 0x2D }, { "AND", EM_ABSX, 0x3D }, { "AND", EM_ABSY, 0x39 }, { "AND", EM_INDX, 0x21 }, { "AND", EM_INDY, 0x31 },
   { "ASL", EM_ACCUMULATOR, 0x0A }, { "ASL", EM_ZP, 0x06 }, { "ASL", EM_ZPX, 0x16 }, { "ASL", EM_ABS, 0x0E }, { "ASL", EM_ABSX, 0x1E },
   { "BCC", EM_REL, 0x90 }, { "BCS", EM_REL, 0xB0 }, { "BEQ", EM_REL, 0xF0 }, { "BMI", EM_REL, 0x30 }, { "BNE", EM_REL, 0xD0 }, { "BPL", EM_REL, 0x10 }, { "BVC", EM_REL, 0x50 }, { "BVS", EM_REL, 0x70 },
   { "BIT", EM_ZP, 0x24 }, { "BIT", EM_ABS, 0x2C },
   { "BRK", EM_IMPLIED, 0x00 },
   { "CLC", EM_IMPLIED, 0x18 }, { "CLD", EM_IMPLIED, 0xD8 }, { "CLI", EM_IMPLIED, 0x58 }, { "CLV", EM_IMPLIED, 0xB8 },
   { "CMP", EM_IMMEDIATE, 0xC9 }, { "CMP", EM_ZP, 0xC5 }, { "CMP", EM_ZPX, 0xD5 }, { "CMP", EM_ABS, 0xCD }, { "CMP", EM_ABSX, 0xDD }, { "CMP", EM_ABSY, 0xD9 }, { "CMP", EM_INDX, 0xC1 }, { "CMP", EM_INDY, 0xD1 },
   { "CPX", EM_IMMEDIATE, 0xE0 }, { "CPX", EM_ZP, 0xE4 }, { "CPX", EM_ABS, 0xEC },
   { "CPY", EM_IMMEDIATE, 0xC0 }, { "CPY", EM_ZP, 0xC4 }, { "CPY", EM_ABS, 0xCC },
   { "DEC", EM_ZP, 0xC6 }, { "DEC", EM_ZPX, 0xD6 }, { "DEC", EM_ABS, 0xCE }, { "DEC", EM_ABSX, 0xDE },
   { "DEX", EM_IMPLIED, 0xCA }, { "DEY", EM_IMPLIED, 0x88 },
   { "EOR", EM_IMMEDIATE, 0x49 }, { "EOR", EM_ZP, 0x45 }, { "EOR", EM_ZPX, 0x55 }, { "EOR", EM_ABS, 0x4D }, { "EOR", EM_ABSX, 0x5D }, { "EOR", EM_ABSY, 0x59 }, { "EOR", EM_INDX, 0x41 }, { "EOR", EM_INDY, 0x51 },
   { "INC", EM_ZP, 0xE6 }, { "INC", EM_ZPX, 0xF6 }, { "INC", EM_ABS, 0xEE }, { "INC", EM_ABSX, 0xFE },
   { "INX", EM_IMPLIED, 0xE8 }, { "INY", EM_IMPLIED, 0xC8 },
   { "JMP", EM_ABS, 0x4C }, { "JMP", EM_IND, 0x6C },
   { "JSR", EM_ABS, 0x20 },
   { "LDA", EM_IMMEDIATE, 0xA9 }, { "LDA", EM_ZP, 0xA5 }, { "LDA", EM_ZPX, 0xB5 }, { "LDA", EM_ABS, 0xAD }, { "LDA", EM_ABSX, 0xBD }, { "LDA", EM_ABSY, 0xB9 }, { "LDA", EM_INDX, 0xA1 }, { "LDA", EM_INDY, 0xB1 },
   { "LDX", EM_IMMEDIATE, 0xA2 }, { "LDX", EM_ZP, 0xA6 }, { "LDX", EM_ZPY, 0xB6 }, { "LDX", EM_ABS, 0xAE }, { "LDX", EM_ABSY, 0xBE },
   { "LDY", EM_IMMEDIATE, 0xA0 }, { "LDY", EM_ZP, 0xA4 }, { "LDY", EM_ZPX, 0xB4 }, { "LDY", EM_ABS, 0xAC }, { "LDY", EM_ABSX, 0xBC },
   { "LSR", EM_ACCUMULATOR, 0x4A }, { "LSR", EM_ZP, 0x46 }, { "LSR", EM_ZPX, 0x56 }, { "LSR", EM_ABS, 0x4E }, { "LSR", EM_ABSX, 0x5E },
   { "NOP", EM_IMPLIED, 0xEA },
   { "ORA", EM_IMMEDIATE, 0x09 }, { "ORA", EM_ZP, 0x05 }, { "ORA", EM_ZPX, 0x15 }, { "ORA", EM_ABS, 0x0D }, { "ORA", EM_ABSX, 0x1D }, { "ORA", EM_ABSY, 0x19 }, { "ORA", EM_INDX, 0x01 }, { "ORA", EM_INDY, 0x11 },
   { "PHA", EM_IMPLIED, 0x48 }, { "PHP", EM_IMPLIED, 0x08 }, { "PLA", EM_IMPLIED, 0x68 }, { "PLP", EM_IMPLIED, 0x28 },
   { "ROL", EM_ACCUMULATOR, 0x2A }, { "ROL", EM_ZP, 0x26 }, { "ROL", EM_ZPX, 0x36 }, { "ROL", EM_ABS, 0x2E }, { "ROL", EM_ABSX, 0x3E },
   { "ROR", EM_ACCUMULATOR, 0x6A }, { "ROR", EM_ZP, 0x66 }, { "ROR", EM_ZPX, 0x76 }, { "ROR", EM_ABS, 0x6E }, { "ROR", EM_ABSX, 0x7E },
   { "RTI", EM_IMPLIED, 0x40 }, { "RTS", EM_IMPLIED, 0x60 },
   { "SBC", EM_IMMEDIATE, 0xE9 }, { "SBC", EM_ZP, 0xE5 }, { "SBC", EM_ZPX, 0xF5 }, { "SBC", EM_ABS, 0xED }, { "SBC", EM_ABSX, 0xFD }, { "SBC", EM_ABSY, 0xF9 }, { "SBC", EM_INDX, 0xE1 }, { "SBC", EM_INDY, 0xF1 },
   { "SEC", EM_IMPLIED, 0x38 }, { "SED", EM_IMPLIED, 0xF8 }, { "SEI", EM_IMPLIED, 0x78 },
   { "STA", EM_ZP, 0x85 }, { "STA", EM_ZPX, 0x95 }, { "STA", EM_ABS, 0x8D }, { "STA", EM_ABSX, 0x9D }, { "STA", EM_ABSY, 0x99 }, { "STA", EM_INDX, 0x81 }, { "STA", EM_INDY, 0x91 },
   { "STX", EM_ZP, 0x86 }, { "STX", EM_ZPY, 0x96 }, { "STX", EM_ABS, 0x8E },
   { "STY", EM_ZP, 0x84 }, { "STY", EM_ZPX, 0x94 }, { "STY", EM_ABS, 0x8C },
   { "TAX", EM_IMPLIED, 0xAA }, { "TAY", EM_IMPLIED, 0xA8 }, { "TSX", EM_IMPLIED, 0xBA }, { "TXA", EM_IMPLIED, 0x8A }, { "TXS", EM_IMPLIED, 0x9A }, { "TYA", EM_IMPLIED, 0x98 },
   { NULL, 0, 0 }
};

int opcode_lookup(const char *mnemonic, emit_mode_t mode, unsigned char *opcode_out)
{
   const opcode_entry_t *e;

   if (opcode_parse_raw_byte(mnemonic, opcode_out) && mode != EM_REL_LONG)
      return 1;

   for (e = g_opcodes; e->mnemonic; ++e) {
      if (!strcmp(e->mnemonic, mnemonic) && e->mode == mode) {
         *opcode_out = e->opcode;
         return 1;
      }
   }

   return 0;
}

int opcode_is_conditional_branch(const char *mnemonic)
{
   return !strcmp(mnemonic, "BCC") || !strcmp(mnemonic, "BCS") ||
          !strcmp(mnemonic, "BEQ") || !strcmp(mnemonic, "BMI") ||
          !strcmp(mnemonic, "BNE") || !strcmp(mnemonic, "BPL") ||
          !strcmp(mnemonic, "BVC") || !strcmp(mnemonic, "BVS");
}

int opcode_invert_branch(const char *mnemonic, unsigned char *opcode_out)
{
   if (!strcmp(mnemonic, "BCC")) { *opcode_out = 0xB0; return 1; }
   if (!strcmp(mnemonic, "BCS")) { *opcode_out = 0x90; return 1; }
   if (!strcmp(mnemonic, "BEQ")) { *opcode_out = 0xD0; return 1; }
   if (!strcmp(mnemonic, "BMI")) { *opcode_out = 0x10; return 1; }
   if (!strcmp(mnemonic, "BNE")) { *opcode_out = 0xF0; return 1; }
   if (!strcmp(mnemonic, "BPL")) { *opcode_out = 0x30; return 1; }
   if (!strcmp(mnemonic, "BVC")) { *opcode_out = 0x70; return 1; }
   if (!strcmp(mnemonic, "BVS")) { *opcode_out = 0x50; return 1; }
   return 0;
}

int emit_mode_size(emit_mode_t mode)
{
   switch (mode) {
      case EM_IMPLIED:
      case EM_ACCUMULATOR:
         return 1;
      case EM_IMMEDIATE:
      case EM_ZP:
      case EM_ZPX:
      case EM_ZPY:
      case EM_INDX:
      case EM_INDY:
      case EM_REL:
         return 2;
      case EM_REL_LONG:
         return 5;
      case EM_ABS:
      case EM_ABSX:
      case EM_ABSY:
      case EM_IND:
         return 3;
   }

   return 0;
}
