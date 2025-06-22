LEX = flex
YACC = bison
CC = gcc
CFLAGS = -Wall -g
#CFLAGS = -Wall -O2
#CFLAGS = -Wall -g -DYYDEBUG
TARGET = nc

OBJ = \
   main.o \
   parser.tab.o \
   lex.yy.o \
   ast.o \
   check.o \
   messages.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lfl

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.c: lexer.l
	$(LEX) lexer.l

clean:
	rm -f $(TARGET) parser.tab.c parser.tab.h lex.yy.c
