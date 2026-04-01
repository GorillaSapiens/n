#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mos6502/mos6502.h"

uint16_t trace_ops = 0;
#define TRACE_OP_READS    (1 << 0)
#define TRACE_OP_WRITES   (1 << 1)
#define TRACE_OP_REGS     (1 << 2)
#define TRACE_OP_DISASM   (1 << 3)
#define TRACE_OP_CYCLES   (1 << 4)
#define TRACE_OP_DISPATCH (1 << 5)

uint64_t counter = 0;
uint8_t mem[65536];

static uint8_t ihex_checksum(const uint8_t *bytes, size_t n) {
   uint32_t sum = 0;

   for (size_t i = 0; i < n; i++) {
      sum += bytes[i];
   }

   return static_cast<uint8_t>((-static_cast<int32_t>(sum)) & 0xFF);
}

static void emit_ihex_record(uint8_t count,
                             uint16_t addr,
                             uint8_t rectype,
                             const uint8_t *data) {
   uint8_t hdr[4] = {
      count,
      static_cast<uint8_t>((addr >> 8) & 0xFF),
      static_cast<uint8_t>(addr & 0xFF),
      rectype,
   };
   uint8_t csum = ihex_checksum(hdr, sizeof(hdr));

   printf(":%02X%04X%02X", count, addr, rectype);
   for (uint8_t i = 0; i < count; i++) {
      printf("%02X", data[i]);
      csum = static_cast<uint8_t>(csum - data[i]);
   }
   printf("%02X\n", csum);
}

void dump_mem_as_intel_hex(void) {
   printf("---8<--- BEGIN MEMORY DUMP ---8<---\n");

   for (uint32_t addr = 0; addr < 65536; addr += 16) {
      emit_ihex_record(16, static_cast<uint16_t>(addr), 0x00, mem + addr);
   }

   emit_ihex_record(0, 0, 0x01, nullptr);

   printf("---8<---  END MEMORY DUMP  ---8<---\n");
}

static uint8_t hex_byte(const std::string &s, size_t pos) {
   return static_cast<uint8_t>(std::stoul(s.substr(pos, 2), nullptr, 16));
}

void load_intel_hex(const char *filename) {
   std::ifstream in(filename);
   if (!in)
      throw std::runtime_error("Failed to open Intel HEX file");

   std::string line;
   uint32_t base = 0;

   while (std::getline(in, line))
   {
      if (line.empty())
         continue;
      if (line[0] != ':')
         throw std::runtime_error("Invalid Intel HEX record");

      if (line.size() < 11)
         throw std::runtime_error("Record too short");

      uint8_t count = hex_byte(line, 1);
      uint16_t addr = (static_cast<uint16_t>(hex_byte(line, 3)) << 8) | hex_byte(line, 5);
      uint8_t type = hex_byte(line, 7);

      if (line.size() != (std::size_t) (11 + count * 2))
         throw std::runtime_error("Record length mismatch");

      uint8_t sum = count + (addr >> 8) + (addr & 0xFF) + type;
      for (uint8_t i = 0; i < count; i++)
         sum += hex_byte(line, 9 + i * 2);
      sum += hex_byte(line, 9 + count * 2);

      if (sum != 0)
         throw std::runtime_error("Checksum error");

      if (type == 0x00)   // data
      {
         uint32_t full_addr = base + addr;
         for (uint8_t i = 0; i < count; i++)
         {
            if (full_addr + i >= 65536)
               throw std::runtime_error("Address out of range for 64K memory");
            mem[full_addr + i] = hex_byte(line, 9 + i * 2);
         }
      }
      else if (type == 0x01)   // EOF
      {
         break;
      }
      else if (type == 0x02)   // extended segment address
      {
         if (count != 2)
            throw std::runtime_error("Bad extended segment address record");
         base = ((static_cast<uint32_t>(hex_byte(line, 9)) << 8) |
                 static_cast<uint32_t>(hex_byte(line, 11))) << 4;
      }
      else if (type == 0x04)   // extended linear address
      {
         if (count != 2)
            throw std::runtime_error("Bad extended linear address record");
         base = ((static_cast<uint32_t>(hex_byte(line, 9)) << 8) |
                 static_cast<uint32_t>(hex_byte(line, 11))) << 16;
      }
   }
}

void write_cb(uint16_t addr, uint8_t val) {
   if (trace_ops & TRACE_OP_WRITES) {
      printf("write $%02x -> $%04x\n", val, addr);
   }
   mem[addr] = val;
}

uint8_t read_cb(uint16_t addr) {
   if (trace_ops & TRACE_OP_READS) {
      printf("read $%04x -> $%02x\n", addr, mem[addr]);
   }
   return mem[addr];
}

void clock_cb(mos6502* cpu) {
   (void) cpu; // unused parameter
   if (trace_ops & TRACE_OP_CYCLES) {
      printf("cycle %" PRId64 "\n", counter);
   }
}

