CC=gcc
CFLAGS=-g

all: assembler

assembler: lexer.o parser.o main.o symbols.o opcodes.o
	$(CC) $(CFLAGS) -o assembler lexer.o parser.o main.o symbols.o opcodes.o -lfl

lexer.o: lexer.c parser.h
	$(CC) $(CFLAGS) -c lexer.c

parser.o: parser.c
	$(CC) $(CFLAGS) -c parser.c

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

symbols.o: symbols.c symbols.h
	$(CC) $(CFLAGS) -c symbols.c

opcodes.o: opcodes.c opcodes.h
	$(CC) $(CFLAGS) -c opcodes.c

parser.c parser.h: parser.y
	bison -d -o parser.c parser.y

lexer.c: lexer.l
	flex -o lexer.c lexer.l

clean:
	rm -f assembler lexer.c parser.c parser.h *.o output.bin

