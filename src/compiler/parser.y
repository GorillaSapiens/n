/* ---------- parser.y ---------- */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "ast.h"

extern char *current_filename;
extern int push_file(const char *filename);
extern int yylex();
void yyerror(const char *fmt, ...);
void yywarn(const char *fmt, ...);

extern int yylineno;
extern int yycolumn;

/////

#define MAX_TYPES 1024
static char *type_names[MAX_TYPES];
static int type_count = 0;

int find_typename(const char* name) {
   for (int i = 0; i < type_count; i++) {
      if (strcmp(type_names[i], name) == 0) return i;
   }
   return -1;
}

int register_typename(const char* name) {
   // allows duplicates, errors come on the second pass
   if (find_typename(name) == -1) {
      if (type_count < MAX_TYPES) {
         type_names[type_count++] = strdup(name);
      }
      else {
         yyerror("type table full %s:%d.%d",
            current_filename, yylineno, yycolumn);
         return -1;
      }
   }
   return 0;
}

ASTNode *root = NULL;

void debug(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "debug: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
}

void error(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "error: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(-1);
}

void warning(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "warning: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(-1);
}

void check_type_decl(ASTNode *tree) {

   if (!tree) {
      return;
   }

   if (!strcmp(tree->name, "type_decl")) {
      debug("%s:%s", __FUNCTION__, tree->children[0]->strval);
      bool haveSize = false;
      // we need to guarantee a "size"
      if (strcmp(tree->children[1]->name, "empty")) {
         for (ASTNode *list = tree->children[1];
              list != NULL;
              list = list->children[1]) {
            debug("%s:\t%s", __FUNCTION__, list->children[0]->strval);
            if (!strncmp(list->children[0]->strval, "$size:", 6)) {
               if (haveSize) {
                  error("[%s:%d.%d] type_decl '%s' has multiple '$size:' flags",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval);
               }
               haveSize = true;
            }
         }
      }
      if (!haveSize) {
         error("[%s:%d.%d] type_decl '%s' missing '$size:' flag",
            tree->file, tree->line, tree->column, tree->children[0]->strval);
      }
   }

   for (int i = 0; i < tree->count; i++) {
      check_type_decl(tree->children[i]);
   }
}

void parse_dump(void) {
   if (root) {
      dump_ast_flat(root, "", 1, NULL);
   }

   check_type_decl(root);
}

%}

%union {
    char    *str;
    double   dval;
    int      intval;
    ASTNode *node;
}

%token <str> STRING IDENTIFIER TYPENAME FLAG OPERATOR
%token <intval> INTEGER
%token <dval> FLOAT
%token IF ELSE WHILE FOR RETURN TYPE
%token ASSIGN
%token EQ NE LE GE LSHIFT RSHIFT OR AND
%token INC DEC ARROW
%token STRUCT UNION
%token GOTO SWITCH CASE DEFAULT
%token BREAK
%token CONTINUE
%token DO
%token CONST
%token STATIC

%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token AND_ASSIGN OR_ASSIGN XOR_ASSIGN LSHIFT_ASSIGN RSHIFT_ASSIGN

%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left LSHIFT RSHIFT
%left '+' '-'
%left '*' '/' '%'
%right UADDR UINDIRECT
%left POSTINC POSTDEC   // post (x++, x--)
%right UMINUS UINC UDEC
%right ASSIGN

%type <node> additive_expr arg_list array_initializer assignable
%type <node> bitwise_and_expr bitwise_or_expr bitwise_xor_expr block
%type <node> case_block case_section
%type <node> const_decl_stmt decl_stmt
%type <node> equality_expr expr expr_args expr_list expr_stmt
%type <node> flag flag_list function_decl const_function_decl
%type <node> include_stmt
%type <node> logical_and_expr logical_or_expr
%type <node> multiplicative_expr
%type <node> opt_address opt_array_dim opt_expr opt_flags opt_pointer
%type <node> param_decls param_list
%type <node> postfix_expr primary_expr program program_item
%type <node> relational_expr
%type <node> shift_expr statement statement_list struct_decl struct_field
%type <node> struct_fields struct_init struct_inits struct_literal
%type <node> type_decl
%type <node> type_name
%type <node> unary_expr
%type <node> union_decl

%type <node> param static_param const_param

%type <node> return_stmt goto_stmt break_stmt continue_stmt switch_stmt 
%type <node> if_stmt while_stmt for_stmt do_stmt label_stmt 

%define parse.error verbose
%locations


%token INCLUDE
%%

program:
    program_item         { if ($1) { root = $$ = MAKE_NODE($1); } }
  | program program_item { if ($2) { root = $$ = MAKE_NODE($1, $2); } else { $$ = $1; } }
  ;

