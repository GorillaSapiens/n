all: tools unit e2e

GENERATED_FLOAT_ARCHIVE_FIXTURES = \
	test/generated_float_archive_gf32_decls.n \
	test/generated_float_archive_gf32_operator_add.n \
	test/generated_float_archive_gf32_operator_div.n \
	test/generated_float_archive_gf32_operator_eq.n \
	test/generated_float_archive_gf32_operator_ge.n \
	test/generated_float_archive_gf32_operator_lt.n \
	test/generated_float_archive_gf32_operator_ne.n \
	test/generated_float_archive_gf3be_decls.n \
	test/generated_float_archive_gf3be_operator_mul.n

generated_float_archive_fixtures:
	tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' 0 1 2 3 15; \
	perl libraries/float/gen.pl --build "$$tmpdir/gf32" gf32 little 4 8; \
	cp "$$tmpdir/gf32/gf32_decls.n" test/generated_float_archive_gf32_decls.n; \
	cp "$$tmpdir/gf32/gf32_operator_add.n" test/generated_float_archive_gf32_operator_add.n; \
	cp "$$tmpdir/gf32/gf32_operator_div.n" test/generated_float_archive_gf32_operator_div.n; \
	cp "$$tmpdir/gf32/gf32_operator_eq.n" test/generated_float_archive_gf32_operator_eq.n; \
	cp "$$tmpdir/gf32/gf32_operator_ge.n" test/generated_float_archive_gf32_operator_ge.n; \
	cp "$$tmpdir/gf32/gf32_operator_lt.n" test/generated_float_archive_gf32_operator_lt.n; \
	cp "$$tmpdir/gf32/gf32_operator_ne.n" test/generated_float_archive_gf32_operator_ne.n; \
	perl libraries/float/gen.pl --build "$$tmpdir/gf3be" gf3be big 3 7; \
	cp "$$tmpdir/gf3be/gf3be_decls.n" test/generated_float_archive_gf3be_decls.n; \
	cp "$$tmpdir/gf3be/gf3be_operator_mul.n" test/generated_float_archive_gf3be_operator_mul.n

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

unit: generated_float_archive_fixtures
	( cd ./test ; ./test.pl --compile-only )

sieve:
	./driver/n65driver -I test test/sieve.n -o sieve.hex
	simulator/n65sim sieve.hex | head

e2e: generated_float_archive_fixtures
	( cd ./test ; ./test.pl --e2e-only )

test: unit e2e

.PHONY: all generated_float_archive_fixtures tools tarball unit sieve e2e test
