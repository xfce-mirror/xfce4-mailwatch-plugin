plugindir = @XFCE4_PANEL_PLUGINSDIR@
plugin_LTLIBRARIES = libmailwatch.la

libmailwatch_la_LDFLAGS = \
	-avoid-version \
	-export-dynamic \
	-module

if HAVE_CYGWIN
  libmailwatch_la_LDFLAGS += \
	-no-undefined \
	-export-symbols $(datadir)/xfce4/devel/panel.def
endif

libmailwatch_la_SOURCES = \
	mailwatch-mailbox-imap.c \
	mailwatch-mailbox-maildir.c \
	mailwatch-mailbox-mbox.c \
	mailwatch-mailbox-mh.c \
	mailwatch-mailbox.h \
	mailwatch-plugin.c \
	mailwatch-utils.c \
	mailwatch-utils.h \
	mailwatch.c \
	mailwatch.h

libmailwatch_la_CFLAGS = \
	@GTHREAD_CFLAGS@ \
	@XFCE4_PANEL_CFLAGS@ \
	@GNUTLS_CFLAGS@ \
	-DDATADIR=\"$(datadir)\" \
	-DLOCALEDIR=\"$(localedir)\"

libmailwatch_la_LIBADD = \
	@GTHREAD_LIBS@ \
	@XFCE4_PANEL_LIBS@ \
	@GNUTLS_LIBS@
