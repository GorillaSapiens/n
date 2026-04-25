//! @file compiler/noreturn.h
//! @brief Declares noreturn portability helpers for the n65 compiler.

#ifndef _INCLUDE_NORETURN_H_
#define _INCLUDE_NORETURN_H_

// macro for functions that never return.
// use as void noreturn foo()

#if defined(__GNUC__) && __GNUC__ >= 3
#define noreturn __attribute__((__noreturn__))
#else
#define noreturn
#endif

#endif
