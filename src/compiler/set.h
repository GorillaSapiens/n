#ifndef _INCLUDE_SET_H_
#define _INCLUDE_SET_H_

struct Set;
typedef struct Set Set;

Set *new_set(void);
void del_set(Set *set);
int set_add(Set *set, const char *key, const void *value);
const void *set_get(Set *set, const char *key);
void set_rm(Set *set, const char *key);

#endif
