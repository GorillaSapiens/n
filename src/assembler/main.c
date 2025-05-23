#include <stdio.h>
#include <stdlib.h>
#include "symbols.h"

extern int yyparse();
extern FILE *yyin;
extern char* current_file;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input file>\n", argv[0]);
        return 1;
    }

    FILE *infile = fopen(argv[1], "r");
    if (!infile) {
        perror("fopen");
        return 1;
    }

    current_file = argv[1];  // Add this line

    yyin = infile;
    init_symbols();
    int ret = yyparse();
    fclose(infile);
    dump_output();
    return ret;
}
