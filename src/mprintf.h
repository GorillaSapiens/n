#ifndef _INCLUDE_UGE_MPRINTF_H_
#define _INCLUDE_UGE_MPRINTF_H_

#include <stdarg.h>

// returns malloc'd pointer that must be free'd
char *mprintf(const char *fmt, ...);

#endif
