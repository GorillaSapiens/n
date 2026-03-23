#ifndef SYMTAB_H
#define SYMTAB_H

typedef struct symbol {
   char *name;
   long value;
   int defined;
   struct symbol *next;
} symbol_t;

typedef struct symtab {
   symbol_t *head;
} symtab_t;

void symtab_init(symtab_t *tab);
void symtab_free(symtab_t *tab);

symbol_t *symtab_find(symtab_t *tab, const char *name);
const symbol_t *symtab_find_const(const symtab_t *tab, const char *name);

symbol_t *symtab_define(symtab_t *tab, const char *name, long value);
symbol_t *symtab_reference(symtab_t *tab, const char *name);

#endif
