#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TABLE_SIZE 101  // prime number for better distribution

typedef struct Entry {
    char *key;
    void *value;
    struct Entry *next;
} Entry;

typedef struct {
    Entry *buckets[TABLE_SIZE];
} Pair;

#define _INSIDE_PAIR_C_
#include "pair.h"

// --- Hash function ---
static unsigned int hash(const char *key) {
    unsigned int h = 0;
    while (*key)
        h = (h * 31) + (unsigned char)(*key++);
    return h % TABLE_SIZE;
}

// --- Create pair ---
Pair *pair_create(void) {
    Pair *pair = calloc(1, sizeof(Pair));
    return pair;
}

// --- Insert or update ---
void pair_insert(Pair *pair, const char *key, void *value) {
    unsigned int idx = hash(key);
    Entry *node = pair->buckets[idx];

    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->value = value;
            return;
        }
        node = node->next;
    }

    Entry *new_node = malloc(sizeof(Entry));
    new_node->key = strdup(key);
    new_node->value = value;
    new_node->next = pair->buckets[idx];
    pair->buckets[idx] = new_node;
}

// --- Lookup ---
bool pair_exists(Pair *pair, const char *key) {
    unsigned int idx = hash(key);
    Entry *node = pair->buckets[idx];
    while (node) {
        if (strcmp(node->key, key) == 0)
            return true;
        node = node->next;
    }
    return false;
}

// --- Lookup ---
void *pair_get(Pair *pair, const char *key) {
    unsigned int idx = hash(key);
    Entry *node = pair->buckets[idx];
    while (node) {
        if (strcmp(node->key, key) == 0)
            return node->value;
        node = node->next;
    }
    return NULL;
}

// --- Delete key ---
void pair_delete(Pair *pair, const char *key) {
    unsigned int idx = hash(key);
    Entry **ptr = &pair->buckets[idx];
    while (*ptr) {
        if (strcmp((*ptr)->key, key) == 0) {
            Entry *to_free = *ptr;
            *ptr = to_free->next;
            free(to_free->key);
            free(to_free);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

// --- Free pair ---
void pair_destroy(Pair *pair) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        Entry *node = pair->buckets[i];
        while (node) {
            Entry *next = node->next;
            free(node->key);
            free(node);
            node = next;
        }
    }
    free(pair);
}
