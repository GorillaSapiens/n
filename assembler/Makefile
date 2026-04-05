LEX = flex
YACC = bison -Werror=conflicts-sr,conflicts-rr
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -MMD -MP

# for debugging
CFLAGS += -g

# for yacc debugging
#CFLAGS += -DYYDEBUG

# for optimization
#CFLAGS += -O2

TARGET = n65asm

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755
INSTALL_DATA ?= $(INSTALL) -m 0644

GENSRC = parser.tab.c lex.yy.c
GENHDR = parser.tab.h

SRCSRC = \
   asm_pass.c \
   directive.c \
   expr.c \
   ihex.c \
   ir.c \
   listing.c \
   o65.c \
   opcode.c \
   source_loader.c \
   symtab.c \
   util.c \
   main.c

SRC = $(SRCSRC) $(GENSRC)
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

all:
	@make --no-print-directory $(TARGET)

$(TARGET): $(GENHDR) $(GENSRC) $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

parser.tab.c parser.tab.h: parser.y
	$(YACC) -v -d -W --report=all parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

tarball:
	tar -czf n65asm.`date "+%Y%m%d_%H%M%S"`.tar.gz *

tidy:
	rm -f $(TARGET) nid $(OBJ) $(DEP) *.o *.d *.gz
	
install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	$(INSTALL_DATA) default.cfg $(DESTDIR)$(BINDIR)/default.cfg
	$(INSTALL_DATA) illegals.cfg $(DESTDIR)$(BINDIR)/illegals.cfg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/default.cfg
	rm -f $(DESTDIR)$(BINDIR)/illegals.cfg

clean: tidy
	rm -f $(GENSRC) $(GENHDR) parser.output

-include $(DEP)

