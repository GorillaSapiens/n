#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "directive.h"
#include "util.h"

expr_list_node_t *expr_list_node_make(expr_t *expr)
{
   expr_list_node_t *node;

   node = (expr_list_node_t *)calloc(1, sizeof(*node));
   if (!node) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   node->expr = expr;
   return node;
}

expr_list_node_t *expr_list_append(expr_list_node_t *list, expr_t *expr)
{
   expr_list_node_t *node;
   expr_list_node_t *tail;

   node = expr_list_node_make(expr);

   if (!list)
      return node;

   tail = list;
   while (tail->next)
      tail = tail->next;

   tail->next = node;
   return list;
}

static directive_info_t *directive_alloc(char *name, directive_arg_kind_t kind)
{
   directive_info_t *dir;

   dir = (directive_info_t *)calloc(1, sizeof(*dir));
   if (!dir) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   dir->name = xstrdup(name);
   dir->kind = kind;
   return dir;
}

directive_info_t *directive_make_empty(char *name)
{
   return directive_alloc(name, DIRARG_NONE);
}

directive_info_t *directive_make_exprs(char *name, expr_list_node_t *exprs)
{
   directive_info_t *dir;

   dir = directive_alloc(name, DIRARG_EXPR_LIST);
   dir->exprs = exprs;
   return dir;
}

directive_info_t *directive_make_string(char *name, char *string)
{
   directive_info_t *dir;

   dir = directive_alloc(name, DIRARG_STRING);
   dir->string = xstrdup(string);
   return dir;
}

directive_info_t *directive_make_string_exprs(char *name, char *string, expr_list_node_t *exprs)
{
   directive_info_t *dir;

   dir = directive_alloc(name, DIRARG_STRING_AND_EXPR_LIST);
   dir->string = xstrdup(string);
   dir->exprs = exprs;
   return dir;
}

static void expr_list_free(expr_list_node_t *list)
{
   expr_list_node_t *next;

   while (list) {
      next = list->next;
      expr_free(list->expr);
      free(list);
      list = next;
   }
}

void directive_free(directive_info_t *dir)
{
   if (!dir)
      return;

   free(dir->name);
   free(dir->string);
   expr_list_free(dir->exprs);
   free(dir);
}

void directive_print(const directive_info_t *dir)
{
   const expr_list_node_t *node;
   int first;

   if (!dir)
      return;

   printf("directive %s", dir->name ? dir->name : "<null>");

   if (dir->string)
      printf(" string=%s", dir->string);

   if (dir->exprs) {
      printf(" exprs=");
      node = dir->exprs;
      first = 1;
      while (node) {
         if (!first)
            printf(", ");
         expr_print(node->expr);
         first = 0;
         node = node->next;
      }
   }

   printf("\n");
}
