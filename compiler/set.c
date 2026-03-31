#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "messages.h"
#include "set.h"
#include "xray.h"

typedef struct Entry {
   const char *key;
   const void *value;
} Entry;

struct Set {
   int size;
   Entry *entries;
};

#if 0
static void dump(Set *s) {
   printf("=== %p %d %p\n", s, s->size, s->entries);
   for (int i = 0; i < s->size; i++) {
      printf("= %d %s %p\n", i, s->entries[i].key, s->entries[i].value);
   }
}
#endif

static int compare_entry_by_key(const void *a, const void *b) {
    const Entry *ea = (const Entry *)a;
    const Entry *eb = (const Entry *)b;
    return strcmp(ea->key, eb->key);
}

static int search_entry_by_key(const void *a, const void *b) {
    const char *key = (const char *)a;
    const Entry *entry = (const Entry *)b;
    return strcmp(key, entry->key);
}

Set *new_set(void) {
   Set *set = (Set *) malloc(sizeof(Set));
   if (!set) {
      error_unreachable("[%s:%d] malloc returned null", __FILE__, __LINE__);
   }

   set->size = 0;
   set->entries = NULL;

   return set;
}

void del_set(Set *set) {
   free(set->entries);
   free(set);
}

int set_add(Set *set, const char *key, const void *value) {
   if (set_get(set, key) != NULL) {
      return -1;
   }

   int index = set->size;
   set->size++;
   set->entries = (Entry *) realloc(set->entries, set->size * sizeof(Entry));
   set->entries[index].key = key;
   set->entries[index].value = value;

   qsort(set->entries, set->size, sizeof(Entry), compare_entry_by_key);

   return 0;
}

const void *set_get(Set *set, const char *key) {
   Entry *found = bsearch(key, set->entries, set->size, sizeof(Entry), search_entry_by_key);
   return (found == NULL) ? NULL : found->value;
}

void set_rm(Set *set, const char *key) {
   Entry *found = bsearch(key, set->entries, set->size, sizeof(Entry), search_entry_by_key);
   if (found) {
      int index = found - set->entries;
      set->size--;

      if (index < set->size) {
         memmove(&set->entries[index], &set->entries[index + 1],
               (set->size - index) * sizeof(Entry));
      }
      // no realloc, we expect very few "rm"
   }
}

