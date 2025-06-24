LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -g
#CFLAGS = -Wall -O2
#CFLAGS = -Wall -g -DYYDEBUG
TARGET = nc

GENSRC = parser.tab.c lex.yy.c
GENHDR = delexer.h

SRCSRC = \
   ast.c \
   compile.c \
   integer.c \
   md5.c \
   md5seen.c \
   messages.c \
   set.c \
   typename.c \
   main.c

SRC = $(SRCSRC) $(GENSRC)
OBJ = $(SRC:.c=.o)

all:
	@make --no-print-directory depend
	@make --no-print-directory $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

delexer.h: lexer.l delexer.pl
	./delexer.pl > delexer.h

depend: $(GENSRC) $(GENHDR)
	$(CC) $(CFLAGS) -MM $(SRC) > depend.mk

clean:
	rm -f $(TARGET) *.o delexer.h parser.tab.c parser.tab.h lex.yy.c depend.mk

-include depend.mk

