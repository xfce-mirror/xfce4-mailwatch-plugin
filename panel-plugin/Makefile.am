# make sure both are distributed, regardless of which condition is taken
EXTRA_DIST = mailwatch-plugin.c mailwatch-plugin-4.2.c

if BUILD_42_PANEL_PLUGIN

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
	mailwatch-mailbox-pop3.c \
	mailwatch-mailbox.h \
	mailwatch-plugin-4.2.c \
	mailwatch-utils.c \
	mailwatch-utils.h \
	mailwatch.c \
	mailwatch.h

if HAVE_SSL_SUPPORT
libmailwatch_la_SOURCES += mailwatch-mailbox-gmail.c
endif

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

EXTRA_DIST = mailwatch-mailbox-gmail.c

else

plugindir = $(libexecdir)/xfce4/panel-plugins
plugin_PROGRAMS = xfce4-mailwatch-plugin

xfce4_mailwatch_plugin_SOURCES = \
	mailwatch-mailbox-imap.c \
	mailwatch-mailbox-maildir.c \
	mailwatch-mailbox-mbox.c \
	mailwatch-mailbox-mh.c \
	mailwatch-mailbox-pop3.c \
	mailwatch-mailbox.h \
	mailwatch-plugin.c \
	mailwatch-utils.c \
	mailwatch-utils.h \
	mailwatch.c \
	mailwatch.h

if HAVE_SSL_SUPPORT
xfce4_mailwatch_plugin_SOURCES += mailwatch-mailbox-gmail.c
endif

xfce4_mailwatch_plugin_CFLAGS = \
	@GTHREAD_CFLAGS@ \
	@LIBXFCE4PANEL_CFLAGS@ \
	@GNUTLS_CFLAGS@ \
	-DDATADIR=\"$(datadir)\" \
	-DLOCALEDIR=\"$(localedir)\"

xfce4_mailwatch_plugin_LDFLAGS = \
	@GTHREAD_LIBS@ \
	@LIBXFCE4PANEL_LIBS@ \
	@GNUTLS_LIBS@

if HAVE_CYGWIN
  xfce4_mailwatch_plugin_LDFLAGS += \
	-no-undefined \
	-export-symbols $(datadir)/xfce4/devel/panel.def
endif

# .desktop file
#
# Some automake trickery here. Because we cannot use $(libexecdir) in the
# automake stage, we'll use sed to get the full path into the .desktop file.
# We also need to let intltool merge the translated fields, so we add an
# additional level of indirection: a <name>.desktop.in.in file.
# 
desktop_in_in_files = mailwatch.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in)
	
desktopdir = $(datadir)/xfce4/panel-plugins
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@
	
# get full path into .desktop file
%.desktop.in: %.desktop.in.in
	sed -e "s^@PLUGIN_PATH@^$(libexecdir)/xfce4/panel-plugins^" \
		$< > $@

EXTRA_DIST += \
	$(desktop_in_in_files) \
	mailwatch-mailbox-gmail.c

DISTCLEANFILES = \
	$(desktop_DATA) $(desktop_in_files)

endif
