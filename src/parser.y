/* ---------- parser.y ---------- */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex();
void yyerror(const char *s);

/////

// Type table
#define MAXTYPES 100

typedef struct {
    char* name;
    int size;
} TypeEntry;

TypeEntry typetable[MAXTYPES];
int typecount = 0;

void register_typename(const char* id, int size) {
    for (int i = 0; i < typecount; i++) {
        if (strcmp(typetable[i].name, id) == 0) {
            typetable[i].size = size;
            return;
        }
    }
    if (typecount < MAXTYPES) {
        typetable[typecount].name = strdup(id);
        typetable[typecount].size = size;
        typecount++;
    }
}

int is_typename(const char* id) {
    for (int i = 0; i < typecount; i++) {
        if (strcmp(typetable[i].name, id) == 0)
            return 1;
    }
    return 0;
}

void declare_typename(const char* name) {
    // Add to type table without fields yet
    printf("Declared typename: %s\n", name);
    register_typename(name, -1);
}

/////

typedef struct Field {
    char* name;
    char* type;
    int pointer_depth;
    struct Field* next;
} Field;

typedef struct FieldList {
    Field* head;
    Field* tail;
} FieldList;

void register_struct(const char* name, FieldList* fields, int is_union) {
    printf("Registering %s '%s'\n", is_union ? "union" : "struct", name);
    for (Field* f = fields->head; f; f = f->next) {
        printf("  field: %s %s%s\n", f->type, (f->pointer_depth > 0) ? "*" : "", f->name);
    }
}

/////

extern int lineno;

%}

%union {
    char* str;
    double dval;
    int   intval;

    struct FieldList* fieldlist;
    struct Field* field;
}

%token <str> STRING IDENTIFIER TYPENAME
%token <intval> INTEGER
%token <dval> FLOAT
%token IF ELSE WHILE FOR RETURN TYPE
%token ASSIGN
%token EQ NE LE GE LSHIFT RSHIFT OR AND
%token OPERATOR
%token INC DEC
%token STRUCT UNION

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

%type <str> type_name
%type <fieldlist> struct_fields
%type <field> struct_field
%type <intval> opt_pointer

%%

program:
    program_item
  | program program_item
  ;

program_item:
    type_decl
  | function_decl
  | top_level_stmt
  | struct_decl
  | union_decl
  ;

type_decl:
    TYPE IDENTIFIER '(' INTEGER ')' ';' {
        register_typename($2, $4);
    }
  ;

function_decl:
    type_name IDENTIFIER '(' param_list ')' ';'
  | type_name IDENTIFIER '(' param_list ')' block
  | type_name OPERATOR '(' param_list ')' block
  ;

struct_decl:
    STRUCT IDENTIFIER '{' {
        declare_typename($2);  // Add early to type table
    }
    struct_fields '}' ';' {
        register_struct($2, $5, 0);
    }
  ;

union_decl:
    UNION IDENTIFIER '{' {
        declare_typename($2);  // Add early to type table
    }
    struct_fields '}' ';' {
        register_struct($2, $5, 0);
    }
  ;


struct_fields:
    struct_fields ',' struct_field {
        $$ = $1;
        $1->tail->next = $3;
        $1->tail = $3;
    }
  | struct_field {
        $$ = malloc(sizeof(FieldList));
        $$->head = $$->tail = $1;
    }
  ;

struct_field:
    type_name opt_pointer IDENTIFIER {
        Field* f = malloc(sizeof(Field));
        f->type = strdup($1);
        f->pointer_depth = $2;
        f->name = $3;
        f->next = NULL;
        $$ = f;
    }
  ;

opt_pointer:
    /* empty */ { $$ = 0; }
  | '*' opt_pointer { $$ = $2 + 1; }
  ;




param_list:
    /* empty */
  | param_decls
  ;

param_decls:
    type_name IDENTIFIER
  | param_decls ',' type_name IDENTIFIER
  ;

type_name:
    TYPENAME
  ;

top_level_stmt:
    block
  | decl_stmt
  | expr_stmt
  ;

block:
    '{' statement_list '}'
  ;

statement_list:
    /* empty */
  | statement_list statement
  ;

statement:
    matched_stmt
  | unmatched_stmt
  ;

