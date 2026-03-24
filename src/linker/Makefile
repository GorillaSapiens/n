CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2

all: nl

nl: nl.c
	$(CC) $(CFLAGS) nl.c -o nl

tarball:
	rm *.gz
	tar -czf nl.`date "+%Y%m%d_%H%M%S"`.tar.gz *

clean:
	rm -f nl
