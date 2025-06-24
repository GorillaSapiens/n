#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"

ASTNode *root = NULL;

ASTNode *make_node(const char *name, ...) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = name;
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   va_list ap;
   va_start(ap, name);
   ASTNode *child;
   while ((child = va_arg(ap, ASTNode *)) != NULL) {
      if (ret->count < 16)
         ret->children[ret->count++] = child;
   }
   va_end(ap);
   return ret;
}

ASTNode *make_integer_leaf(const char *intval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "int";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_INTEGER;
   ret->strval = intval;
   return ret;
}

ASTNode *increment_integer_leaf(ASTNode *node) {
   unsigned long n = strtoul(node->strval, NULL, 0);
   n++;
   free((void *)node->strval);
   node->strval = (char *) malloc(24);
   sprintf((char *)node->strval, "%ld", n);
   return node;
}

ASTNode *make_string_leaf(const char *strval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "str";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_STRING;
   ret->strval = strval ? strdup(strval) : NULL;
   return ret;
}

ASTNode *make_identifier_leaf(const char *strval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "identifier";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_IDENTIFIER;
   ret->strval = strval ? strdup(strval) : NULL;
   return ret;
}

ASTNode *make_typename_leaf(const char *strval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "typename";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_TYPENAME;
   ret->strval = strval ? strdup(strval) : NULL;
   return ret;
}

ASTNode *make_float_leaf(const char *dval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "float";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_FLOAT;
   ret->strval = dval;
   return ret;
}

ASTNode *make_empty_leaf(void) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "empty";
   ret->file = strdup(current_filename);
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_EMPTY;
   return ret;
}

char *make_negative(const char *p) {
   char *q = (char *) malloc(sizeof(char) * strlen(p) + 2);
   sprintf(q, "-%s", p);
   free((void *)p);
   return q;
}

void dump_ast_flat(const ASTNode *node,
                   const char *prefix,
                   int is_last,
                   const char *parent_name) {
    if (!node) return;

    parent_name = NULL;

    // Print current node
    if (!parent_name ||
        strcmp(parent_name, node->name) ||
        !strcmp(node->name, "identifier")) {
       printf("%s%s%s", prefix,
             is_last ? "└── " : "├── ",
             node->name);

       switch (node->kind) {
          case AST_INTEGER:    printf(" %s", node->strval); break;
          case AST_FLOAT:      printf(" %s", node->strval); break;
          case AST_STRING:     printf(" \"%s\"", node->strval); break;
          case AST_IDENTIFIER: printf(" %s", node->strval); break;
          case AST_TYPENAME:   printf(" %s", node->strval); break;
          case AST_EMPTY:      printf(" <empty>"); break;
          default: break;
       }
       printf("\n");
    }

    // Determine if we can flatten this node's children
    int can_flatten = 0;
    if (node->count > 1 &&
        node->name && parent_name &&
        strcmp(node->name, parent_name) == 0) {
        can_flatten = 1;
    }

    // Build next prefix
    char new_prefix[4096];
    snprintf(new_prefix, sizeof(new_prefix), "%s%s",
             prefix, is_last ? "    " : "│   ");

    for (int i = 0; i < node->count; ++i) {
        if (can_flatten) {
            dump_ast_flat(node->children[i],
                          prefix, i == node->count - 1, node->name);
        } else {
            dump_ast_flat(node->children[i],
                          new_prefix, i == node->count - 1, node->name);
        }
    }
}

void parse_dump_node(ASTNode *node) {
   if (node) {
      dump_ast_flat(node, "", 1, NULL);
   }
}

void parse_dump(void) {
   parse_dump_node(root);
}
