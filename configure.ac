AC_INIT([apteryx_netconf], [1.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PROG_CC_STDC
AM_PROG_AR

LT_INIT([disable-static pic-only])

AC_CONFIG_MACRO_DIRS([m4])

PKG_CHECK_MODULES([GLIB],[glib-2.0])
PKG_CHECK_MODULES([APTERYX],[apteryx])
PKG_CHECK_MODULES([LIBYANG],[libyang])
PKG_CHECK_MODULES([LIBNETCONF2],[libnetconf2])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
