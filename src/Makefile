LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -O2
TARGET = 8c

all: $(TARGET)

$(TARGET): main.c mprintf.c parser.tab.c lex.yy.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c mprintf.c parser.tab.c lex.yy.c -lfl

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

clean:
	rm -f $(TARGET) parser.tab.c parser.tab.h lex.yy.c
