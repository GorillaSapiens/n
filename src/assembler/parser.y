%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbols.h"
#include "opcodes.h"
extern int yylex();
extern int yylineno;
extern int yycolumn;
void yyerror(const char *s) {
    fprintf(stderr, "Syntax error at line %d, column %d: %s\n", yylineno, yycolumn, s);
}
char* current_opcode = NULL;
%}

%union {
    int num;
    char* str;
}

%token <str> OPCODE LABEL STRING
%token <num> NUMBER
%token COLON COMMA LPAREN RPAREN HASH NEWLINE
%token DOTBYTE DOTWORD DOTORG DOTASCII DOTTEXT
%token DOTIMPORT DOTEXPORT DOTINCLUDE DOTPROC DOTENDPROC

%define parse.error verbose
%%

program:
    | program line
    ;

line:
      LABEL COLON NEWLINE                { define_label($1, current_address); }
    | DOTIMPORT import_list NEWLINE      { printf("; importing:\n"); }
    | DOTEXPORT import_list NEWLINE      { printf("; exporting:\n"); }
    | OPCODE operand NEWLINE             { current_opcode = $1; }
    | OPCODE NEWLINE                     { emit_byte(get_opcode($1, "impl")); }
    | DOTBYTE NUMBER NEWLINE            { emit_byte($2); }
    | DOTWORD NUMBER NEWLINE            { emit_word($2); }
    | DOTORG NUMBER NEWLINE             { current_address = $2; }
    | DOTASCII STRING NEWLINE           { emit_string($2); }
    | DOTTEXT STRING NEWLINE            { emit_string($2); emit_byte(0); }
    | DOTINCLUDE STRING NEWLINE         { printf("; including file %s (not yet implemented)\n", $2); }
    | DOTPROC LABEL NEWLINE             { printf("; begin proc %s\n", $2); }
    | DOTENDPROC NEWLINE                { printf("; end proc\n"); }
    | NEWLINE
    ;

operand:
      HASH NUMBER                        { emit_byte(get_opcode(current_opcode, "imm")); emit_byte($2); }
    | NUMBER                             { emit_byte(get_opcode(current_opcode, "abs")); emit_word($1); }
    | NUMBER COMMA 'X'                   { emit_byte(get_opcode(current_opcode, "abs,X")); emit_word($1); }
    | NUMBER COMMA 'Y'                   { emit_byte(get_opcode(current_opcode, "abs,Y")); emit_word($1); }
    | LPAREN NUMBER RPAREN               { emit_byte(get_opcode(current_opcode, "ind")); emit_word($2); }
    | LPAREN NUMBER COMMA 'X' RPAREN     { emit_byte(get_opcode(current_opcode, "(ind,X)")); emit_byte($2); }
    | LPAREN NUMBER RPAREN COMMA 'Y'     { emit_byte(get_opcode(current_opcode, "(ind),Y")); emit_byte($2); }
    | LABEL                              { emit_byte(get_opcode(current_opcode, "abs")); emit_word(get_label_address($1)); }
    ;

import_list:
      LABEL                              { printf("  %s\n", $1); }
    | import_list COMMA LABEL            { printf("  %s\n", $3); }
    ;

%%
