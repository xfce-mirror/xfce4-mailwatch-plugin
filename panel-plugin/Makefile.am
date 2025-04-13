AM_CPPFLAGS = \
	-I${top_srcdir} \
	-DG_LOG_DOMAIN=\"libmailwatch\" \
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\" \
	$(PLATFORM_CPPFLAGS)
#
# Mailwatch plugin
#
plugindir = $(libdir)/xfce4/panel/plugins

plugin_LTLIBRARIES = \
	libmailwatch.la

libmailwatch_la_SOURCES = \
	mailwatch-plugin.c

libmailwatch_la_DEPENDENCIES = \
	$(top_builddir)/libmailwatch-core/libmailwatch-core.la

libmailwatch_la_CFLAGS = \
	-I$(top_srcdir)/libmailwatch-core \
	$(GTHREAD_CFLAGS) \
	$(LIBXFCE4PANEL_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(EXO_CFLAGS) \
	$(PLATFORM_CFLAGS)

libmailwatch_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined \
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)' \
	$(PLATFORM_LDFLAGS)

libmailwatch_la_LIBADD = \
	$(top_builddir)/libmailwatch-core/libmailwatch-core.la \
	$(GTHREAD_LIBS) \
	$(LIBXFCE4PANEL_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(EXO_LIBS)

if HAVE_SSL_SUPPORT
libmailwatch_la_LIBADD += \
	$(GNUTLS_LIBS)
endif

#
# Desktop file
#
desktopdir = $(datadir)/xfce4/panel/plugins
desktop_DATA = mailwatch.desktop
$(desktop_DATA): $(desktop_DATA).in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST = \
	mailwatch.desktop.in \
	meson.build \
	$(NULL)

DISTCLEANFILES = \
	$(desktop_DATA)
