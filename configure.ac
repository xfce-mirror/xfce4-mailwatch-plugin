dnl
dnl xfce4-mailwatch-plugin - a mail notification plugin for the xfce4 panel
dnl

dnl ***************************
dnl *** Version information ***
dnl ***************************
m4_define([copyright_year], [2025])
XDT_VERSION_INIT([1.4.0], [git])

dnl ***************************
dnl *** Initialize autoconf ***
dnl ***************************
AC_COPYRIGHT([Copyright (c) 2005-copyright_year() The Xfce development team. All rights reserved.])
AC_INIT([xfce4-mailwatch-plugin], [xdt_version], [https://gitlab.xfce.org/panel-plugins/xfce4-mailwatch-plugin/])
AC_PREREQ([2.69])
AC_REVISION([xdt_version_build])
AC_USE_SYSTEM_EXTENSIONS()
AC_DEFINE([VERSION_FULL], [PACKAGE_VERSION], [Alias for VERSION and PACKAGE_VERSION for meson compatibility])
COPYRIGHT_YEAR=copyright_year()
AC_DEFINE_UNQUOTED([COPYRIGHT_YEAR], ["$COPYRIGHT_YEAR"], [Copyright year])
AC_SUBST([COPYRIGHT_YEAR])

dnl ***************************
dnl *** Initialize automake ***
dnl ***************************
AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-bzip2 tar-ustar foreign])
AC_CONFIG_HEADERS([config.h])
AM_MAINTAINER_MODE()
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_MACRO_DIRS([m4])

dnl **************************
dnl *** Initialize libtool ***
dnl **************************
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

dnl *******************************
dnl *** Check for UNIX variants ***
dnl *******************************
AC_USE_SYSTEM_EXTENSIONS
AC_SEARCH_LIBS([strerror],[cposix])

dnl ********************************
dnl *** Check for basic programs ***
dnl ********************************
AC_PROG_CC()
AM_PROG_CC_C_O()
LT_PATH_LD([])
AC_PROG_INSTALL()

dnl **********************************
dnl *** Check for standard headers ***
dnl **********************************
AC_CHECK_HEADERS([stdlib.h unistd.h locale.h stdio.h errno.h time.h string.h \
                  math.h sys/types.h sys/wait.h memory.h signal.h sys/prctl.h \
                  libintl.h fcntl.h netdb.h netinet/in.h stddef.h sys/select.h \
		  sys/socket.h sys/stat.h])
AC_CHECK_FUNCS([mmap sigaction srandom])

dnl ******************************
dnl *** Check for i18n support ***
dnl ******************************
GETTEXT_PACKAGE="$PACKAGE"
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Name of default gettext domain])
AC_SUBST([GETTEXT_PACKAGE])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.8])

dnl ***********************************
dnl *** Check for required packages ***
dnl ***********************************
XDT_CHECK_PACKAGE([GLIB], [glib-2.0], [2.50.0])
XDT_CHECK_PACKAGE([GTHREAD], [gthread-2.0], [2.50.0])
XDT_CHECK_PACKAGE([GTK], [gtk+-3.0], [3.22.0])
XDT_CHECK_PACKAGE([LIBXFCE4UI], [libxfce4ui-2], [4.16.0])
XDT_CHECK_PACKAGE([LIBXFCE4UTIL], [libxfce4util-1.0], [4.16.0])
XDT_CHECK_PACKAGE([LIBXFCE4PANEL], [libxfce4panel-2.0], [4.16.0])
XDT_CHECK_PACKAGE([EXO], [exo-2], [0.11.0])

dnl Check for gnutls support
enable_ssl_support=no
XDT_CHECK_OPTIONAL_PACKAGE([GNUTLS], [gnutls], [1.2.0], [ssl],
    [gnutls support for secure IMAP/POP3 connections], [yes])
if test "x$GNUTLS_FOUND" = "xyes"; then
    enable_ssl_support=yes
    AC_DEFINE([HAVE_SSL_SUPPORT], [1], [Define if SSL support is enabled])
fi
AM_CONDITIONAL([HAVE_SSL_SUPPORT], [test "x$enable_ssl_support" = "xyes"])

dnl Check for IPv6 support
AC_MSG_CHECKING([whether to support IPv6])
AC_ARG_ENABLE([ipv6],
              [AS_HELP_STRING([--disable-ipv6],
                              [Disable IPv6 support (default=enabled)])],
              [enable_ipv6_support=$enableval], [enable_ipv6_support=yes])
AC_MSG_RESULT([$enable_ipv6_support])
if test "x$enable_ipv6_support" = "xyes"; then
    AC_DEFINE([ENABLE_IPV6_SUPPORT], [1],
              [Define if IPv6 support should be enabled])
fi

dnl Suppress warnings related to GStaticMutexes
XDT_SUPPORTED_FLAGS([CFLAGS], [-fno-strict-aliasing])

dnl ***********************************
dnl *** Check for debugging support ***
dnl ***********************************
XDT_FEATURE_DEBUG([xdt_debug_default])

dnl *********************************
dnl *** Substitute platform flags ***
dnl *********************************
AC_MSG_CHECKING([PLATFORM_CPPFLAGS])
AC_MSG_RESULT([$PLATFORM_CPPFLAGS])
AC_SUBST([PLATFORM_CPPFLAGS])
AC_MSG_CHECKING([PLATFORM_CFLAGS])
AC_MSG_RESULT([$PLATFORM_CFLAGS])
AC_SUBST([PLATFORM_CFLAGS])
AC_MSG_CHECKING([PLATFORM_LDFLAGS])
AC_MSG_RESULT([$PLATFORM_LDFLAGS])
AC_SUBST([PLATFORM_LDFLAGS])

AC_DEFINE_UNQUOTED([WEBSITE],
                   ["https://docs.xfce.org/panel-plugins/xfce4-mailwatch-plugin"],
                   [The Mailwatch website])

AC_CONFIG_FILES([
Makefile
icons/Makefile
icons/48x48/Makefile
icons/scalable/Makefile
libmailwatch-core/Makefile
panel-plugin/Makefile
po/Makefile.in
])
AC_OUTPUT

dnl ***************************
dnl *** Print configuration ***
dnl ***************************
echo
echo "Build Configuration:"
echo
echo "* Debug Support:    $enable_debug"
echo
echo "* IPv6 Support:     $enable_ipv6_support"
echo "* SSL Support:      $enable_ssl_support"
echo
