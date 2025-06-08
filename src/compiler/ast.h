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

#endif
