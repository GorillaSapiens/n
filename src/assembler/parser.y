%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbols.h"
#include "opcodes.h"

extern void yyrestart(FILE* input_file);
extern int yylex();
extern int yylineno;
extern int yycolumn;
extern FILE* yyin;
extern char* current_file;
extern int include_depth;
extern FILE* file_stack[];
extern char* name_stack[];
extern int line_stack[];
extern int col_stack[];

void yyerror(const char *s) {
    fprintf(stderr, "%s at %s:%d:%d\n", s, current_file ? current_file : "<stdin>", yylineno, yycolumn);
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

%%

program:
    | program line
    ;

line:
      LABEL COLON NEWLINE                { define_label($1, current_address); }
    | OPCODE { current_opcode = $1; } operand NEWLINE
    | OPCODE NEWLINE                     { emit_byte(get_opcode($1, "impl")); }
    | DOTBYTE NUMBER NEWLINE            { emit_byte($2); }
    | DOTWORD NUMBER NEWLINE            { emit_word($2); }
    | DOTORG NUMBER NEWLINE             { current_address = $2; }
    | DOTASCII STRING NEWLINE           { emit_string($2); }
    | DOTTEXT STRING NEWLINE            { emit_string($2); emit_byte(0); }
    | DOTIMPORT import_list NEWLINE     { printf("; importing:\n"); }
    | DOTEXPORT import_list NEWLINE     { printf("; exporting:\n"); }
    | DOTINCLUDE STRING NEWLINE {
        if (include_depth >= 16) {
            fprintf(stderr, "Include nesting too deep\n");
            exit(1);
        }
        FILE* f = fopen($2, "r");
        if (!f) {
            perror($2);
            exit(1);
        }
        file_stack[include_depth] = yyin;
        name_stack[include_depth] = current_file;
        line_stack[include_depth] = yylineno;
        col_stack[include_depth] = yycolumn;
        include_depth++;

        yyin = f;
        yyrestart(yyin);
        current_file = $2;
        yylineno = 1;
        yycolumn = 1;
    }
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

%%
