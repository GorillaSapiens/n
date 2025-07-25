#ifndef _INCLUDE_XRAY_H_
#define _INCLUDE_XRAY_H_

#include "noreturn.h"

// return the xray number for a human readable string
int lookup_xray(const char *);

// set an xray by number
void set_xray(int n);

// determine if an xray is set
int get_xray(int n);

// override the system exit()
void noreturn xray_exit(int, const char *, int);

#ifndef NO_XRAY_OVERRIDE_EXIT
#define exit(x) xray_exit(x, __FILE__, __LINE__)
#endif

#endif
