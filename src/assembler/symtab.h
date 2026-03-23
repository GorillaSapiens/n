#ifndef SYMTAB_H
#define SYMTAB_H

typedef struct symbol {
   char *name;
   long value;
   int defined;
   char *def_file;
   int def_line;
   struct symbol *next;
} symbol_t;

typedef struct symtab {
   symbol_t *head;
} symtab_t;

void symtab_init(symtab_t *tab);
void symtab_free(symtab_t *tab);

symbol_t *symtab_find(symtab_t *tab, const char *name);
const symbol_t *symtab_find_const(const symtab_t *tab, const char *name);

/*
   Declare a symbol name at a specific source location.

   Returns:
   - existing/new symbol pointer on success
   - NULL if the name was already declared earlier in this pass
*/
symbol_t *symtab_declare(symtab_t *tab,
                         const char *name,
                         const char *def_file,
                         int def_line);

void symtab_set_value(symbol_t *sym, long value);

symbol_t *symtab_reference(symtab_t *tab, const char *name);

#endif
