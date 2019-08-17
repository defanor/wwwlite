#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([WWWLite], [0.0.0], [defanor@uberspace.net])
AM_INIT_AUTOMAKE([-Wall])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES([LIBSOUP], [libsoup-2.4])
AC_SUBST(LIBSOUP_CFLAGS)
AC_SUBST(LIBSOUP_LIBS)

PKG_CHECK_MODULES([LIBXML], [libxml-2.0])
AC_SUBST(LIBXML_CFLAGS)
AC_SUBST(LIBXML_LIBS)

PKG_CHECK_MODULES([GTK3], [gtk+-3.0])
AC_SUBST(GTK3_CFLAGS)
AC_SUBST(GTK3_LIBS)

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strdup])

AC_OUTPUT
