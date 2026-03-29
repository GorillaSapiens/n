#ifndef SOURCE_LOADER_H
#define SOURCE_LOADER_H

#include <stdio.h>

void source_loader_add_include_dir(const char *dir);
void source_loader_clear_include_dirs(void);
FILE *source_loader_open_expanded(const char *root_path);

#endif
