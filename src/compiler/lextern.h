#ifndef _INCLUDE_LEXTERN_H_
#define _INCLUDE_LEXTERN_H_

int push_file(const char *filename);
int yylex();

extern char *current_filename;
extern int yylineno;
extern int yycolumn;
extern char* yytext;

#endif
