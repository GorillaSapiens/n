CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755

all: n65driver

n65driver: n65driver.c
	$(CC) $(CFLAGS) n65driver.c -o n65driver

install: n65driver
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) n65driver $(DESTDIR)$(BINDIR)/n65driver

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/n65driver

clean:
	rm -f n65driver
