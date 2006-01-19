SUBDIRS = \
	images

noinst_DATA = xfce4-mailwatch-plugin.xml

docdir = $(datadir)/xfce4/doc/C
doc_DATA = xfce4-mailwatch-plugin.html

EXTRA_DIST = $(noinst_DATA) $(doc_DATA)

html: $(srcdir)/xfce4-mailwatch-plugin.xml
	xsltproc $(top_srcdir)/doc/xfce-nochunk.xsl \
	    $(srcdir)/xfce4-mailwatch-plugin.xml
