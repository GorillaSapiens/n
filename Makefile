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

e2e:
	( cd ./test ; ./e2e.pl )

test: unit e2e
