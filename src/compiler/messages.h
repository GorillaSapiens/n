#ifndef _INCLUDE_MESSAGES_H_
#define _INCLUDE_MESSAGES_H_

void yyerror(const char *fmt, ...);
void yywarn(const char *fmt, ...);

void debug(const char *fmt, ...);
void error(const char *fmt, ...);
void warning(const char *fmt, ...);

#endif
