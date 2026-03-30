all: tools unit e2e

tools:
	( cd ./assembler ; make clean ; make )
	( cd ./linker ; make clean ; make )
	( cd ./archiver ; make clean ; make )
	( cd ./libraries/nint ; make clean ; make )
	( cd ./libraries/nlib ; make clean ; make )
	( cd ./compiler ; make clean ; make )
	( cd ./simulator ; make clean ; make )
	( cd ./driver ; make clean ; make )

tarball:
	git clean -fdx
	make
	rm -f n_*.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

unit:
	( cd ./test ; ./test.pl )

sieve:
	./driver/n65driver -I test test/sieve.n -o sieve.hex
	simulator/n65sim sieve.hex | head

e2e:
	( cd ./test ; ./e2e.pl )

test: unit e2e
