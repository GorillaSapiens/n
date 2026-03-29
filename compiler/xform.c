#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ast.h"
#include "lextern.h"
#include "memname.h"
#include "messages.h"
#include "pair.h"
#include "typename.h"
#include "xform.h"

static Pair *xforms = NULL;

int register_xform(const char *name, ASTNode *node) {
   if (!xforms) {
      xforms = pair_create();
   }

   if (memname_exists(name)) {
      ASTNode *previous = get_memname_node(name);
      error ("xform at %s:%d.%d cannot be the same as existing memname at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (typename_exists(name)) {
      ASTNode *previous = get_typename_node(name);
      error ("xform at %s:%d.%d cannot be the same as existing typename at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (pair_exists(xforms, name)) {
      ASTNode *previous = get_xform_node(name);
      error ("xform at %s:%d.%d already exists at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   pair_insert(xforms, name, node);
   return 0;
}

bool xform_exists(const char *name) {
   if (!xforms) {
      xforms = pair_create();
   }

   return pair_exists(xforms, name);
}

static void str_append(char **sp, int *lp, unsigned char byte) {
   *sp = (char *) realloc(*sp, *lp + 2);
   (*sp)[*lp] = byte;
   *lp = *lp + 1;
   (*sp)[*lp] = 0;
}

static ASTNode *context = NULL;
static ASTNode *working = NULL;

static void str_append_helper(char **sp, int *lp, const char *match) {
   for (int i = 0; i < context->count; i++) {
      ASTNode *item = context->children[i];
      if (!strcmp(match, item->children[0]->strval)) {
         for (int j = 1; j < item->count; j++) {
            str_append(sp, lp, strtol(item->children[j]->strval, NULL, 0));
         }
         return;
      }
   }
   if (match[0] == '\'' && match[1] == '\\') {
      if (match[2] == 'u') {
         warning("no xform translation for %s, using 0xFF at %s:%d.%d",
               match, working->file, working->line, working->column);
         str_append(sp, lp, 0xFF);
      }
      else {
         warning("no xform translation for %s, using 0x%02X%s%c%s at %s:%d.%d",
               match,
               match[2],
               (match[2] >= ' ' && match[2] <= '~') ? "(" : "",
               match[2],
               (match[2] >= ' ' && match[2] <= '~') ? ")" : "",
               working->file, working->line, working->column);
         str_append(sp, lp, match[2]);
      }
   }
}

static void str_append_codepoint(char **sp, int *lp, int codepoint) {
   char buf[16];
   sprintf(buf, "'\\u%04x'", codepoint);
   str_append_helper(sp, lp, buf);
}

static void str_append_utf8(char **sp, int *lp, int codepoint) {
   char buf[16];

   if (codepoint <= 0x7F) {
      buf[0] = codepoint;
      buf[1] = '\0';
   } else if (codepoint <= 0x7FF) {
      buf[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
      buf[1] = 0x80 | (codepoint & 0x3F);
      buf[2] = '\0';
   } else if (codepoint <= 0xFFFF) {
      buf[0] = 0xE0 | ((codepoint >> 12) & 0x0F);
      buf[1] = 0x80 | ((codepoint >> 6) & 0x3F);
      buf[2] = 0x80 | (codepoint & 0x3F);
      buf[3] = '\0';
   } else if (codepoint <= 0x10FFFF) {
      buf[0] = 0xF0 | ((codepoint >> 18) & 0x07);
      buf[1] = 0x80 | ((codepoint >> 12) & 0x3F);
      buf[2] = 0x80 | ((codepoint >> 6) & 0x3F);
      buf[3] = 0x80 | (codepoint & 0x3F);
      buf[4] = '\0';
   } else {
      buf[0] = '\0'; // invalid code point
   }

   for (char *p = buf; *p; p++) {
      str_append(sp, lp, *p);
   }
}

static void str_append_esc(char **sp, int *lp, char c) {
   char buf[16];
   sprintf(buf, "'\\%c'", c);
   str_append_helper(sp, lp, buf);
}

static int decode_utf8(const char *s, int *codepoint) {
    *codepoint = 0;
    const unsigned char *us = (const unsigned char *)s;

    if ((us[0] & 0x80) == 0) {
        // 1-byte ASCII
        *codepoint = us[0];
        return 1;
    } else if ((us[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((us[1] & 0xC0) != 0x80) return 0;
        *codepoint = ((us[0] & 0x1F) << 6) | (us[1] & 0x3F);
        return 2;
    } else if ((us[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((us[1] & 0xC0) != 0x80 || (us[2] & 0xC0) != 0x80) return 0;
        *codepoint = ((us[0] & 0x0F) << 12) |
                ((us[1] & 0x3F) << 6) |
                (us[2] & 0x3F);
        return 3;
    } else if ((us[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if ((us[1] & 0xC0) != 0x80 || (us[2] & 0xC0) != 0x80 || (us[3] & 0xC0) != 0x80) return 0;
        *codepoint = ((us[0] & 0x07) << 18) |
                ((us[1] & 0x3F) << 12) |
                ((us[2] & 0x3F) << 6) |
                (us[3] & 0x3F);
        return 4;
    }

    // Invalid or overlong
    return 0;
}

static int fromhex(int len, const char *p) {
   int ret = 0;
   const char *op = p;
   for (int i = 0; i < len; i++) {
      ret <<= 4;
      if (*p >= '0' && *p <= '9') {
         ret |= (*p - '0');
      }
      else if (*p >= 'A' && *p <= 'F') {
         ret |= (*p - 'A' + 10);
      }
      else if (*p >= 'a' && *p <= 'f') {
         ret |= (*p - 'a' + 10);
      }
      else {
         warning("malformed hex '%s'\n", op);
      }
      p++;
   }
   return ret;
}

ASTNode *do_xform(ASTNode *node, const char *name) {
   const char *s = node->strval;
   char *ret = NULL;
   int retlen = 0;
   int codepoint;

   working = node;

   if (!name) {
      name = "";
   }

   context = pair_get(xforms, name);
   if (!context) {
      warning("could not find context '%s'", name ? name : "(null)");
      return node;
   }

   while (*s) {
      if (*s == '\\') {
         if (s[1] == 'x') {
            // \x?? is raw no matter what
            unsigned char byte = fromhex(2, s+2);
            str_append(&ret, &retlen, byte);
            s += 4;
         }
         else if (s[1] == 'u') {
            codepoint = fromhex(4, s+2);
            s += 6;
            if (name[0]) {
               // named xform, \u???? is a lookup
               str_append_codepoint(&ret, &retlen, codepoint);
            }
            else {
               // unnamed xform, \u???? is utf8
               str_append_utf8(&ret, &retlen, codepoint);
            }
         }
         else {
            // \? an escaped character
            str_append_esc(&ret, &retlen, s[1]);
            s += 2;
         }
      }
      else {
         if (name[0]) {
            // named xform, utf8 is a lookup
            int skip = decode_utf8(s, &codepoint);
            if (skip == 0) {
               // TODO FIX give a bit more information
               error("invalid utf8 found");
               exit(-1);
            }
            s += skip;
            str_append_codepoint(&ret, &retlen, codepoint);
         }
         else {
            // unnamed xform, raw characters
            str_append(&ret, &retlen, *s++);
         }
      }
   }

   node->strval = ret;
   return node;
}

ASTNode *get_xform_node(const char *name) {
   if (!xforms) {
      xforms = pair_create();
   }

   return pair_get(xforms, name);
}
