#ifndef N65LD_ABI_H
#define N65LD_ABI_H

#include "n65ld_internal.h"

int abi_metadata_has_prefix(const char *name);
void validate_abi_metadata(const input_set_t *in);

#endif
