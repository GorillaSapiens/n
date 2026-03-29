#ifndef _INCLUDE_LEXTERN_H_
#define _INCLUDE_LEXTERN_H_

// called when an "include" directive is found
int push_file(const char *filename);

// extern stuff created by lex/flex
int yylex();
extern FILE *yyin;
extern char *root_filename;
extern char *current_filename;
extern int yylineno;
extern int yycolumn;
extern char* yytext;

#endif
