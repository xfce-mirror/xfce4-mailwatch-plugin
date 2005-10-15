@SET_MAKE@

SUBDIRS = \
	panel-plugin \
	pixmaps \
	po

$(top_srcdir)/configure: $(top_srcdir)/configure.ac
	[ -x $(top_srcdir)/autogen.sh ] && cd $(top_srcdir) && NOCONFIGURE=1 ./autogen.sh || /bin/true

html:
	$(MAKE) -C doc html

dist-bz2: dist
	zcat $(PACKAGE)-$(VERSION).tar.gz | bzip2 --best -c > $(PACKAGE)-$(VERSION).tar.bz2

distcheck-bz2: distcheck
	zcat $(PACKAGE)-$(VERSION).tar.gz | bzip2 --best -c > $(PACKAGE)-$(VERSION).tar.bz2

EXTRA_DIST = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update
