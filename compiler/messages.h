#ifndef _INCLUDE_MESSAGES_H_
#define _INCLUDE_MESSAGES_H_

// printf style messages emitted by the compiler
// XRAY_DEBUG is needed to see debug() messages

#include "noreturn.h"

void noreturn yyerror(const char *fmt, ...);
void yywarn(const char *fmt, ...);
void message(const char *fmt, ...);
void debug(const char *fmt, ...);
void noreturn error(const char *fmt, ...);
void warning(const char *fmt, ...);

#endif
