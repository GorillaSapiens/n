#ifndef _INCLUDE_PAIR_H_
#define _INCLUDE_PAIR_H_

#include <stdbool.h>

#ifndef _INSIDE_PAIR_C_
typedef void *Pair;
#endif

// create/destroy for collection
Pair *pair_create(void);
void pair_destroy(Pair *pair);

void pair_insert(Pair *pair, const char *key, void *value);
bool pair_exists(Pair *pair, const char *key);
void *pair_get(Pair *pair, const char *key);
void pair_delete(Pair *pair, const char *key);
const char *pair_null_value(Pair *pair);

#endif
