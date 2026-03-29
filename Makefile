all: tools unit e2e

tools:
	( cd ./assembler ; make clean ; make )
	( cd ./linker ; make clean ; make )
	( cd ./archiver ; make clean ; make )
	( cd ./libraries/nint ; make clean ; make )
	( cd ./libraries/nlib ; make clean ; make )
	( cd ./compiler ; make clean ; make )
	( cd ./simulator ; make clean ; make )

tarball:
	git clean -fdx
	make
	rm -f n_*.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

unit:
	( cd ./test ; ./test.pl )

sieve:
	./compiler/n65cc -quiet -I test test/sieve.n -o sieve.s -dumpbase sieve.n -dumpbase-ext .n -dumpdir ./
	./assembler/n65asm -I libraries/nlib/ -o sieve.o65 sieve.s
	./linker/n65ld -o sieve.hex sieve.o65 libraries/nlib/nlib.a65
	simulator/n65sim sieve.hex  | head

e2e:
	( cd ./test ; ./e2e.pl )

test: unit e2e
