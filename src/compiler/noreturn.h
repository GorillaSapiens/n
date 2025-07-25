#ifndef _INCLUDE_NORETURN_H_
#define _INCLUDE_NORETURN_H_

#if defined(__GNUC__) && __GNUC__ >= 3
#define noreturn __attribute__((__noreturn__))
#else
#define noreturn
#endif

#endif
