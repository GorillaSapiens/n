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
      const char *strval;
   };

   int count;
   struct ASTNode *children[];
} ASTNode;

#define is_empty(x) ((x)->kind == AST_EMPTY)

ASTNode *make_node(const char *name, ...);

ASTNode *append_child(ASTNode *parent, ASTNode *child);
ASTNode *prepend_child(ASTNode *parent, ASTNode *child);

ASTNode *make_integer_leaf(const char *intval);
ASTNode *increment_integer_leaf(ASTNode *node);
ASTNode *make_string_leaf(const char *strval);
ASTNode *make_identifier_leaf(const char *strval);
ASTNode *make_typename_leaf(const char *strval);
ASTNode *make_float_leaf(const char *dval);
ASTNode *make_empty_leaf(void);
char *make_negative(const char *p);

void dump_ast_flat(const ASTNode *node,
                   const char *prefix,
                   int is_last,
                   const char *parent_name);

void parse_dump(void);
void parse_dump_node(const ASTNode *node);

extern ASTNode *root;

#define MAKE_NODE(...) make_node(yysymbol_name(yyr1[yyn]), __VA_ARGS__, NULL)
#define MAKE_NAMED_NODE(name, ...) make_node(name, __VA_ARGS__, NULL)

#endif // _INCLUDE_AST_H_
