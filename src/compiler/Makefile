LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -MMD -MP
CFLAGS += -g        # for debugging
#CFLAGS += -DYYDEBUG # for yacc debugging
#CFLAGS += -O2       # for obtimization

TARGET = nc
TEST   = test

GENSRC = parser.tab.c lex.yy.c
GENHDR = delexer.h coverage_map.h

SRCSRC = \
   ast.c \
   compile.c \
   coverage.c \
   emit.c \
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
	(cd ../test; ./test.pl)

$(TARGET): $(GENHDR) $(GENSRC) $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl

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

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)
	rm -f delexer.h parser.tab.c parser.tab.h lex.yy.c parser.output

-include $(DEP)

