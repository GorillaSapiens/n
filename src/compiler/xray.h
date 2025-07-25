#ifndef _INCLUDE_XRAY_H_
#define _INCLUDE_XRAY_H_

// return the xray number for a human readable string
int lookup_xray(const char *);

// set an xray by number
void set_xray(int n);

// determine if an xray is set
int get_xray(int n);

#endif
