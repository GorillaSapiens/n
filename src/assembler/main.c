#include <stdio.h>
#include "symbols.h"

extern int yyparse();

int main() {
    init_symbols();
    int ret = yyparse();
    dump_output();
    return ret;
}
