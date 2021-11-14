AC_INIT([apteryx_netconf], [1.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PROG_CC_STDC
AM_PROG_AR

LT_INIT([disable-static pic-only])

AC_CONFIG_MACRO_DIRS([m4])

PKG_CHECK_MODULES([GLIB],[glib-2.0])
PKG_CHECK_MODULES([LIBXML2],[libxml-2.0])
PKG_CHECK_MODULES([APTERYX],[apteryx])
PKG_CHECK_MODULES([APTERYX_XML],[apteryx-xml])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