program_item:
    type_decl            { $$ = $1; }
  | function_decl        { $$ = $1; }
  | const_function_decl  { $$ = $1; }
  | decl_stmt            { $$ = $1; }
  | const_decl_stmt      { $$ = $1; }
  | struct_decl          { $$ = $1; }
  | union_decl           { $$ = $1; }
  | include_stmt         { $$ = NULL; } // process an include line
  ;

type_decl:
    TYPE IDENTIFIER '{' opt_flags '}' ';' {
        if (register_typename($2) < 0) YYABORT;
        $$ = MAKE_NODE(make_identifier_leaf($2), $4);
    }
  | TYPE '*' '{' opt_flags '}' ';' {
        if (register_typename("*") < 0) YYABORT;
        $$ = MAKE_NODE(make_identifier_leaf("*"), $4);
    }
  | TYPE TYPENAME '{' opt_flags '}' ';' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;
        $$ = MAKE_NODE(make_identifier_leaf($2), $4); // TODO FIX is this needed?
    }
  ;

function_decl:
    type_name IDENTIFIER '(' param_list ')' ';'    { $$ = MAKE_NODE($1, make_identifier_leaf($2), $4); }
  | type_name IDENTIFIER '(' param_list ')' block  { $$ = MAKE_NODE($1, make_identifier_leaf($2), $4, $6); }
  | type_name OPERATOR '(' param_list ')' ';'      { $$ = MAKE_NODE($1, make_identifier_leaf($2), $4); }
  | type_name OPERATOR '(' param_list ')' block    { $$ = MAKE_NODE($1, make_identifier_leaf($2), $4, $6); }
  ;

const_function_decl:
    CONST type_name IDENTIFIER '(' param_list ')' ';'    { $$ = MAKE_NODE($2, make_identifier_leaf($3), $5); }
  | CONST type_name IDENTIFIER '(' param_list ')' block  { $$ = MAKE_NODE($2, make_identifier_leaf($3), $5, $7); }
  | CONST type_name OPERATOR '(' param_list ')' ';'      { $$ = MAKE_NODE($2, make_identifier_leaf($3), $5); }
  | CONST type_name OPERATOR '(' param_list ')' block    { $$ = MAKE_NODE($2, make_identifier_leaf($3), $5, $7); }
  ;

struct_decl:
    STRUCT IDENTIFIER '{' {
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' {
        register_typename($2);
        $$ = MAKE_NODE(make_identifier_leaf($2), $5);
    }
  | STRUCT TYPENAME '{' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
  ;

union_decl:
    UNION IDENTIFIER '{' {
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' {
        register_typename($2);
        $$ = MAKE_NODE(make_identifier_leaf($2), $5);
    }
  | UNION TYPENAME '{' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
  ;

struct_fields:
    struct_field                        { $$ = $1; }
  | struct_fields struct_field          { $$ = MAKE_NODE($1, $2); }
  ;

struct_field:
    type_name opt_pointer IDENTIFIER ';' { $$ = MAKE_NODE($1, $2, make_identifier_leaf($3)); }
  ;

opt_pointer:
    /* empty */     { $$ = make_integer_leaf(0); }
  | '*' opt_pointer { $$ = $2; $$->intval++; }
  ;

param_list:
    /* empty */     { $$ = make_empty_leaf(); }
  | param_decls     { $$ = $1; }
  ;

param_decls:
    param                        { $$ = MAKE_NODE($1); }
  | const_param                  { $$ = MAKE_NODE($1); }
  | static_param                 { $$ = MAKE_NODE($1); }
  | param_decls ',' param        { $$ = MAKE_NODE($1, MAKE_NODE($3)); }
  | param_decls ',' const_param  { $$ = MAKE_NODE($1, MAKE_NODE($3)); }
  | param_decls ',' static_param { $$ = MAKE_NODE($1, MAKE_NODE($3)); }
;

param:
    type_name IDENTIFIER { $$ = MAKE_NODE($1, make_identifier_leaf($2)); }
  | type_name            { $$ = MAKE_NODE($1); } // unnamed params are allowed
;

const_param:
    CONST type_name IDENTIFIER { $$ = MAKE_NODE($2, make_identifier_leaf($3)); }
  | CONST type_name            { $$ = MAKE_NODE($2); } // unnamed params are allowed
;

static_param:
    STATIC type_name IDENTIFIER { $$ = MAKE_NODE($2, make_identifier_leaf($3)); }
  | STATIC type_name            { $$ = MAKE_NODE($2); } // unnamed params are allowed, but why ???
