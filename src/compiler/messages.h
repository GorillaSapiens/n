#ifndef _INCLUDE_MESSAGES_H_
#define _INCLUDE_MESSAGES_H_

// printf style messages emitted by the compiler
// XRAY_DEBUG is needed to see debug() messages

void yyerror(const char *fmt, ...);
void yywarn(const char *fmt, ...);
void message(const char *fmt, ...);
void debug(const char *fmt, ...);
void error(const char *fmt, ...);
void warning(const char *fmt, ...);

#endif
