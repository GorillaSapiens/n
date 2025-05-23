all: assembler

assembler: lexer.o parser.o main.o symbols.o opcodes.o
	$(CC) -o assembler lexer.o parser.o main.o symbols.o opcodes.o -lfl

lexer.o: lexer.c parser.h
	$(CC) -c lexer.c

parser.o: parser.c
	$(CC) -c parser.c

main.o: main.c
	$(CC) -c main.c

symbols.o: symbols.c symbols.h
	$(CC) -c symbols.c

opcodes.o: opcodes.c opcodes.h
	$(CC) -c opcodes.c

parser.c parser.h: parser.y
	bison -d -o parser.c parser.y

lexer.c: lexer.l
	flex -o lexer.c lexer.l

clean:
	rm -f assembler lexer.c parser.c parser.h *.o output.bin