;

type_name:
    TYPENAME { $$ = make_identifier_leaf($1); }
  ;

block:
    '{' statement_list '}' { $$ = $2; }
  ;

statement_list:
    /* empty */              { $$ = make_empty_leaf(); }
  | statement_list statement { $$ = ($1->kind != AST_EMPTY) ? MAKE_NODE($1, $2) : MAKE_NODE($2); }
  ;

statement:
    block            { $$ = $1; }
  | decl_stmt        { $$ = $1; }
  | const_decl_stmt  { $$ = $1; }
  | expr_stmt        { $$ = $1; }
  | return_stmt      { $$ = $1; }
  | goto_stmt        { $$ = $1; }
  | break_stmt       { $$ = $1; }
  | continue_stmt    { $$ = $1; }
  | switch_stmt      { $$ = $1; }
  | if_stmt          { $$ = $1; }
  | while_stmt       { $$ = $1; }
  | for_stmt         { $$ = $1; }
  | do_stmt          { $$ = $1; }
  | label_stmt       { $$ = $1; }
  ;

return_stmt:
    RETURN opt_expr ';' { $$ = MAKE_NODE($2); }
  ;

goto_stmt:
    GOTO IDENTIFIER ';' { $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

break_stmt:
    BREAK ';'            { $$ = MAKE_NODE(make_empty_leaf()); }
  | BREAK IDENTIFIER ';' { $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

continue_stmt:
    CONTINUE ';'            { $$ = MAKE_NODE(make_empty_leaf()); }
  | CONTINUE IDENTIFIER ';' { $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

switch_stmt:
    SWITCH '(' expr ')' '{' case_section '}' { $$ = MAKE_NODE($3,$6); }
  ;

if_stmt:
    IF '(' expr ')' block ELSE block { $$ = MAKE_NODE($3, $5, $7); }
  | IF '(' expr ')' block            { $$ = MAKE_NODE($3, $5); }
  ;

while_stmt:
    WHILE '(' expr ')' block { $$ = MAKE_NODE($3, $5); }
  ;

for_stmt:
    FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' block { $$ = MAKE_NODE($3, $5, $7, $9); }
  ;

do_stmt:
    DO block WHILE '(' expr ')' ';' { $$ = MAKE_NODE($2, $5); }
  ;

label_stmt:
    IDENTIFIER ':' statement { $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  ;

opt_address:
    /* empty */   { $$ = make_empty_leaf(); }
  | '@' INTEGER   { $$ = make_integer_leaf($2); }
  ;

decl_stmt:
    type_name IDENTIFIER opt_array_dim opt_address ';'                          { $$ = MAKE_NODE($1, make_identifier_leaf($2), $3, $4); }
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN expr ';'              { $$ = MAKE_NODE($1, make_identifier_leaf($2), $3, $4, $6); }
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN array_initializer ';' { $$ = MAKE_NODE($1, make_identifier_leaf($2), $3, $4, $6); }
  ;

const_decl_stmt:
    CONST type_name IDENTIFIER opt_array_dim opt_address ';'                          { $$ = MAKE_NODE($2, make_identifier_leaf($3), $4, $5); }
  | CONST type_name IDENTIFIER opt_array_dim opt_address ASSIGN expr ';'              { $$ = MAKE_NODE($2, make_identifier_leaf($3), $4, $5, $7); }
  | CONST type_name IDENTIFIER opt_array_dim opt_address ASSIGN array_initializer ';' { $$ = MAKE_NODE($2, make_identifier_leaf($3), $4, $5, $7); }
  ;

opt_array_dim:
    /* empty */  { $$ = make_empty_leaf(); }
  | '[' expr ']' { $$ = $2; }
  ;

array_initializer:
    '{' expr_list '}' { $$ = $2; }
  ;

expr_list:
    expr                        { $$ = MAKE_NODE($1); }
  | expr_list ',' expr          { $$ = MAKE_NODE($1, $3); }
;


expr_stmt:
    expr ';' { $$ = $1; }
  | ';'      { $$ = make_empty_leaf(); }
  ;

opt_expr:
    /* empty */  { $$ = make_empty_leaf(); }
  | expr         { $$ = $1; }
  ;


expr:
    logical_or_expr                    { $$ = MAKE_NODE($1); }
  | logical_or_expr '?' expr ':' expr  { $$ = MAKE_NODE(make_identifier_leaf("?:"), $1, $3, $5); }
  | assignable ASSIGN expr             { $$ = MAKE_NODE(make_identifier_leaf(":="), $1, $3); }
  | assignable ADD_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("+="), $1, $3); }
  | assignable SUB_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("-="), $1, $3); }
  | assignable MUL_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("*="), $1, $3); }
  | assignable DIV_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("/="), $1, $3); }
  | assignable MOD_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("%="), $1, $3); }
  | assignable AND_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("&="), $1, $3); }
  | assignable OR_ASSIGN expr          { $$ = MAKE_NODE(make_identifier_leaf("|="), $1, $3); }
  | assignable XOR_ASSIGN expr         { $$ = MAKE_NODE(make_identifier_leaf("^="), $1, $3); }
  | assignable LSHIFT_ASSIGN expr      { $$ = MAKE_NODE(make_identifier_leaf("<<="), $1, $3); }
  | assignable RSHIFT_ASSIGN expr      { $$ = MAKE_NODE(make_identifier_leaf(">>="), $1, $3); }
  ;

