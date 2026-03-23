#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mos6502/mos6502.h"

uint64_t counter = 0;
uint8_t mem[65536];

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
   mem[addr] = val;
}

uint8_t read_cb(uint16_t addr) {
   return mem[addr];
}

void clock_cb(mos6502* cpu) {
}

int main (int argc, char **argv) {
   if (argc != 2) {
      fprintf(stderr, "Usage: %s <hex>\n", argv[0]);
      exit(-1);
   }

   memset(mem, 0xFF, 65536);

   load_intel_hex(argv[1]);

   mos6502 *cpu = new mos6502(read_cb, write_cb, clock_cb);

   cpu->Reset();

   while (1) {
      cpu->Run(1, counter, mos6502::INST_COUNT);
      printf("%04x\n", cpu->GetPC());
      if (cpu->GetPC() == 0xFFFF) {

         // do stuff here!
         printf("peep!\n");

         uint8_t tmp = mem[0xFFFF]; // remember original value
         mem[0xFFFF] = 0x60; // insert an RTS there

         cpu->Run(1, counter, mos6502::INST_COUNT);

         mem[0xFFFF] = tmp; // restore original value
      }
   }

   return 0;
}
