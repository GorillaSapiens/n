LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -MMD -MP
CFLAGS += -g # for debugging
#CFLAGS += -DYYDEBUG # for yacc debugging
#CFLAGS += -O2 # for obtimization

TARGET = n65asm

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
   util.o \
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
	
clean: tidy
	rm -f $(GENSRC) $(GENHDR) parser.output

-include $(DEP)

