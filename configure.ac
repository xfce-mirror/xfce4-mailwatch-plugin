dnl configure.ac
dnl
dnl xfce4-mailwatch-plugin - a mail notification plugin for the xfce4 panel
dnl

dnl version info
m4_define([mailwatch_version], [0.1.0svn])

dnl init autoconf
AC_INIT([xfce4-mailwatch-plugin], [mailwatch_version], [bjt23@cornell.edu])
AC_PREREQ([2.50])

dnl init automake
MAILWATCH_VERSION=mailwatch_version
AM_INIT_AUTOMAKE([xfce4-mailwatch-plugin], [$MAILWATCH_VERSION])
AM_CONFIG_HEADER([config.h])
AM_MAINTAINER_MODE

dnl check for UNIX variants
AC_AIX
AC_ISC_POSIX
AC_MINIX
AM_CONDITIONAL([HAVE_CYGWIN], [test "`uname | grep \"CYGWIN\"`" != ""])

dnl check for basic programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_INTLTOOL([0.31], [no-xml])

dnl check for standard header files
AC_HEADER_STDC
AC_CHECK_HEADERS(errno.h fcntl.h math.h netdb.h netinet/in.h signal.h stddef.h string.h sys/select.h sys/socket.h sys/stat.h sys/types.h sys/wait.h time.h unistd.h)
AC_CHECK_FUNCS(mmap sigaction srandom)

dnl Check for i18n support
XDT_I18N([de el eu fi gu ja])

dnl Check for X11 installed
XDT_CHECK_LIBX11_REQUIRE
XDT_CHECK_LIBSM

dnl required
XDT_CHECK_PACKAGE([GMODULE], [gmodule-2.0], [2.4.0])
XDT_CHECK_PACKAGE([GOBJECT], [gobject-2.0], [2.4.0])
XDT_CHECK_PACKAGE([GTHREAD], [gthread-2.0], [2.4.0])
XDT_CHECK_PACKAGE([GTK], [gtk+-2.0], [2.4.0])
XDT_CHECK_PACKAGE([LIBXFCE4UTIL], [libxfce4util-1.0], [4.2.0])
XDT_CHECK_PACKAGE([LIBXFCEGUI4], [libxfcegui4-1.0], [4.2.0])

XDT_CHECK_OPTIONAL_PACKAGE([LIBXFCE4PANEL], [libxfce4panel-1.0], [4.3.20])
if test "x$LIBXFCE4PANEL_FOUND" != "xyes"; then
    XDT_XFCE_PANEL_PLUGIN([XFCE4_PANEL], [4.2.0])
fi
AM_CONDITIONAL([BUILD_42_PANEL_PLUGIN], [test "x$LIBXFCE4PANEL_FOUND" != "xyes"])

dnl check for gnutls support
XDT_CHECK_OPTIONAL_PACKAGE([GNUTLS], [gnutls], [1.2.0], [ssl],
	[gnutls support for secure IMAP/POP3 connections], [yes])
if test "x$GNUTLS_FOUND" = "xyes"; then
	AC_DEFINE_UNQUOTED([HAVE_SSL_SUPPORT], [1], [Define if SSL support is enabled])
fi

dnl check for debugging support
XDT_FEATURE_DEBUG

AC_OUTPUT([
Makefile
doc/Makefile
doc/C/Makefile
doc/C/images/Makefile
doc/version
panel-plugin/Makefile
pixmaps/Makefile
po/Makefile.in
])