void dispatch(uint8_t op, uint16_t arg) {
   if (trace_ops & TRACE_OP_DISPATCH) {
      printf("dispatch %02x %04X\n", op, arg);
   }
   switch(op) {
      case 0:
         printf("%s", mem+arg);
         fflush(stdout);
         break;
      case 0xfd:
         trace_ops = arg;
         break;
      case 0xfe:
         dump_mem_as_intel_hex();
         break;
      case 0xff:
         exit(arg);
         break;
      default:
         fprintf(stderr, "unknown dispatch op %02x\n", op);
         break;
   }
}

void trace_regs(mos6502 *cpu) {
   uint8_t p = cpu->GetP();
   printf("A:$%02x X:$%02x Y:$%02x P:$%02x(%c%c%c%c%c%c%c%c) SP:$%02x PC:$%04x\n",
      cpu->GetA(),
      cpu->GetX(),
      cpu->GetY(),
      p,
      (p & 0x80) ? 'N' : 'n',
      (p & 0x40) ? 'V' : 'v',
      (p & 0x20) ? '-' : '?',
      (p & 0x10) ? 'B' : 'b',
      (p & 0x08) ? 'D' : 'd',
      (p & 0x04) ? 'I' : 'i',
      (p & 0x02) ? 'Z' : 'z',
      (p & 0x01) ? 'C' : 'c',
      cpu->GetS(),
      cpu->GetPC());
}

void trace_disasm(mos6502 *cpu) {
   uint16_t pc = cpu->GetPC();
   const char *code = cpu->GetCode(mem[pc]);
   const char *addr = cpu->GetAddr(mem[pc]);

   switch (addr[0]) {
      case 'A':
         switch(addr[2]) {
            case 'I':
               // ABI
               printf("ASM: $%04x: %s.i ($%04x)    ; %02x %02x %02x\n", pc, code, mem[pc+1] | (mem[pc+2] << 8), mem[pc], mem[pc+1], mem[pc+2]);
               break;
            case 'S':
               // ABS
               printf("ASM: $%04x: %s.a $%04x      ; %02x %02x %02x\n", pc, code, mem[pc+1] | (mem[pc+2] << 8), mem[pc], mem[pc+1], mem[pc+2]);
               break;
            case 'X':
               // ABX
               printf("ASM: $%04x: %s.ax $%04x,X   ; %02x %02x %02x\n", pc, code, mem[pc+1] | (mem[pc+2] << 8), mem[pc], mem[pc+1], mem[pc+2]);
               break;
            case 'Y':
               // ABY
               printf("ASM: $%04x: %s.ay $%04x,Y   ; %02x %02x %02x\n", pc, code, mem[pc+1] | (mem[pc+2] << 8), mem[pc], mem[pc+1], mem[pc+2]);
               break;
            case 'C':
               // ACC
               printf("ASM: $%04x: %s A            ; %02x\n", pc, code, mem[pc]);
               break;
         } 
         break;
      case  'I':
         switch(addr[2]) {
            case 'M':
               // IMM
               printf("ASM: $%04x: %s #$%02x       ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
            case 'P':
               // IMP
               printf("ASM: $%04x: %s              ; %02x\n", pc, code, mem[pc]);
               break;
            case 'X':
               // INX
               printf("ASM: $%04x: %s.ix ($%02x,X) ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
            case 'Y':
               // INY
               printf("ASM: $%04x: %s.iy ($%02x),Y ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
         }
         break;
      case 'R':
         // REL
               printf("ASM: $%04x: %s $%02x        ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
         break;
      case 'Z':
         switch(addr[2]) {
            case 'R':
               // ZER
               printf("ASM: $%04x: %s.z $%02x      ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
            case 'X':
               // ZEX
               printf("ASM: $%04x: %s.zx $%02x,X   ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
            case 'Y':
               // ZEY
               printf("ASM: $%04x: %s.zy $%02x,Y   ; %02x %02x\n", pc, code, mem[pc+1], mem[pc], mem[pc+1]);
               break;
         }
         break;
   }
}

int main (int argc, char **argv) {
   if (argc != 2 && argc != 3) {
      fprintf(stderr, "Usage: %s <hex> [<trace>]\n", argv[0]);
      exit(-1);
   }

   if (argc == 3) {
      trace_ops = strtoul(argv[2], NULL, 0);
   }

   memset(mem, 0xFF, 65536);

   load_intel_hex(argv[1]);

   mos6502 *cpu = new mos6502(read_cb, write_cb, clock_cb);

   cpu->Reset();

   while (1) {
      if (trace_ops & TRACE_OP_REGS) {
         trace_regs(cpu);
      }
      if (trace_ops & TRACE_OP_DISASM) {
         trace_disasm(cpu);
      }
      cpu->Run(1, counter, mos6502::INST_COUNT);
      if (cpu->GetPC() == 0xFFFF) {

         uint8_t op = cpu->GetA();
         uint16_t arg = ((uint16_t)cpu->GetY()) << 8 | cpu->GetX();
         dispatch(op, arg);

         uint8_t tmp = mem[0xFFFF]; // remember original value
         mem[0xFFFF] = 0x60; // insert an RTS there

         cpu->Run(1, counter, mos6502::INST_COUNT);

         mem[0xFFFF] = tmp; // restore original value
      }
   }

   return 0;
}
