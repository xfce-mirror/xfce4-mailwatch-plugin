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

dnl check for standard header files
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h math.h signal.h stddef.h string.h sys/stat.h sys/types.h sys/wait.h time.h unistd.h)
AC_CHECK_FUNCS(mmap sigaction srandom)

dnl Check for i18n support
XDT_I18N()

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
XDT_XFCE_PANEL_PLUGIN([XFCE4_PANEL], [4.2.0])

dnl check for debugging support
BM_DEBUG_SUPPORT

AC_OUTPUT([
Makefile
panel-plugin/Makefile
po/Makefile.in
])
