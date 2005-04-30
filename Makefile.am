SUBDIRS = \
	panel-plugin \
	po

$(top_srcdir)/configure: $(top_srcdir)/configure.ac
	[ -x $(top_srcdir)/autogen.sh ] && cd $(top_srcdir) && NOCONFIGURE=1 ./autogen.sh || /bin/true
