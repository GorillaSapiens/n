LEX = flex
YACC = bison
CC = gcc
#CFLAGS = -Wall -O2
CFLAGS = -Wall -g
#CFLAGS = -Wall -g -DYYDEBUG
TARGET = nc

all: $(TARGET)

$(TARGET): main.c ast.c parser.tab.c lex.yy.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c ast.c parser.tab.c lex.yy.c -lfl

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

clean:
	rm -f $(TARGET) parser.tab.c parser.tab.h lex.yy.c
