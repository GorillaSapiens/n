PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/n
INCLUDEDIR ?= $(PREFIX)/include/n
DATADIR ?= $(PREFIX)/share/n
PACKAGE_PREFIX ?= /usr/local
PACKAGE_STAGING ?= $(CURDIR)/pkgroot
INSTALLCHECK_STAGING ?= $(CURDIR)/.installcheck-root

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

install: tools install-core

install-core:
	@$(MAKE) --no-print-directory -C ./assembler install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./linker install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./archiver install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./compiler install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./simulator install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./driver install DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./libraries/nlib install DESTDIR="$(DESTDIR)" LIBDIR="$(LIBDIR)" INCLUDEDIR="$(INCLUDEDIR)" DATADIR="$(DATADIR)"
	@$(MAKE) --no-print-directory -C ./libraries/nint install DESTDIR="$(DESTDIR)" LIBDIR="$(LIBDIR)" DATADIR="$(DATADIR)"
	@$(MAKE) --no-print-directory install-data DESTDIR="$(DESTDIR)" DATADIR="$(DATADIR)"

install-data:
	install -d $(DESTDIR)$(DATADIR)/float
	install -m 0755 libraries/float/gen.pl $(DESTDIR)$(DATADIR)/float/gen.pl
	install -m 0644 libraries/float/README.md $(DESTDIR)$(DATADIR)/float/README.md
	install -d $(DESTDIR)$(DATADIR)/vcs
	install -m 0644 libraries/vcs/README.md $(DESTDIR)$(DATADIR)/vcs/README.md
	install -m 0644 libraries/vcs/riot.n $(DESTDIR)$(DATADIR)/vcs/riot.n
	install -m 0644 libraries/vcs/tia.n $(DESTDIR)$(DATADIR)/vcs/tia.n
	install -m 0644 libraries/vcs/vcs.n $(DESTDIR)$(DATADIR)/vcs/vcs.n
	install -m 0644 libraries/vcs/vcs_4k.cfg $(DESTDIR)$(DATADIR)/vcs/vcs_4k.cfg
	install -d $(DESTDIR)$(DATADIR)/vcs/batari-basic
	install -m 0644 libraries/vcs/batari-basic/LICENSE.txt $(DESTDIR)$(DATADIR)/vcs/batari-basic/LICENSE.txt
	install -m 0644 libraries/vcs/batari-basic/OMITTED-UPSTREAM-ARTIFACTS.txt $(DESTDIR)$(DATADIR)/vcs/batari-basic/OMITTED-UPSTREAM-ARTIFACTS.txt
	install -m 0644 libraries/vcs/batari-basic/README.md $(DESTDIR)$(DATADIR)/vcs/batari-basic/README.md

uninstall:
	@$(MAKE) --no-print-directory uninstall-data DESTDIR="$(DESTDIR)" DATADIR="$(DATADIR)"
	@$(MAKE) --no-print-directory -C ./libraries/nint uninstall DESTDIR="$(DESTDIR)" LIBDIR="$(LIBDIR)" DATADIR="$(DATADIR)"
	@$(MAKE) --no-print-directory -C ./libraries/nlib uninstall DESTDIR="$(DESTDIR)" LIBDIR="$(LIBDIR)" INCLUDEDIR="$(INCLUDEDIR)" DATADIR="$(DATADIR)"
	@$(MAKE) --no-print-directory -C ./driver uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./simulator uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./compiler uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./archiver uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./linker uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"
	@$(MAKE) --no-print-directory -C ./assembler uninstall DESTDIR="$(DESTDIR)" BINDIR="$(BINDIR)"

uninstall-data:
	rm -f $(DESTDIR)$(DATADIR)/float/gen.pl
	rm -f $(DESTDIR)$(DATADIR)/float/README.md
	rm -f $(DESTDIR)$(DATADIR)/vcs/README.md
	rm -f $(DESTDIR)$(DATADIR)/vcs/riot.n
	rm -f $(DESTDIR)$(DATADIR)/vcs/tia.n
	rm -f $(DESTDIR)$(DATADIR)/vcs/vcs.n
	rm -f $(DESTDIR)$(DATADIR)/vcs/vcs_4k.cfg
	rm -f $(DESTDIR)$(DATADIR)/vcs/batari-basic/LICENSE.txt
	rm -f $(DESTDIR)$(DATADIR)/vcs/batari-basic/OMITTED-UPSTREAM-ARTIFACTS.txt
	rm -f $(DESTDIR)$(DATADIR)/vcs/batari-basic/README.md

package: tools
	rm -rf $(PACKAGE_STAGING)
	$(MAKE) --no-print-directory install-core DESTDIR="$(PACKAGE_STAGING)" PREFIX="$(PACKAGE_PREFIX)" BINDIR="$(PACKAGE_PREFIX)/bin" LIBDIR="$(PACKAGE_PREFIX)/lib/n" INCLUDEDIR="$(PACKAGE_PREFIX)/include/n" DATADIR="$(PACKAGE_PREFIX)/share/n"
	tar -C $(PACKAGE_STAGING) -czf ./n.install.`date -u "+%Y-%m-%dT%H:%M:%SZ"`.tar.gz .

installcheck: tools
	rm -rf $(INSTALLCHECK_STAGING)
	$(MAKE) --no-print-directory install-core DESTDIR="$(INSTALLCHECK_STAGING)" PREFIX="/usr/local" BINDIR="/usr/local/bin" LIBDIR="/usr/local/lib/n" INCLUDEDIR="/usr/local/include/n" DATADIR="/usr/local/share/n"
	stage_bin="$(INSTALLCHECK_STAGING)/usr/local/bin"; 	"$$stage_bin/n65driver" -print-prog-name=cc1 >/dev/null; 	"$$stage_bin/n65driver" -print-prog-name=as >/dev/null; 	"$$stage_bin/n65driver" -I "$(CURDIR)/test" "$(CURDIR)/test/sieve.n" -o "$(INSTALLCHECK_STAGING)/sieve.hex"; 	"$$stage_bin/n65sim" "$(INSTALLCHECK_STAGING)/sieve.hex" | head -n 1 >/dev/null

tarball:
	-git clean -fdx
	make
	make ctar

ctar:
	rm -f n.*.gz
	tar -czf n.`date "+%Y%m%d_%H%M%S"`.tar.gz *

unit:
	@$(MAKE) --no-print-directory -C ./test unit

sieve:
	./driver/n65driver -I test test/sieve.n -o sieve.hex
	simulator/n65sim sieve.hex | head

e2e:
	@$(MAKE) --no-print-directory -C ./test e2e

test: unit e2e

.PHONY: all generated_float_archive_fixtures tools install install-core install-data uninstall uninstall-data package installcheck tarball unit sieve e2e test
