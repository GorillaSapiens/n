#ifndef _INCLUDE_MD5SEEN_H_
#define _INCLUDE_MD5SEEN_H_

#include <stdio.h>
#include <stdbool.h>

// returns true if the file has been seen before
// "seen" is based on md5sum
bool md5seen(const char *filename, FILE *f);

#endif
