CC ?= cc
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra -Werror -pedantic

all: nar

nar: nar.c
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ nar.c

clean:
	rm -f nar
