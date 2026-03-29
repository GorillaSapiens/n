CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2

all: n65ld

n65ld: n65ld.c
	$(CC) $(CFLAGS) n65ld.c -o n65ld

tarball:
	rm *.gz
	tar -czf n65ld.`date "+%Y%m%d_%H%M%S"`.tar.gz *

clean:
	rm -f n65ld
