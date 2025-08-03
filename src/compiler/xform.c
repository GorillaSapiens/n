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
      error ("xform conflicts with memname '%s' %s:%d.%d",
         name, current_filename, yylineno, yycolumn);
      return -1;
   }

   if (typename_exists(name)) {
      error ("xform conflicts with typename '%s' %s:%d.%d",
         name, current_filename, yylineno, yycolumn);
      return -1;
   }

   if (pair_exists(xforms, name)) {
      error ("duplicate xform '%s' %s:%d.%d",
         name, current_filename, yylineno, yycolumn);
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

static void strappend(char **sp, int *lp, unsigned char byte) {
   *sp = (char *) realloc(*sp, *lp + 2);
   (*sp)[*lp] = byte;
   *lp = *lp + 1;
   (*sp)[*lp] = 0;
}

static ASTNode *context = NULL;

static void strappend_helper(const char *match, char **sp, int *lp) {
   for (int i = 0; i < context->count; i++) {
      ASTNode *item = context->children[i];
      if (!strcasecmp(match, item->children[0]->strval)) {
         for (int j = 1; j < item->count; j++) {
            strappend(sp, lp, strtol(item->children[j]->strval, NULL, 0));
            return;
         }
      }
   }
   strappend(sp, lp, 0xFF);
}

static void strappend_uval(int uval, char **sp, int *lp) {
   char buf[16];
   sprintf(buf, "'\\u%04x'", uval);
   strappend_helper(buf, sp, lp);
}

static void strappend_esc(char c, char **sp, int *lp) {
   char buf[16];
   sprintf(buf, "'\\%c'", c);
   strappend_helper(buf, sp, lp);
}

static int decode_utf8(const char *s, int *uval) {
    *uval = 0;
    const unsigned char *us = (const unsigned char *)s;

    if ((us[0] & 0x80) == 0) {
        // 1-byte ASCII
        *uval = us[0];
        return 1;
    } else if ((us[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((us[1] & 0xC0) != 0x80) return 0;
        *uval = ((us[0] & 0x1F) << 6) | (us[1] & 0x3F);
        return 2;
    } else if ((us[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((us[1] & 0xC0) != 0x80 || (us[2] & 0xC0) != 0x80) return 0;
        *uval = ((us[0] & 0x0F) << 12) |
                ((us[1] & 0x3F) << 6) |
                (us[2] & 0x3F);
        return 3;
    } else if ((us[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if ((us[1] & 0xC0) != 0x80 || (us[2] & 0xC0) != 0x80 || (us[3] & 0xC0) != 0x80) return 0;
        *uval = ((us[0] & 0x07) << 18) |
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
   for (int i = 0; i < len; i++) {
      ret <<= 4;
      if (*p <= '9') {
         ret |= (*p - '0');
      }
      else if (*p <= 'F') {
         ret |= (*p - 'A' + 10);
      }
      else if (*p <= 'f') {
         ret |= (*p - 'a' + 10);
      }
      p++;
   }
   return ret;
}

const char *do_xform(const char *s, const char *name) {
   char *ret = NULL;
   int retlen = 0;
   int uval;

   if (!name) {
      name = "";
   }

   context = pair_get(xforms, name);
   if (!context) {
      warning("could not find context '%s'", name ? name : "(null)");
      return s;
   }

   while (*s) {
      if (*s == '\\') {
         if (s[1] == 'x') {
            unsigned char byte = fromhex(2, s+2);
            strappend(&ret, &retlen, byte);
            s += 4;
         }
         else if (s[1] == 'u') {
            uval = fromhex(4, s+2);
            strappend_uval(uval, &ret, &retlen);
            s += 6;
         }
         else {
            strappend_esc(s[1], &ret, &retlen);
            s += 2;
         }
      }
      else {
         int skip = decode_utf8(s, &uval);
         if (skip == 0) {
            // TODO FIX give a bit more information
            error("invalid utf8 found");
            exit(-1);
         }
         s += skip;
         strappend_uval(uval, &ret, &retlen);
      }
   }

   return ret;
}