assignable:
    IDENTIFIER                { $$ = MAKE_NODE(make_identifier_leaf($1)); }
  | postfix_expr '[' expr ']' { $$ = MAKE_NODE($1,$3); }
  | '*' unary_expr            { $$ = MAKE_NODE(make_identifier_leaf("*"), $2); }
  ;

logical_or_expr:
    logical_and_expr                    { $$ = $1; }
  | logical_or_expr OR logical_and_expr { $$ = MAKE_NAMED_NODE("||", $1, $3); }
  ;

logical_and_expr:
    bitwise_or_expr                       { $$ = $1; }
  | logical_and_expr AND bitwise_or_expr  { $$ = MAKE_NAMED_NODE("&&", $1, $3); }
  ;

bitwise_or_expr:
    bitwise_xor_expr                       { $$ = $1; }
  | bitwise_or_expr '|' bitwise_xor_expr  { $$ = MAKE_NAMED_NODE("|", $1, $3); }
  ;

bitwise_xor_expr:
    bitwise_and_expr                       { $$ = $1; }
  | bitwise_xor_expr '^' bitwise_and_expr  { $$ = MAKE_NAMED_NODE("^", $1, $3); }
  ;

bitwise_and_expr:
    equality_expr                       { $$ = $1; }
  | bitwise_and_expr '&' equality_expr  { $$ = MAKE_NAMED_NODE("&", $1, $3); }
  ;

equality_expr:
    relational_expr                       { $$ = $1; }
  | equality_expr EQ relational_expr      { $$ = MAKE_NAMED_NODE("==", $1, $3); }
  | equality_expr NE relational_expr      { $$ = MAKE_NAMED_NODE("!=", $1, $3); }
  ;

relational_expr:
    shift_expr                       { $$ = $1; }
  | relational_expr '<' shift_expr   { $$ = MAKE_NAMED_NODE("<", $1, $3); }
  | relational_expr '>' shift_expr   { $$ = MAKE_NAMED_NODE(">", $1, $3); }
  | relational_expr LE shift_expr    { $$ = MAKE_NAMED_NODE("<=", $1, $3); }
  | relational_expr GE shift_expr    { $$ = MAKE_NAMED_NODE(">=", $1, $3); }
  ;

shift_expr:
    additive_expr                       { $$ = $1; }
  | shift_expr LSHIFT additive_expr     { $$ = MAKE_NAMED_NODE("<<", $1, $3); }
  | shift_expr RSHIFT additive_expr     { $$ = MAKE_NAMED_NODE(">>", $1, $3); }
  ;

additive_expr:
    multiplicative_expr                       { $$ = $1; }
  | additive_expr '+' multiplicative_expr     { $$ = MAKE_NAMED_NODE("+", $1, $3); }
  | additive_expr '-' multiplicative_expr     { $$ = MAKE_NAMED_NODE("-", $1, $3); }
  ;

multiplicative_expr:
    unary_expr                         { $$ = $1; }
  | multiplicative_expr '*' unary_expr { $$ = MAKE_NAMED_NODE("*", $1, $3); }
  | multiplicative_expr '/' unary_expr { $$ = MAKE_NAMED_NODE("/", $1, $3); }
  | multiplicative_expr '%' unary_expr { $$ = MAKE_NAMED_NODE("%", $1, $3); }
  ;

unary_expr:
    postfix_expr   { $$ = $1; }
  | '!' unary_expr { $$ = MAKE_NAMED_NODE("!", $2); }
  | '~' unary_expr { $$ = MAKE_NAMED_NODE("~", $2); }
  | '-' unary_expr { $$ = MAKE_NAMED_NODE("-", $2); }
  | '&' unary_expr { $$ = MAKE_NAMED_NODE("&", $2); }
  | '*' unary_expr { $$ = MAKE_NAMED_NODE("*", $2); }
  | INC unary_expr { $$ = MAKE_NAMED_NODE("pre++", $2); }
  | DEC unary_expr { $$ = MAKE_NAMED_NODE("pre--", $2); }
  ;

