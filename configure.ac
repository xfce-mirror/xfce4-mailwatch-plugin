dnl configure.ac
dnl
dnl xfce4-mailwatch-plugin - a mail notification plugin for the xfce4 panel
dnl

dnl version info
m4_define([mailwatch_version], [1.0.2svn])

dnl init autoconf
AC_INIT([xfce4-mailwatch-plugin], [mailwatch_version], [bjt23@cornell.edu])
AC_PREREQ([2.50])

dnl init automake
MAILWATCH_VERSION=mailwatch_version
AM_INIT_AUTOMAKE([dist-bzip2])
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
XDT_I18N([ca cs de el en_GB eu fi fr gl gu he hu it ja pt_BR pt_PT ru sq uk vi zh_TW])

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

XDT_CHECK_PACKAGE([LIBXFCE4PANEL], [libxfce4panel-1.0], [4.3.20],
                  [have_44_panel=yes], [
echo "*** Package libxfce4panel not found.  Looking for old-style 4.2 panel"
])
if test "x$have_44_panel" != "xyes"; then
    XDT_XFCE_PANEL_PLUGIN([XFCE4_PANEL], [4.2.0])
fi
AM_CONDITIONAL([BUILD_42_PANEL_PLUGIN], [test "x$have_44_panel" != "xyes"])

dnl check for gnutls support
XDT_CHECK_OPTIONAL_PACKAGE([GNUTLS], [gnutls], [1.2.0], [ssl],
	[gnutls support for secure IMAP/POP3 connections], [yes])
if test "x$GNUTLS_FOUND" = "xyes"; then
	AC_DEFINE_UNQUOTED([HAVE_SSL_SUPPORT], [1], [Define if SSL support is enabled])
fi
AM_CONDITIONAL([HAVE_SSL_SUPPORT], [test "x$GNUTLS_FOUND" = "xyes"])

dnl to ipv6, or to not ipv6
AC_MSG_CHECKING([whether to support IPv6])
AC_ARG_ENABLE([ipv6],
              [AC_HELP_STRING([--disable-ipv6],
                              [Disable IPv6 support (default=enabled)])],
              [enable_ipv6_support=$enableval], [enable_ipv6_support=yes])
AC_MSG_RESULT([$enable_ipv6_support])
if test "x$enable_ipv6_support" = "xyes"; then
    AC_DEFINE([ENABLE_IPV6_SUPPORT], [1],
              [Define if IPv6 support should be enabled])
fi

dnl check for debugging support
XDT_FEATURE_DEBUG

AC_OUTPUT([
Makefile
doc/Makefile
doc/C/Makefile
doc/C/images/Makefile
doc/version
libmailwatch-core/Makefile
panel-plugin/Makefile
pixmaps/Makefile
po/Makefile.in
])
