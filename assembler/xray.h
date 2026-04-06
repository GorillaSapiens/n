#ifndef ASM_XRAY_H
#define ASM_XRAY_H

#define ASM_XRAY_PASSES 0

int assembler_lookup_xray(const char *name);
void assembler_set_xray(int n);
int assembler_get_xray(int n);

#endif
