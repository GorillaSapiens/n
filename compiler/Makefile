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

TARGET = n65cc

BINDIR ?= /usr/local/bin
DESTDIR ?=
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 0755
INSTALL_DATA ?= $(INSTALL) -m 0644
TEST   = test

GENSRC = parser.tab.c lex.yy.c
GENHDR = parser.tab.h delexer.h coverage_map.h

SRCSRC = \
   ast.c \
   compile.c \
   coverage.c \
   emit.c \
   enumname.c \
   expropt.c \
   float.c \
   integer.c \
   md5.c \
   md5seen.c \
   memname.c \
   messages.c \
   pair.c \
   set.c \
   typename.c \
   xform.c \
   xray.c \
   main.c

SRC = $(SRCSRC) $(GENSRC)
OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

all:
	@make --no-print-directory $(TARGET)
	@make --no-print-directory $(TEST)

part:
	@make --no-print-directory clean
	@make --no-print-directory $(TARGET)

test:
	(cd ../test; ./test.pl --compile-only)

$(TARGET): $(GENHDR) $(GENSRC) $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl -lm

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

parser.tab.c parser.tab.h: parser.y
	./check_parser.pl parser.y
	$(YACC) -v -d -W --report=all parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

delexer.h: lexer.l delexer.pl
	./delexer.pl > delexer.h

coverage_map.h: parser.y coverage.pl
	./coverage.pl > coverage_map.h

install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)
	rm -f $(GENSRC) $(GENHDR) parser.output

-include $(DEP)