postfix_expr:
    primary_expr                   { $$ = $1; }
  | postfix_expr '.' IDENTIFIER    { $$ = MAKE_NAMED_NODE(".", $1, make_identifier_leaf($3)); }
  | postfix_expr ARROW IDENTIFIER  { $$ = MAKE_NAMED_NODE("->", $1, make_identifier_leaf($3)); }
  | postfix_expr INC               { $$ = MAKE_NAMED_NODE("post++", $1); }
  | postfix_expr DEC               { $$ = MAKE_NAMED_NODE("post--", $1); }
  | postfix_expr '[' expr ']'      { $$ = MAKE_NAMED_NODE("[]", $1, $3); }
  | IDENTIFIER '(' arg_list ')'    { $$ = MAKE_NAMED_NODE("()", make_identifier_leaf($1), $3); }
  ;

primary_expr:
    IDENTIFIER     { $$ = make_identifier_leaf($1); }
  | INTEGER        { $$ = make_integer_leaf($1); }
  | FLOAT          { $$ = make_float_leaf($1); }
  | STRING         { $$ = make_string_leaf($1); }
  | struct_literal { $$ = $1; }
  | '(' expr ')'   { $$ = $2; }
  ;

arg_list:
    /* empty */  { $$ = make_empty_leaf(); }
  | expr_args    { $$ = $1; }
  ;

expr_args:
    expr                       { $$ = MAKE_NODE($1); }
  | expr_args ',' expr         { $$ = MAKE_NODE($1, $3); }
  ;


struct_literal:
    TYPENAME '{' struct_inits ';' '}' { $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  | TYPENAME '{' FLOAT '}'            { $$ = MAKE_NODE(make_identifier_leaf($1), make_float_leaf($3)); }
  | TYPENAME '{' '-' FLOAT '}'        { $$ = MAKE_NODE(make_identifier_leaf($1), make_float_leaf(-$4)); }
  | TYPENAME '{' INTEGER '}'          { $$ = MAKE_NODE(make_identifier_leaf($1), make_integer_leaf($3)); }
  | TYPENAME '{' '-' INTEGER '}'      { $$ = MAKE_NODE(make_identifier_leaf($1), make_integer_leaf(-$4)); }
  | TYPENAME '{' STRING '}'           { $$ = MAKE_NODE(make_identifier_leaf($1), make_string_leaf($3)); }
  ;

struct_inits:
    struct_inits ';' struct_init { $$ = MAKE_NODE($1, $3); }
  | struct_init                  { $$ = MAKE_NODE($1); }
  ;

struct_init:
    IDENTIFIER ASSIGN expr { $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  ;

case_section:
    case_section case_block { $$ = MAKE_NODE($1, $2); }
  | case_block              { $$ = MAKE_NODE($1); }
  ;

case_block:
    CASE expr ':' statement_list { $$ = MAKE_NODE($2, $4); }
  | DEFAULT ':' statement_list   { $$ = MAKE_NODE(make_empty_leaf(), $3); }
  ;

opt_flags:
    flag_list   { $$ = $1; }
  | /* empty */ { $$ = make_empty_leaf(); }
  ;

flag_list:
    flag_list flag { $$ = MAKE_NODE($2, $1); }
  | flag           { $$ = MAKE_NODE($1); }
  ;

flag:
    FLAG { $$ = make_identifier_leaf($1); }
  ;

include_stmt:
    INCLUDE STRING {
        if (push_file($2) != 0) {
            yyerror("failed to include file: %s", $2);
            YYABORT;
        }
    }
  ;

%%
extern char* yytext;

void yyerror(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int size = 1 + vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   char *str = (char *) malloc(sizeof(char) * size);

   va_start(ap, fmt);
   vsnprintf(str, size, fmt, ap);
   va_end(ap);

   fprintf(stderr, "Error at %s:%d.%d (near '%s')\n", current_filename, yylineno, yycolumn, yytext);
   fprintf(stderr, "   %s\n", str);
   free(str);
}

void yywarn(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int size = 1 + vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   char *str = (char *) malloc(sizeof(char) * size);

   va_start(ap, fmt);
   vsnprintf(str, size, fmt, ap);
   va_end(ap);

   fprintf(stderr, "Warning at %s:%d.%d (near '%s')\n", current_filename, yylineno, yycolumn, yytext);
   fprintf(stderr, "   %s\n", str);
   free(str);
}
