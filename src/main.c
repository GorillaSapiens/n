#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Declare from parser.y
extern int yyparse(void);
extern void register_typename(const char* name, int size);

// Optional: reference to input file
extern FILE* yyin;

int main(int argc, char** argv) {
    // Optional: read from file
    if (argc > 1) {
        yyin = fopen(argv[1], "r");
        if (!yyin) {
            perror(argv[1]);
            return 1;
        }
    }

    // Register built-in types
    register_typename("int", 2);
    register_typename("char", 1);
    register_typename("void", 0);

    printf("Parsing...\n");
    if (yyparse() == 0) {
        printf("Parse successful.\n");
    } else {
        printf("Parse failed.\n");
    }

    return 0;
}
