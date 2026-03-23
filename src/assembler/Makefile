LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -MMD -MP
CFLAGS += -g        # for debugging
#CFLAGS += -DYYDEBUG # for yacc debugging
#CFLAGS += -O2       # for obtimization

TARGET = na

GENSRC = parser.tab.c lex.yy.c
GENHDR = parser.tab.h

SRCSRC = \
   asm_pass.c \
   directive.c \
   expr.c \
   ir.c \
   symtab.c \
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

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)
	rm -f $(GENSRC) $(GENHDR) parser.output

-include $(DEP)

