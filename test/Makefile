GENERATED_FLOAT_ARCHIVE_FIXTURES = \
	generated_float_archive_gf32_decls.n \
	generated_float_archive_gf32_operator_add.n \
	generated_float_archive_gf32_operator_div.n \
	generated_float_archive_gf32_operator_eq.n \
	generated_float_archive_gf32_operator_ge.n \
	generated_float_archive_gf32_operator_gt.n \
	generated_float_archive_gf32_operator_le.n \
	generated_float_archive_gf32_operator_lt.n \
	generated_float_archive_gf32_operator_ne.n \
	generated_float_archive_gf3be_decls.n \
	generated_float_archive_gf3be_operator_mul.n

all: unit e2e

generated_float_archive_fixtures:
	tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' 0 1 2 3 15; \
	perl ../libraries/float/gen.pl --build "$$tmpdir/gf32" gf32 little 4 8; \
	cp "$$tmpdir/gf32/gf32_decls.n" generated_float_archive_gf32_decls.n; \
	cp "$$tmpdir/gf32/gf32_operator_add.n" generated_float_archive_gf32_operator_add.n; \
	cp "$$tmpdir/gf32/gf32_operator_div.n" generated_float_archive_gf32_operator_div.n; \
	cp "$$tmpdir/gf32/gf32_operator_eq.n" generated_float_archive_gf32_operator_eq.n; \
	cp "$$tmpdir/gf32/gf32_operator_ge.n" generated_float_archive_gf32_operator_ge.n; \
	cp "$$tmpdir/gf32/gf32_operator_gt.n" generated_float_archive_gf32_operator_gt.n; \
	cp "$$tmpdir/gf32/gf32_operator_le.n" generated_float_archive_gf32_operator_le.n; \
	cp "$$tmpdir/gf32/gf32_operator_lt.n" generated_float_archive_gf32_operator_lt.n; \
	cp "$$tmpdir/gf32/gf32_operator_ne.n" generated_float_archive_gf32_operator_ne.n; \
	perl ../libraries/float/gen.pl --build "$$tmpdir/gf3be" gf3be big 3 7; \
	cp "$$tmpdir/gf3be/gf3be_decls.n" generated_float_archive_gf3be_decls.n; \
	cp "$$tmpdir/gf3be/gf3be_operator_mul.n" generated_float_archive_gf3be_operator_mul.n

unit: generated_float_archive_fixtures
	./test.pl --compile-only

e2e: generated_float_archive_fixtures
	./test.pl --e2e-only

test: unit e2e

.PHONY: all generated_float_archive_fixtures unit e2e test
