#
# Mailwatch plugin
#
plugindir = $(libdir)/xfce4/panel-plugins

plugin_LTLIBRARIES = \
	libmailwatch-plugin.la

libmailwatch_plugin_la_SOURCES = \
	mailwatch-plugin.c

libmailwatch_plugin_la_DEPENDENCIES = \
	$(top_builddir)/libmailwatch-core/libmailwatch-core.la

libmailwatch_plugin_la_CFLAGS = \
	-I$(top_srcdir)/libmailwatch-core \
	@GTHREAD_CFLAGS@ \
	@LIBXFCE4PANEL_CFLAGS@ \
	@LIBXFCE4UI_CFLAGS@ \
	-DDATADIR=\"$(datadir)\" \
	-DLOCALEDIR=\"$(localedir)\"

libmailwatch_plugin_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined \
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)'

libmailwatch_plugin_la_LIBADD = \
	$(top_builddir)/libmailwatch-core/libmailwatch-core.la \
	@GTHREAD_LIBS@ \
	@LIBXFCE4PANEL_LIBS@ \
	@LIBXFCE4UI_LIBS@ \
	@GNUTLS_LIBS@ \
	@LIBGCRYPT_LIBS@

#
# Desktop file
#
desktopdir = $(datadir)/xfce4/panel/plugins
desktop_DATA = mailwatch.desktop
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = \
	mailwatch.desktop.in

DISTCLEANFILES = \
	$(desktop_DATA)
