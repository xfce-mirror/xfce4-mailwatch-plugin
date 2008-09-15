# gmail is not always built
EXTRA_DIST = \
	mailwatch-mailbox-gmail.c

noinst_LTLIBRARIES = libmailwatch-core.la

libmailwatch_core_la_SOURCES = \
	mailwatch-common.c \
	mailwatch-common.h \
	mailwatch-mailbox-imap.c \
	mailwatch-mailbox-maildir.c \
	mailwatch-mailbox-mbox.c \
	mailwatch-mailbox-mh.c \
	mailwatch-mailbox-pop3.c \
	mailwatch-mailbox.h \
	mailwatch-net-conn.c \
	mailwatch-net-conn.h \
	mailwatch-utils.c \
	mailwatch-utils.h \
	mailwatch.c \
	mailwatch.h

if HAVE_SSL_SUPPORT
libmailwatch_core_la_SOURCES += mailwatch-mailbox-gmail.c
endif

libmailwatch_core_la_CFLAGS = \
	@GTHREAD_CFLAGS@ \
	@GTK_CFLAGS@ \
	@LIBXFCEGUI4_CFLAGS@ \
	@GNUTLS_CFLAGS@ \
	-DDATADIR=\"$(datadir)\" \
	-DLOCALEDIR=\"$(localedir)\"
