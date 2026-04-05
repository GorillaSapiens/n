CC ?= cc
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra -Werror -pedantic

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755

all: n65ar

n65ar: n65ar.c
	$(CC) $(CFLAGS) $(WARNINGS) -o $@ n65ar.c

install: n65ar
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) n65ar $(DESTDIR)$(BINDIR)/n65ar

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/n65ar

clean:
	rm -f n65ar
