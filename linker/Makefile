CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755

all: n65ld

n65ld: n65ld.c
	$(CC) $(CFLAGS) n65ld.c -o n65ld

install: n65ld
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) n65ld $(DESTDIR)$(BINDIR)/n65ld

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/n65ld

tarball:
	rm *.gz
	tar -czf n65ld.`date "+%Y%m%d_%H%M%S"`.tar.gz *

clean:
	rm -f n65ld
