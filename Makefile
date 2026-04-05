all: tools unit e2e

generated_float_archive_fixtures:
	@$(MAKE) --no-print-directory -C ./test generated_float_archive_fixtures

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
	-git clean -fdx
	make
	rm -f n_*.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

unit:
	@$(MAKE) --no-print-directory -C ./test unit

sieve:
	./driver/n65driver -I test test/sieve.n -o sieve.hex
	simulator/n65sim sieve.hex | head

e2e:
	@$(MAKE) --no-print-directory -C ./test e2e

test: unit e2e

.PHONY: all generated_float_archive_fixtures tools tarball unit sieve e2e test
