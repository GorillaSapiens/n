#ifndef _INCLUDE_PAIR_H_
#define _INCLUDE_PAIR_H_

#ifndef _INSIDE_PAIR_C_
typedef void *Pair;
#endif

// --- Create pair ---
Pair *pair_create(void);

// --- Insert or update ---
void pair_insert(Pair *pair, const char *key, void *value);

// --- Existance ---
bool pair_exists(Pair *pair, const char *key);

// --- Lookup ---
void *pair_get(Pair *pair, const char *key);

// --- Delete key ---
void pair_delete(Pair *pair, const char *key);

// --- Free pair ---
void pair_destroy(Pair *pair);

#endif
