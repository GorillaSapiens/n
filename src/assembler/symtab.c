#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"
#include "util.h"

void symtab_init(symtab_t *tab)
{
   tab->head = NULL;
}

void symtab_free(symtab_t *tab)
{
   symbol_t *sym;
   symbol_t *next;

   sym = tab->head;
   while (sym) {
      next = sym->next;
      free(sym->name);
      free(sym);
      sym = next;
   }

   tab->head = NULL;
}

symbol_t *symtab_find(symtab_t *tab, const char *name)
{
   symbol_t *sym;

   for (sym = tab->head; sym; sym = sym->next) {
      if (!strcmp(sym->name, name))
         return sym;
   }

   return NULL;
}

const symbol_t *symtab_find_const(const symtab_t *tab, const char *name)
{
   const symbol_t *sym;

   for (sym = tab->head; sym; sym = sym->next) {
      if (!strcmp(sym->name, name))
         return sym;
   }

   return NULL;
}

symbol_t *symtab_define(symtab_t *tab, const char *name, long value)
{
   symbol_t *sym;

   sym = symtab_find(tab, name);
   if (!sym) {
      sym = (symbol_t *)calloc(1, sizeof(*sym));
      if (!sym) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }

      sym->name = xstrdup(name);
      sym->next = tab->head;
      tab->head = sym;
   }

   sym->value = value;
   sym->defined = 1;
   return sym;
}

symbol_t *symtab_reference(symtab_t *tab, const char *name)
{
   symbol_t *sym;

   sym = symtab_find(tab, name);
   if (sym)
      return sym;

   sym = (symbol_t *)calloc(1, sizeof(*sym));
   if (!sym) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   sym->name = xstrdup(name);
   sym->defined = 0;
   sym->next = tab->head;
   tab->head = sym;
   return sym;
}
