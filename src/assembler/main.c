#include <stdio.h>

int yyparse(void);

int main(void)
{
   int rc = yyparse();
   printf("yyparse returned %d\n", rc);
   return rc;
}
