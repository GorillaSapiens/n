CC ?= cc
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra -Werror -pedantic

all: n65ar

n65ar: n65ar.c
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ n65ar.c

clean:
	rm -f n65ar