matched_stmt:
    IF '(' expr ')' matched_stmt ELSE matched_stmt
  | WHILE '(' expr ')' matched_stmt
  | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' matched_stmt
  | block
  | decl_stmt
  | expr_stmt
  | RETURN opt_expr ';'
  ;

unmatched_stmt:
    IF '(' expr ')' statement
  | IF '(' expr ')' matched_stmt ELSE unmatched_stmt
  | WHILE '(' expr ')' unmatched_stmt
  | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' unmatched_stmt
  ;

opt_address:
    /* empty */
  | '@' INTEGER
  ;

decl_stmt:
    type_name IDENTIFIER opt_array_dim opt_address ';'
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN expr ';'
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN array_initializer ';'
  ;

opt_array_dim:
    /* empty */
  | '[' expr ']'
  ;

array_initializer:
    '{' expr_list '}'
  ;

expr_list:
    expr
  | expr_list ',' expr
  ;

expr_stmt:
    expr ';'
  | ';'
  ;

opt_expr:
    /* empty */
  | expr
  ;


expr: assignment_expr ;


assignment_expr:
    logical_or_expr
  | logical_or_expr '?' expr ':' assignment_expr
  | assignable ASSIGN assignment_expr
  | assignable ADD_ASSIGN assignment_expr
  | assignable SUB_ASSIGN assignment_expr
  | assignable MUL_ASSIGN assignment_expr
  | assignable DIV_ASSIGN assignment_expr
  | assignable MOD_ASSIGN assignment_expr
  | assignable AND_ASSIGN assignment_expr
  | assignable OR_ASSIGN assignment_expr
  | assignable XOR_ASSIGN assignment_expr
  | assignable LSHIFT_ASSIGN assignment_expr
  | assignable RSHIFT_ASSIGN assignment_expr
  ;

assignable:
    IDENTIFIER
  | postfix_expr '[' expr ']'
  | '*' unary_expr
  ;

logical_or_expr:
    logical_and_expr
  | logical_or_expr OR logical_and_expr
  ;

logical_and_expr:
    bitwise_or_expr
  | logical_and_expr AND bitwise_or_expr
  ;

bitwise_or_expr:
    bitwise_xor_expr
  | bitwise_or_expr '|' bitwise_xor_expr
  ;

bitwise_xor_expr:
    bitwise_and_expr
  | bitwise_xor_expr '^' bitwise_and_expr
  ;

bitwise_and_expr:
    equality_expr
  | bitwise_and_expr '&' equality_expr
  ;

equality_expr:
    relational_expr
  | equality_expr EQ relational_expr
  | equality_expr NE relational_expr
  ;

relational_expr:
    shift_expr
  | relational_expr '<' shift_expr
  | relational_expr '>' shift_expr
  | relational_expr LE shift_expr
  | relational_expr GE shift_expr
  ;

shift_expr:
    additive_expr
  | shift_expr LSHIFT additive_expr
  | shift_expr RSHIFT additive_expr
  ;

additive_expr:
    multiplicative_expr
  | additive_expr '+' multiplicative_expr
  | additive_expr '-' multiplicative_expr
  ;

multiplicative_expr:
    unary_expr
  | multiplicative_expr '*' unary_expr
  | multiplicative_expr '/' unary_expr
  | multiplicative_expr '%' unary_expr
  ;

unary_expr:
    postfix_expr
  | '-' unary_expr
  | '&' unary_expr
  | '*' unary_expr
  | INC unary_expr
  | DEC unary_expr
  ;

postfix_expr:
    primary_expr
  | postfix_expr INC
  | postfix_expr DEC
  | postfix_expr '[' expr ']'
  | IDENTIFIER '(' arg_list ')'
  ;

primary_expr:
    IDENTIFIER
  | INTEGER opt_annotation
  | FLOAT opt_annotation
  | STRING
  | '(' expr ')'
  ;

opt_annotation:
    /* empty */
  | '#' type_name
  ;

arg_list:
    /* empty */
  | expr_args
  ;

expr_args:
    expr
  | expr_args ',' expr
  ;

%%

extern int yylineno;
extern char* yytext;

void yyerror(const char *s) {
    fprintf(stderr, "Syntax error at line %d: %s (near '%s')\n", yylineno, s, yytext);
}

