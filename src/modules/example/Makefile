.SUFFIXES: .lo .bundle .xml .xmlh .so

CC=$(shell noit-config --cc)
CPPFLAGS=$(shell noit-config --cppflags)
SHCFLAGS=$(shell noit-config --shcflags)
MODULELD=$(shell noit-config --moduleld)
SHLDFLAGS=$(shell noit-config --shldflags)
MODULEEXT=$(shell noit-config --moduleext)
MODULESDIR=$(shell noit-config --modules-dir)
XML2H=$(shell noit-config --xml2h)

.xml.xmlh:
	@$(XML2H) `echo $< | sed -e 's/\.xml$$//;'`_xml_description < $< > $@

MODULES=example.$(MODULEEXT)

all:	$(MODULES)

example.lo:	example.c example.xmlh
	$(CC) $(CPPFLAGS) $(SHCFLAGS) -o $@ -c $<

example.$(MODULEEXT):	example.lo
	$(MODULELD) $(SHLDFLAGS) -o $@ example.lo

install:	$(MODULES)
	for i in $(MODULES); do \
		cp $$i $(DESTDIR)$(MODULESDIR)/$$i; \
		chmod 755 $(DESTDIR)$(MODULESDIR)/$$i; \
	done
