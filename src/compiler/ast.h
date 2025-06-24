#ifndef _INCLUDE_AST_H_
#define _INCLUDE_AST_H_

enum ASTKind {
    AST_GENERIC = 0,
    AST_IDENTIFIER,
    AST_TYPENAME,
    AST_INTEGER,
    AST_FLOAT,
    AST_STRING,
    AST_EMPTY
};

typedef struct ASTNode {
   const char *name;
   const char *file;
   int line, column;
   enum ASTKind kind;

   union {
      unsigned long long intval;
      double dval;
      char *strval;
   };

   int count;
   struct ASTNode *children[16];
} ASTNode;

ASTNode *make_node(const char *name, ...);
ASTNode *make_integer_leaf(unsigned long long intval);
ASTNode *make_string_leaf(char *strval);
ASTNode *make_identifier_leaf(char *strval);
ASTNode *make_typename_leaf(char *strval);
ASTNode *make_float_leaf(double dval);
ASTNode *make_empty_leaf(void);

void dump_ast_flat(const ASTNode *node,
                   const char *prefix,
                   int is_last,
                   const char *parent_name);

void parse_dump(void);
void parse_dump_node(ASTNode *node);

extern ASTNode *root;

#define MAKE_NODE(...) make_node(yysymbol_name(yyr1[yyn]), __VA_ARGS__, NULL)
#define MAKE_NAMED_NODE(name, ...) make_node(name, __VA_ARGS__, NULL)

#endif // _INCLUDE_AST_H_
