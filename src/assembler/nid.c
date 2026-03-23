#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 1024

static int hex_nibble(int c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
   if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
   return -1;
}

static int hex_byte(const char *s)
{
   int hi = hex_nibble((unsigned char)s[0]);
   int lo = hex_nibble((unsigned char)s[1]);

   if (hi < 0 || lo < 0)
      return -1;

   return (hi << 4) | lo;
}

static void trim_newline(char *s)
{
   size_t len = strlen(s);

   while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
      s[len - 1] = '\0';
      len--;
   }
}

static int parse_ihex_line(const char *line)
{
   size_t len;
   int byte_count;
   int address_hi, address_lo;
   int record_type;
   int checksum;
   int i;

   if (line[0] != ':') {
      fprintf(stderr, "Invalid line: missing ':'\n");
      return 0;
   }

   len = strlen(line);

   if ((len - 1) % 2 != 0) {
      fprintf(stderr, "Invalid line: odd number of hex digits\n");
      return 0;
   }

   if (len < 11) {
      fprintf(stderr, "Invalid line: too short\n");
      return 0;
   }

   byte_count = hex_byte(line + 1);
   address_hi = hex_byte(line + 3);
   address_lo = hex_byte(line + 5);
   record_type = hex_byte(line + 7);

   if (byte_count < 0 || address_hi < 0 || address_lo < 0 || record_type < 0) {
      fprintf(stderr, "Invalid line: bad header hex\n");
      return 0;
   }

   if ((int)len != 1 + 2 + 4 + 2 + (byte_count * 2) + 2) {
      fprintf(stderr, "Invalid line: length does not match byte count\n");
      return 0;
   }

   checksum = hex_byte(line + 9 + byte_count * 2);
   if (checksum < 0) {
      fprintf(stderr, "Invalid line: bad checksum hex\n");
      return 0;
   }

   printf(": %02X %02X%02X %02X",
      byte_count,
      address_hi,
      address_lo,
      record_type);

   for (i = 0; i < byte_count; i++) {
      int data = hex_byte(line + 9 + i * 2);

      if (data < 0) {
         fprintf(stderr, "\nInvalid line: bad data hex\n");
         return 0;
      }

      printf(" %02X", data);
   }

   printf(" %02X\n", checksum);

   return 1;
}

int main(void)
{
   char line[MAX_LINE];

   while (fgets(line, sizeof(line), stdin) != NULL) {
      trim_newline(line);

      if (line[0] == '\0')
         continue;

      parse_ihex_line(line);
   }

   return 0;
}
