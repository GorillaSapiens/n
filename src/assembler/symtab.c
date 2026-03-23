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
      free(sym->def_file);
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

symbol_t *symtab_declare(symtab_t *tab,
                         const char *name,
                         const char *def_file,
                         int def_line)
{
   symbol_t *sym;

   sym = symtab_find(tab, name);
   if (sym)
      return NULL;

   sym = (symbol_t *)calloc(1, sizeof(*sym));
   if (!sym) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   sym->name = xstrdup(name);
   sym->defined = 0;
   sym->def_file = xstrdup(def_file ? def_file : "<input>");
   sym->def_line = def_line;
   sym->next = tab->head;
   tab->head = sym;
   return sym;
}

void symtab_set_value(symbol_t *sym, long value)
{
   if (!sym)
      return;

   sym->value = value;
   sym->defined = 1;
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
   sym->def_file = NULL;
   sym->def_line = 0;
   sym->next = tab->head;
   tab->head = sym;
   return sym;
}
