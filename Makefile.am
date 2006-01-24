@SET_MAKE@

SUBDIRS = \
	doc \
	panel-plugin \
	pixmaps \
	po

$(top_srcdir)/configure: $(top_srcdir)/configure.ac
	[ -x $(top_srcdir)/autogen.sh ] && cd $(top_srcdir) && NOCONFIGURE=1 ./autogen.sh || /bin/true

html:
	$(MAKE) -C doc html

EXTRA_DIST = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update
