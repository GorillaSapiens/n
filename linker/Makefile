CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755

OBJS = n65ld.o n65ld_input.o n65ld_abi.o
DEPS = $(OBJS:.o=.d)

all: n65ld

n65ld: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o n65ld

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

install: n65ld
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) n65ld $(DESTDIR)$(BINDIR)/n65ld

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/n65ld

tarball:
	rm *.gz
	tar -czf n65ld.`date "+%Y%m%d_%H%M%S"`.tar.gz *

clean:
	rm -f n65ld $(OBJS) $(DEPS)

-include $(DEPS)
