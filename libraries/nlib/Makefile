ASM=../../assembler/n65asm
AR=../../archiver/n65ar

DESTDIR ?=
LIBDIR ?= /usr/local/lib/n
INCLUDEDIR ?= /usr/local/include/n
DATADIR ?= /usr/local/share/n
INSTALL ?= install
INSTALL_DATA ?= $(INSTALL) -m 0644

all:
	rm -rf wrk nlib.a
	mkdir wrk
	./gen_weakops.pl
	./unbundle.pl
	cp *.s wrk
	for each in `ls wrk/*.s`; do stem=`basename $$each .s`; echo ===== $$each ; $(ASM) -o wrk/$$stem.o65 $$each; done
	$(AR) rcs nlib.a65 wrk/*.o65

install: all
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)/nlib
	$(INSTALL_DATA) nlib.a65 $(DESTDIR)$(LIBDIR)/nlib.a65
	$(INSTALL_DATA) nlib.h $(DESTDIR)$(INCLUDEDIR)/nlib.h
	$(INSTALL_DATA) nlib.inc $(DESTDIR)$(INCLUDEDIR)/nlib.inc
	$(INSTALL_DATA) n.cfg $(DESTDIR)$(DATADIR)/nlib/n.cfg
	$(INSTALL_DATA) nrt0.s $(DESTDIR)$(DATADIR)/nlib/nrt0.s
	$(INSTALL_DATA) nrt0_noint.s $(DESTDIR)$(DATADIR)/nlib/nrt0_noint.s
	$(INSTALL_DATA) nlib_zeropage.s $(DESTDIR)$(DATADIR)/nlib/nlib_zeropage.s
	$(INSTALL_DATA) README.md $(DESTDIR)$(DATADIR)/nlib/README.md
	$(INSTALL_DATA) LICENSE $(DESTDIR)$(DATADIR)/nlib/LICENSE

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/nlib.a65
	rm -f $(DESTDIR)$(INCLUDEDIR)/nlib.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/nlib.inc
	rm -f $(DESTDIR)$(DATADIR)/nlib/n.cfg
	rm -f $(DESTDIR)$(DATADIR)/nlib/nrt0.s
	rm -f $(DESTDIR)$(DATADIR)/nlib/nrt0_noint.s
	rm -f $(DESTDIR)$(DATADIR)/nlib/nlib_zeropage.s
	rm -f $(DESTDIR)$(DATADIR)/nlib/README.md
	rm -f $(DESTDIR)$(DATADIR)/nlib/LICENSE

clean:
	rm -f *.o *.bin tests nlib.a65 wrk/*
