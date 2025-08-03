#ifndef _INCLUDE_MEMNAME_H_
#define _INCLUDE_MEMNAME_H_

// determine if memname exists
// returns a unique number for the mem if it exists
// returns -1 if it does not exist
int find_memname(const char* name);

// register a new memname
int register_memname(const char* name);

#endif
