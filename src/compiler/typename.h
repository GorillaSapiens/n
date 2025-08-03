#ifndef _INCLUDE_TYPENAME_H_
#define _INCLUDE_TYPENAME_H_

// determine if typename exists
// returns a unique number for the type if it exists
// returns -1 if it does not exist
int find_typename(const char* name);

// register a new typename
int register_typename(const char* name);

// determine if memname exists
// returns a unique number for the mem if it exists
// returns -1 if it does not exist
int find_memname(const char* name);

// register a new memname
int register_memname(const char* name);

#endif
