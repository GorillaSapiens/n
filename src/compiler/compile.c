#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "ast.h"
#include "compile.h"
#include "messages.h"
#include "set.h"

void emit(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);
}

Set *types = NULL;
Set *globals = NULL;

// check type_decl for existence of $size and $endian
static void compile_type_decl(ASTNode *node) {

   if (!types) {
      types = new_set();
   }

//   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
//   parse_dump_node(node);

   const char *key = node->children[0]->strval;
   set_add(types, key, node);

   //debug("%s:%s", __FUNCTION__, node->children[0]->strval);
   bool haveSize = false;
   int size = -1;
   bool haveEndian = false;
   // we need to guarantee a "size" and "endian"
   if (strcmp(node->children[1]->name, "empty")) {
      for (ASTNode *list = node->children[1];
            list != NULL;
            list = list->children[1]) {
         //debug("%s:\t%s", __FUNCTION__, list->children[0]->strval);

         // check for $size, must be nonnegative
         if (!strncmp(list->children[0]->strval, "$size:", 6)) {
            if (haveSize) {
               error("[%s:%d.%d] type_decl '%s' has multiple '$size:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(list->children[0]->strval, ':');
            p++;
            size = atoi(p);
            if (size < 0 || (size == 0 && strcmp(p, "0"))) {
               error("[%s:%d.%d] type_decl '%s' unrecognized '$size:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }
            haveSize = true;
         }

         // check for $endian, must be "big" or "little"
         if (!strncmp(list->children[0]->strval, "$endian:", 8)) {
            if (haveEndian) {
               error("[%s:%d.%d] type_decl '%s' has multiple '$endian:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(list->children[0]->strval, ':');
            p++;
            if (strcmp(p, "big") && strcmp(p, "little")) {
               error("[%s:%d.%d] type_decl '%s' unrecognized '$endian:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }

            haveEndian = true;
         }
      }
   }
   if (!haveSize) {
      error("[%s:%d.%d] type_decl '%s' missing '$size:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }
   if (!haveEndian && size > 1) {
      error("[%s:%d.%d] type_decl '%s' missing '$endian:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }
}

static bool has_modifier(ASTNode *node, const char *modifier) {
   if (node && node->kind != AST_EMPTY) {
      while (node) {
         if (!strcmp(modifier, node->children[0]->strval)) {
            return true;
         }
         node = node->children[1];
      }
   }
   return false;
}

static int get_size(const char *type) {
   const ASTNode *node = set_get(types, type);
   for (const ASTNode *list = node->children[1];
         list != NULL;
         list = list->children[1]) {
      if (!strncmp(list->children[0]->strval, "$size:", 6)) {
         return atoi(list->children[0]->strval + 6);
      }
   }
   error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   return -1; // unreachable
}

static void compile_decl_stmt(ASTNode *node) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   ASTNode *modifiers    = node->children[0];
   const char *type      = node->children[1]->strval;
   const char *name      = node->children[2]->strval;
   ASTNode *dimension    = node->children[3];
   const char *location  = node->children[4]->strval;
   ASTNode *expression   = node->children[5];

   if (!globals) {
      globals = new_set();
   }

   const ASTNode *value = set_get(globals, name);
   if (value != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
         node->file, node->line, node->column,
         name,
         value->file, value->line, value->column);
   }
   set_add(globals, name, node);

   bool is_extern = has_modifier(modifiers, "extern");
   bool is_const  = has_modifier(modifiers, "const");
   bool is_static = has_modifier(modifiers, "static");

   printf("=");
   if (is_extern) {
      printf("extern ");
   }
   if (is_const) {
      printf("const ");
   }
   if (is_static) {
      printf("static ");
   }
   printf("%s %s %p @%s %p\n", type, name, dimension, location, expression);
   
   int size = get_size(type);

   if (is_extern) {
      if (is_static) {
         error("[%s:%d.%d] 'extern' and 'static' don't mix",
            node->file, node->line, node->column);
      }

      emit(".import %s\n", name);
   }
   else {
      if (!is_static) {
         emit(".export %s\n", name);
      }
      if (expression == NULL) {
         if (is_const) {
            error("[%s:%d.%d] 'const' missinf initializer",
               node->file, node->line, node->column);
         }
         emit(".section \"BSS\"\n");
         emit("%s:\n", name);
         // TODO FIX multiply "size" by "dimension" if necessary
         emit(".res %d\n", size);
      }
      else {
         if (is_const) {
            emit(".section \"RODATA\"\n");
         }
         else {
            emit(".section \"DATA\"\n");
         }
         emit("%s:\n", name);
         // TODO FIX multiply "size" by "dimension" if necessary
         emit(".res %d\n", size); // TODO FIX change to initializer
      }
   }

   return;
}

static void compile_function_decl(ASTNode *node) {
   //debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   //parse_dump_node(node);
   return;
}

static void compile(ASTNode *node) {
   if (!node) {
      return;
   }

   if (!strcmp(node->name, "program")) {
      compile(node->children[0]);
      compile(node->children[1]);
   }
   else if (!strcmp(node->name, "type_decl")) {
      compile_type_decl(node);
   }
   else if (!strcmp(node->name, "decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "static_decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "const_decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "function_decl")) {
      compile_function_decl(node);
   }
   else {
      error("[%s:%d.%d] unrecognized AST node '%s'",
         node->file, node->line, node->column,
         node->name);
   }
}

void do_compile(void) {
   compile(root);
}
