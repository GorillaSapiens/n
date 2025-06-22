LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -g
#CFLAGS = -Wall -O2
#CFLAGS = -Wall -g -DYYDEBUG
TARGET = nc

GENSRC = parser.tab.c lex.yy.c
SRCSRC = main.c ast.c check.c messages.c
SRC = $(SRCSRC) $(GENSRC)
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

depend: $(GENSRC)
	$(CC) $(CFLAGS) -MM $(SRC) > depend.mk

clean:
	rm -f $(TARGET) *.o parser.tab.c parser.tab.h lex.yy.c depend.mk

-include depend.mk

